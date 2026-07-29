#include "atomic_file.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
namespace af = atomic_file;

namespace
{
int failures = 0;

void expect(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

void expect_equal(std::string_view actual, std::string_view expected, std::string_view message)
{
  if(actual == expected) return;
  ++failures;
  std::cerr << "FAIL: " << message << " (got \"" << actual << "\", expected \"" << expected << "\")\n";
}

class TempDirectory
{
public:
  TempDirectory()
  {
    static unsigned int counter = 0;
    path_ = fs::temp_directory_path() /
            ("notepp_atomic_file_tests_" + std::to_string(++counter));
    std::error_code error;
    fs::remove_all(path_, error);
    fs::create_directories(path_);
  }

  ~TempDirectory()
  {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  const fs::path &path() const noexcept { return path_; }

private:
  fs::path path_;
};

void write_direct(const fs::path &path, std::string_view content)
{
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << content;
}

std::string read_direct(const fs::path &path)
{
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool has_temp_sibling(const fs::path &directory)
{
  for(const auto &entry : fs::directory_iterator(directory))
    if(entry.path().filename().string().find(".~npp-t-") != std::string::npos)
      return true;
  return false;
}

void test_create_replace_and_unchanged()
{
  TempDirectory temp;
  const fs::path file = temp.path() / "nested" / "note.md";

  const af::SaveResult created = af::save_text(file, "first");
  expect(created.disposition == af::SaveDisposition::saved, "missing file is created");
#ifndef _WIN32
  expect(created.durability_confirmed, "POSIX create confirms directory durability");
#endif
  expect_equal(read_direct(file), "first", "created bytes match");
  expect(created.new_snapshot == af::Snapshot{true, "first"}, "created snapshot returned");

  const af::SaveResult replaced = af::save_text(file, "second", &created.new_snapshot);
  expect(replaced.disposition == af::SaveDisposition::saved, "existing file is replaced");
  expect_equal(read_direct(file), "second", "replacement bytes match");

  const af::SaveResult unchanged = af::save_text(file, "second", &replaced.new_snapshot);
  expect(unchanged.disposition == af::SaveDisposition::unchanged, "identical content is unchanged");
  expect(!has_temp_sibling(file.parent_path()), "successful saves leave no temporary file");
}

void test_stale_content_is_preserved_exactly()
{
  TempDirectory temp;
  const fs::path file = temp.path() / "note.md";
  write_direct(file, "loaded");
  const af::ReadResult loaded = af::read_text(file);
  expect(static_cast<bool>(loaded), "initial snapshot reads");

  write_direct(file, "external");
  const std::string local("local\0bytes", 11);
  const af::SaveResult stale = af::save_text(file, local, &loaded.snapshot);
  expect(stale.disposition == af::SaveDisposition::stale_preserved, "stale save is preserved");
  expect_equal(read_direct(file), "external", "stale save leaves canonical bytes untouched");
  expect(fs::exists(stale.recovery_path), "stale save creates recovery file");
  expect_equal(read_direct(stale.recovery_path), local, "recovery bytes are exact");
  expect(stale.recovery_path.extension() == ".md", "recovery preserves original extension");
  expect(!has_temp_sibling(temp.path()), "stale save leaves no temporary file");
}

void test_expected_missing_race_preserves_both_versions()
{
  TempDirectory temp;
  const fs::path file = temp.path() / "race.md";
  const af::Snapshot expected_missing{false, {}};
  write_direct(file, "arrived");

  const af::SaveResult result = af::save_text(file, "local", &expected_missing);
  expect(result.disposition == af::SaveDisposition::stale_preserved,
         "file appearing after missing snapshot is stale");
  expect_equal(read_direct(file), "arrived", "appeared canonical file is retained");
  expect_equal(read_direct(result.recovery_path), "local", "racing local bytes are retained");
}

void test_unicode_and_spaces()
{
  TempDirectory temp;
  const fs::path file = temp.path() / "folder with spaces" / "unicodé note.md";
  const std::string content = "héllo ✓";
  const af::SaveResult result = af::save_text(file, content);
  expect(static_cast<bool>(result), "Unicode and space path saves");
  const af::ReadResult read = af::read_text(file);
  expect(static_cast<bool>(read), "Unicode and space path reads");
  expect_equal(read.snapshot.content, content, "Unicode content round trips");
}

void test_unique_conflict_names()
{
  TempDirectory temp;
  const fs::path file = temp.path() / "same.md";
  write_direct(file, "base");
  const af::ReadResult loaded = af::read_text(file);
  write_direct(file, "external");

  const af::SaveResult first = af::save_text(file, "local one", &loaded.snapshot);
  const af::SaveResult second = af::save_text(file, "local two", &loaded.snapshot);
  expect(first.disposition == af::SaveDisposition::stale_preserved, "first conflict preserved");
  expect(second.disposition == af::SaveDisposition::stale_preserved, "second conflict preserved");
  expect(first.recovery_path != second.recovery_path, "conflict names are unique");
  expect_equal(read_direct(first.recovery_path), "local one", "first conflict bytes retained");
  expect_equal(read_direct(second.recovery_path), "local two", "second conflict bytes retained");
}

void test_second_check_catches_synchronized_writer()
{
  TempDirectory temp;
  const fs::path file = temp.path() / "race.md";
  write_direct(file, "loaded");
  const af::ReadResult loaded = af::read_text(file);
  expect(static_cast<bool>(loaded), "race snapshot reads");

  const std::string local(64U * 1024U * 1024U, 'L');
  std::atomic<bool> saw_temp{false};
  std::atomic<bool> temp_was_private{false};
  std::thread external_writer([&] {
    for(unsigned int attempt = 0; attempt < 200000U; ++attempt)
    {
      std::error_code error;
      for(const auto &entry : fs::directory_iterator(temp.path(), error))
      {
        if(error) break;
        if(entry.path().filename().string().find(".~npp-t-") == std::string::npos)
          continue;
        saw_temp.store(true, std::memory_order_release);
#ifdef _WIN32
        HANDLE reader = CreateFileW(entry.path().c_str(), GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(reader == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION)
          temp_was_private.store(true, std::memory_order_release);
        else if(reader != INVALID_HANDLE_VALUE)
          CloseHandle(reader);
#else
        struct stat status
        {
        };
        if(::stat(entry.path().c_str(), &status) == 0 && (status.st_mode & 0077) == 0)
          temp_was_private.store(true, std::memory_order_release);
#endif
        write_direct(file, "external-during-save");
        return;
      }
      std::this_thread::yield();
    }
  });

  const af::SaveResult result = af::save_text(file, local, &loaded.snapshot);
  external_writer.join();
  expect(saw_temp.load(std::memory_order_acquire), "synchronized writer observed prepared temp");
  expect(temp_was_private.load(std::memory_order_acquire), "temporary file is private while incomplete");
  expect(result.disposition == af::SaveDisposition::stale_preserved,
         "second comparison detects writer during preparation");
  expect_equal(read_direct(file), "external-during-save", "racing canonical bytes survive");
  expect(fs::exists(result.recovery_path), "racing local bytes receive recovery path");
  expect(read_direct(result.recovery_path) == local, "racing local bytes survive exactly");
}

void test_final_component_symlink_is_rejected()
{
  TempDirectory temp;
  const fs::path target = temp.path() / "target.md";
  const fs::path link = temp.path() / "link.md";
  write_direct(target, "target");
  std::error_code error;
  fs::create_symlink(target.filename(), link, error);
  if(error)
    return;

  const af::ReadResult read = af::read_text(link);
  expect(!read, "reading a final-component symlink is rejected");
  const af::SaveResult save = af::save_text(link, "replacement");
  expect(save.disposition == af::SaveDisposition::io_error,
         "saving a final-component symlink is rejected");
  expect(fs::is_symlink(fs::symlink_status(link)), "rejected save preserves symlink");
  expect_equal(read_direct(target), "target", "rejected save preserves symlink target");
}

void test_long_canonical_name_allows_temp_and_conflict_names()
{
  TempDirectory temp;
  const fs::path file = temp.path() / (std::string(220, 'a') + ".md");
  const af::SaveResult created = af::save_text(file, "loaded");
  expect(static_cast<bool>(created), "long canonical name saves");
  write_direct(file, "external");

  const af::SaveResult stale = af::save_text(file, "local", &created.new_snapshot);
  expect(stale.disposition == af::SaveDisposition::stale_preserved,
         "long canonical name permits conflict preservation");
  expect(stale.recovery_path.extension() == ".md", "long-name conflict keeps extension");
  expect(stale.recovery_path.filename().native().size() <= 240U,
         "generated sibling component stays bounded");
  expect_equal(read_direct(stale.recovery_path), "local", "long-name conflict bytes survive");
}

void test_existing_permissions_are_retained()
{
#ifndef _WIN32
  TempDirectory temp;
  const fs::path file = temp.path() / "permissions.md";
  write_direct(file, "before");
  expect(::chmod(file.c_str(), 0640) == 0, "test sets canonical permissions");
  const af::ReadResult loaded = af::read_text(file);
  const af::SaveResult saved = af::save_text(file, "after", &loaded.snapshot);
  struct stat status
  {
  };
  expect(static_cast<bool>(saved), "permission-preserving replacement saves");
  expect(::stat(file.c_str(), &status) == 0, "replacement permissions can be inspected");
  expect((status.st_mode & 0777) == 0640, "replacement retains canonical permissions");
#endif
}

void test_directory_durability_warning_is_nonfatal()
{
#ifndef _WIN32
  TempDirectory temp;
  const fs::path restricted = temp.path() / "write-only-directory";
  fs::create_directory(restricted);
  expect(::chmod(restricted.c_str(), 0300) == 0, "test restricts directory reads");

  const fs::path file = restricted / "note.md";
  const af::SaveResult saved = af::save_text(file, "content");
  expect(saved.disposition == af::SaveDisposition::saved,
         "directory flush failure does not misreport publication failure");
  expect(!saved.durability_confirmed, "directory flush failure is reported");
  expect(!saved.message.empty(), "durability warning includes details");

  expect(::chmod(restricted.c_str(), 0700) == 0, "test restores directory permissions");
  expect_equal(read_direct(file), "content", "published bytes survive durability warning");
#endif
}

void test_io_errors_are_structured_and_clean()
{
  TempDirectory temp;
  const fs::path blocking_parent = temp.path() / "not-a-directory";
  write_direct(blocking_parent, "block");
  const fs::path target = blocking_parent / "note.md";

  const af::SaveResult result = af::save_text(target, "content");
  expect(result.disposition == af::SaveDisposition::io_error, "invalid parent returns I/O error");
  expect(!result.message.empty(), "I/O error includes a message");
  expect_equal(read_direct(blocking_parent), "block", "I/O error leaves blocker untouched");
  expect(!has_temp_sibling(temp.path()), "I/O error leaves no temporary file");

  const af::ReadResult directory_read = af::read_text(temp.path());
  expect(!directory_read, "reading a directory returns a structured error");
}
} // namespace

int main()
{
  test_create_replace_and_unchanged();
  test_stale_content_is_preserved_exactly();
  test_expected_missing_race_preserves_both_versions();
  test_unicode_and_spaces();
  test_unique_conflict_names();
  test_second_check_catches_synchronized_writer();
  test_final_component_symlink_is_rejected();
  test_long_canonical_name_allows_temp_and_conflict_names();
  test_existing_permissions_are_retained();
  test_directory_durability_warning_is_nonfatal();
  test_io_errors_are_structured_and_clean();

  if(failures != 0)
  {
    std::cerr << failures << " atomic_file test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "atomic_file tests passed\n";
  return EXIT_SUCCESS;
}
