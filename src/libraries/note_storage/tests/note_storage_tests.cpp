#include "note_content_cache.hpp"
#include "note_path.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace ns = notepp::note_storage;

namespace
{
int failures = 0;

void expect_true(bool cond, std::string_view msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void expect_eq_str(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void write_file(const fs::path &p, std::string_view content)
{
  std::ofstream(p, std::ios::binary).write(content.data(), static_cast<std::streamsize>(content.size()));
}

void test_make_note_path_root()
{
  fs::path root = "/tmp/notepp_storage_test";
  fs::path p = ns::make_note_path(root, "", "My Note!");
  expect_eq_str(p.filename().string(), "My Note!.md", "sanitized note filename");
  expect_eq_str(p.parent_path().string(), root.string(), "root parent preserved");
}

void test_make_note_path_nested_folder()
{
  fs::path root = "/tmp/notepp_storage_test";
  fs::path p = ns::make_note_path(root, "a/b", "Note");
  expect_eq_str(p.filename().string(), "Note.md", "simple note filename");
  expect_eq_str(p.parent_path().filename().string(), "b", "nested folder");
  expect_eq_str(p.parent_path().parent_path().filename().string(), "a", "outer folder");
}

void test_make_unique_note_title_no_collision()
{
  std::vector<std::string> existing = {"Other"};
  std::string t = ns::make_unique_note_title(existing, "Note");
  expect_eq_str(t, "Note", "no collision returns base");
}

void test_make_unique_note_title_with_collision()
{
  std::vector<std::string> existing = {"Note", "Note 2", "Note 3"};
  std::string t = ns::make_unique_note_title(existing, "Note");
  expect_eq_str(t, "Note 4", "collision increments suffix");
}

void test_make_unique_note_title_empty_base()
{
  std::vector<std::string> existing;
  std::string t = ns::make_unique_note_title(existing, "");
  expect_eq_str(t, "Note", "empty base becomes Note");
}

void test_cache_loads_from_disk()
{
  fs::path tmp = fs::temp_directory_path() / "notepp_storage_cache_load.txt";
  write_file(tmp, "hello");

  ns::NoteContentCache cache;
  expect_eq_str(cache.get(tmp.string()), "hello", "loaded from disk");
  expect_true(cache.contains(tmp.string()), "cache contains path after get");
  fs::remove(tmp);
}

void test_cache_update_and_reload()
{
  fs::path tmp = fs::temp_directory_path() / "notepp_storage_cache_update.txt";
  write_file(tmp, "first");

  ns::NoteContentCache cache;
  cache.update(tmp.string(), "manual");
  expect_eq_str(cache.get(tmp.string()), "manual", "returns manually updated text");
  expect_true(cache.size() == 1, "size is 1 after update");

  write_file(tmp, "second");
  cache.update(tmp.string(), "second");
  expect_eq_str(cache.get(tmp.string()), "second", "reload after external write");
  fs::remove(tmp);
}

void test_cache_invalidate()
{
  fs::path tmp = fs::temp_directory_path() / "notepp_storage_cache_invalidate.txt";
  write_file(tmp, "old");

  ns::NoteContentCache cache;
  cache.get(tmp.string());
  expect_true(cache.contains(tmp.string()), "valid before invalidate");
  cache.invalidate(tmp.string());
  expect_true(!cache.contains(tmp.string()), "invalid after invalidate");
  fs::remove(tmp);
}

void test_cache_eviction()
{
  ns::NoteContentCache cache(2);
  cache.update("/virtual/a", "1");
  cache.update("/virtual/b", "2");
  cache.update("/virtual/c", "3");
  expect_true(cache.size() == 2, "size capped at max_entries");
  fs::remove("/virtual/a");
  fs::remove("/virtual/b");
  fs::remove("/virtual/c");
}

void test_cache_clear()
{
  ns::NoteContentCache cache;
  cache.update("/virtual/x", "x");
  cache.update("/virtual/y", "y");
  cache.clear();
  expect_true(cache.size() == 0, "size is 0 after clear");
}
} // namespace

int main()
{
  test_make_note_path_root();
  test_make_note_path_nested_folder();
  test_make_unique_note_title_no_collision();
  test_make_unique_note_title_with_collision();
  test_make_unique_note_title_empty_base();
  test_cache_loads_from_disk();
  test_cache_update_and_reload();
  test_cache_invalidate();
  test_cache_eviction();
  test_cache_clear();
  if(failures != 0)
  {
    std::cerr << failures << " note_storage test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "note_storage tests passed\n";
  return EXIT_SUCCESS;
}