#include "note_project.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace npp = notepp::project;

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

void expect_eq_int(long long a, long long b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got " << a << ", expected " << b << ")\n";
}

class ScopedXdgConfig
{
public:
  ScopedXdgConfig()
  {
    static int counter = 0;
    const std::string base = (fs::temp_directory_path() /
                              ("notepp_project_tests_" + std::to_string(static_cast<long long>(++counter))))
                                 .generic_string();
    fs::create_directories(base);
    prev_ = std::getenv("XDG_CONFIG_HOME");
    if(prev_) previous_value_ = prev_;
    setenv("XDG_CONFIG_HOME", base.c_str(), 1);
    base_ = base;
  }
  ~ScopedXdgConfig()
  {
    if(prev_)
      setenv("XDG_CONFIG_HOME", previous_value_.c_str(), 1);
    else
      unsetenv("XDG_CONFIG_HOME");
    std::error_code ec;
    fs::remove_all(base_, ec);
  }
  const fs::path &base() const { return base_; }

private:
  const char *prev_ = nullptr;
  std::string previous_value_;
  fs::path base_;
};

void test_create_or_open_project_creates_layout()
{
  ScopedXdgConfig env;
  const fs::path root = env.base() / "projA";
  fs::create_directories(root);

  npp::ProjectInfo info = npp::create_or_open_project(root);
  expect_eq_str(info.root.generic_string(), root.generic_string(), "project root matches");
  expect_true(fs::exists(info.notes), "notes dir created");
  expect_true(fs::exists(info.assets), "assets dir created");
  expect_true(fs::exists(info.config), "config dir created");
  expect_true(fs::exists(info.projectFile), "project file created");

  // Reopen should not overwrite project file.
  const auto mtime1 = fs::last_write_time(info.projectFile);
  npp::create_or_open_project(root);
  const auto mtime2 = fs::last_write_time(info.projectFile);
  expect_eq_int(static_cast<long long>(mtime1 == mtime2), 1, "reopen does not rewrite project file");
}

void test_save_and_load_last_project_path()
{
  ScopedXdgConfig env;
  const fs::path root = env.base() / "projB";
  fs::create_directories(root);

  expect_true(!npp::load_last_project_path().has_value(), "no last project before saving");

  npp::save_last_project_path(root);
  auto loaded = npp::load_last_project_path();
  expect_true(loaded.has_value(), "last project loaded after save");
  expect_eq_str(loaded->generic_string(), root.generic_string(), "loaded path matches");
}

void test_recent_projects_dedupe_and_cap()
{
  ScopedXdgConfig env;
  const fs::path a = env.base() / "recA";
  const fs::path b = env.base() / "recB";
  const fs::path c = env.base() / "recC";
  fs::create_directories(a);
  fs::create_directories(b);
  fs::create_directories(c);

  // Add 12 entries (over the 10 cap), with one duplicate.
  for(int i = 0; i < 12; ++i)
  {
    const fs::path p = (i % 3 == 0) ? a : (i % 3 == 1) ? b
                                                       : c;
    npp::save_last_project_path(p);
  }

  auto recents = npp::load_recent_projects();
  expect_eq_int(static_cast<long long>(recents.size()), 3, "deduped to 3 unique existing paths");
}

// Regression for the WSL crash where ~/.config/Notepp could not be created
// (permission denied). The directory-creation failure must NOT throw out of
// the public API; it must be handled internally so the application can keep
// running. We simulate the failure portably by pointing XDG_CONFIG_HOME at a
// regular file: std::filesystem::create_directories fails when the target
// exists but is not a directory.
void test_save_last_project_path_does_not_throw_on_creation_failure()
{
  static int counter = 0;
  const fs::path base = fs::temp_directory_path() /
                        ("notepp_project_tests_readonly_" + std::to_string(static_cast<long long>(++counter)));
  fs::create_directories(base);
  const fs::path blockingFile = base / "Notepp";
  {
    std::ofstream f(blockingFile);
    f << "not a directory";
  }

  const char *prev = std::getenv("XDG_CONFIG_HOME");
  std::string previousValue;
  if(prev) previousValue = prev;
  setenv("XDG_CONFIG_HOME", blockingFile.generic_string().c_str(), 1);

  const fs::path root = base / "projC";
  fs::create_directories(root);

  bool threw = false;
  try
  {
    npp::save_last_project_path(root);
  }
  catch(const std::exception &)
  {
    threw = true;
  }

  if(prev)
    setenv("XDG_CONFIG_HOME", previousValue.c_str(), 1);
  else
    unsetenv("XDG_CONFIG_HOME");

  std::error_code cleanupEc;
  fs::remove_all(base, cleanupEc);

  expect_true(!threw, "save_last_project_path must not throw when config dir cannot be created");
}

// Companion regression for create_or_open_project: when the project root
// cannot host subdirectories (simulated by pointing root at a regular file),
// the function must not throw either. It still returns a ProjectInfo with
// the originally requested paths so callers see a coherent value.
void test_create_or_open_project_does_not_throw_on_creation_failure()
{
  static int counter = 0;
  const fs::path base = fs::temp_directory_path() /
                        ("notepp_project_tests_rootblock_" + std::to_string(static_cast<long long>(++counter)));
  fs::create_directories(base);
  const fs::path blockingRoot = base / "blockingRoot";
  {
    std::ofstream f(blockingRoot);
    f << "not a directory";
  }

  bool threw = false;
  try
  {
    (void)npp::create_or_open_project(blockingRoot);
  }
  catch(const std::exception &)
  {
    threw = true;
  }

  std::error_code cleanupEc;
  fs::remove_all(base, cleanupEc);

  expect_true(!threw, "create_or_open_project must not throw when project subdirs cannot be created");
}
} // namespace

int main()
{
  test_create_or_open_project_creates_layout();
  test_save_and_load_last_project_path();
  test_recent_projects_dedupe_and_cap();
  test_save_last_project_path_does_not_throw_on_creation_failure();
  test_create_or_open_project_does_not_throw_on_creation_failure();
  if(failures != 0)
  {
    std::cerr << failures << " note_project test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "note_project tests passed\n";
  return EXIT_SUCCESS;
}
