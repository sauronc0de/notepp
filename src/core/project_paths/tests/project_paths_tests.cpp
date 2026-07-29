#include "project_paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;
using notepp::project_paths::PathError;
using notepp::project_paths::ProjectPaths;

namespace
{
int failures = 0;

void expect(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

class TempProject
{
public:
  TempProject()
      : root_(fs::temp_directory_path() / "notepp_project_paths_tests"),
        outside_(fs::temp_directory_path() / "notepp_project_paths_outside")
  {
    std::error_code error;
    fs::remove_all(root_, error);
    fs::remove_all(outside_, error);
    fs::create_directories(root_ / "notes" / "work");
    fs::create_directories(root_ / "assets");
    fs::create_directories(outside_);
  }

  ~TempProject()
  {
    std::error_code error;
    fs::remove_all(root_, error);
    fs::remove_all(outside_, error);
  }

  const fs::path &root() const { return root_; }
  const fs::path &outside() const { return outside_; }

private:
  fs::path root_;
  fs::path outside_;
};

void test_round_trip()
{
  TempProject project;
  ProjectPaths paths(project.root());
  const fs::path note = project.root() / "notes" / "work" / "plan.md";

  const auto encoded = paths.encode(note);
  expect(encoded && *encoded == "notes/work/plan.md", "encode uses project-relative generic path");
  const auto decoded = paths.decode(*encoded);
  expect(decoded && *decoded == note.lexically_normal(), "decode restores absolute runtime path");
  const auto stable = paths.stable_key(note);
  expect(stable && *stable == *encoded, "stable key is portable");
}

void test_moved_root_legacy_migration()
{
  TempProject project;
  const fs::path note = project.root() / "notes" / "work" / "plan.md";
  std::ofstream(note) << "portable";
  ProjectPaths paths(project.root());

  const auto linux_path = paths.migrate_legacy("/home/alice/old/notes/work/plan.md", "notes");
  expect(linux_path && linux_path->absolute_path == note, "migrates moved Linux root by notes suffix");
  expect(linux_path && linux_path->stored_path == "notes/work/plan.md", "Linux migration stores relative path");

  const auto windows_path = paths.migrate_legacy("C:\\Users\\Alice\\old\\notes\\work\\plan.md", "notes");
  expect(windows_path && windows_path->absolute_path == note, "migrates moved Windows root on any platform");
}

void test_rejects_unsafe_paths()
{
  TempProject project;
  ProjectPaths paths(project.root());

  expect(!paths.decode("../outside.md") && paths.decode("../outside.md").error() == PathError::traversal,
         "decode rejects parent traversal");
  expect(!paths.decode("/tmp/outside.md") && paths.decode("/tmp/outside.md").error() == PathError::absolute,
         "decode rejects absolute v2 path");
  expect(!paths.encode(project.root().parent_path() / "outside.md"), "encode rejects out-of-root path");
  expect(!paths.migrate_legacy("/old/notes/x/notes/work/plan.md", "notes"),
         "legacy migration rejects ambiguous suffix");
  expect(!paths.migrate_legacy("/old/notes/work/missing.md", "notes"),
         "legacy migration requires a target at the moved root");
  expect(!paths.migrate_legacy("/old/notes/../assets/icon.png", "notes"),
         "legacy migration rejects traversal components");

  std::error_code symlink_error;
  fs::create_directory_symlink(project.outside(), project.root() / "notes" / "external", symlink_error);
  if(!symlink_error)
    expect(!paths.decode("notes/external/escaped.md"), "decode rejects an existing symlink escape");
}
} // namespace

int main()
{
  test_round_trip();
  test_moved_root_legacy_migration();
  test_rejects_unsafe_paths();

  if(failures != 0)
  {
    std::cerr << failures << " project_paths test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "project_paths tests passed\n";
  return EXIT_SUCCESS;
}
