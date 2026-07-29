#include "atomic_file.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

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
    if(entry.path().filename().string().find(".notepp-tmp-") != std::string::npos)
      return true;
  return false;
}

void test_create_replace_and_unchanged()
{
  TempDirectory temp;
  const fs::path file = temp.path() / "nested" / "note.md";

  const af::SaveResult created = af::save_text(file, "first");
  expect(created.disposition == af::SaveDisposition::saved, "missing file is created");
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
  test_io_errors_are_structured_and_clean();

  if(failures != 0)
  {
    std::cerr << failures << " atomic_file test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "atomic_file tests passed\n";
  return EXIT_SUCCESS;
}
