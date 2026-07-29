#include "markdown_support.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;
namespace
{
using Json = nlohmann::json;
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
      : root_(fs::temp_directory_path() / "notepp_markdown_support_tests")
  {
    std::error_code error;
    fs::remove_all(root_, error);
    fs::create_directories(root_ / "notes");
    fs::create_directories(root_ / "config");
  }

  ~TempProject()
  {
    std::error_code error;
    fs::remove_all(root_, error);
  }

  const fs::path &root() const { return root_; }
  fs::path state_file() const { return root_ / "config" / "markdown_preview_state.json"; }

private:
  fs::path root_;
};

Json captured_preview()
{
  const Json snapshot = Json::parse(MarkdownSupport::capture_preview_state_snapshot());
  return snapshot.at("preview");
}

void test_preview_key_collision_preserves_both_values()
{
  TempProject project;
  const fs::path note = project.root() / "notes" / "note.md";
  std::ofstream(note) << "note";
  const Json portable_value = {{"open", true}};
  const Json legacy_value = {{"open", false}};
  const Json state = {
      {"documents", Json{{"notes/note.md", portable_value},
                         {note.string(), legacy_value}}}};
  std::ofstream(project.state_file()) << state.dump(2);

  MarkdownSupport::set_preview_state_path(project.state_file());
  const Json migrated = captured_preview();
  expect(migrated["documents"].contains("notes/note.md"),
         "one preview state is retained under the portable key");
  expect(migrated["legacyDocuments"].size() == 1,
         "the colliding preview state is quarantined instead of overwritten");
  const Json document_value = migrated["documents"]["notes/note.md"];
  const Json quarantined_value = migrated["legacyDocuments"].begin().value();
  expect((document_value == portable_value && quarantined_value == legacy_value) ||
             (document_value == legacy_value && quarantined_value == portable_value),
         "both colliding preview values survive migration");
}

void test_preview_state_stale_save_preserves_both_versions()
{
  TempProject project;
  const Json initial = {{"documents", Json::object()}};
  std::ofstream(project.state_file()) << initial.dump(2);

  MarkdownSupport::set_preview_state_path(project.state_file());
  Json snapshot = Json::parse(MarkdownSupport::capture_preview_state_snapshot());
  snapshot["preview"]["documents"]["notes/local.md"] = {{"open", true}};

  const std::string external = Json{{"documents", Json{{"notes/external.md", {{"open", false}}}}}}.dump(2);
  std::ofstream(project.state_file(), std::ios::trunc) << external;
  MarkdownSupport::apply_preview_state_snapshot(snapshot.dump());

  std::ifstream canonical_file(project.state_file());
  const std::string canonical((std::istreambuf_iterator<char>(canonical_file)),
                              std::istreambuf_iterator<char>());
  expect(canonical == external, "stale preview save preserves external canonical bytes");
  expect(!MarkdownSupport::last_persistence_error().empty(),
         "stale preview save exposes an actionable error");
  expect(!MarkdownSupport::flush_preview_state(),
         "stale preview state remains dirty after failed save");

  bool found_conflict = false;
  for(const auto &entry : fs::directory_iterator(project.state_file().parent_path()))
    if(entry.path() != project.state_file() &&
       entry.path().filename().string().find("notepp-conflict") != std::string::npos)
      found_conflict = true;
  expect(found_conflict, "stale preview save preserves local bytes in a conflict file");
}

void test_failed_preview_load_does_not_apply_placeholder_mutation()
{
  TempProject project;
  fs::create_directory(project.state_file());

  MarkdownSupport::set_preview_state_path(project.state_file());
  const bool changed = MarkdownSupport::set_all_preview_headers_open(
      "notes/failed.md", "# Heading\n", true);
  expect(!changed, "preview mutation is rejected when canonical state cannot load");
  expect(!MarkdownSupport::last_persistence_error().empty(),
         "failed preview load exposes an actionable error");
  expect(MarkdownSupport::flush_preview_state(),
         "rejected preview mutation does not leave blank placeholder state dirty");

  fs::remove_all(project.state_file());
  const Json canonical = {
      {"documents", Json{{"notes/external.md", {{"headers", {{"0", false}}}}}}}};
  std::ofstream(project.state_file()) << canonical.dump(2);

  const Json loaded = captured_preview();
  expect(loaded["documents"].contains("notes/external.md"),
         "successful retry loads canonical preview state");
  expect(!loaded["documents"].contains("notes/failed.md"),
         "failed-load mutation was never applied to a placeholder");
}

void test_quarantined_preview_key_is_retried()
{
  TempProject project;
  const Json value = {{"open", true}};
  const Json state = {
      {"documents", Json::object()},
      {"legacyDocuments", Json{{"/old/device/project/notes/restored.md", value}}}};
  std::ofstream(project.state_file()) << state.dump(2);

  MarkdownSupport::set_preview_state_path(project.state_file());
  Json migrated = captured_preview();
  expect(migrated["legacyDocuments"].size() == 1,
         "missing legacy preview target remains quarantined");

  std::ofstream(project.root() / "notes" / "restored.md") << "restored";
  MarkdownSupport::set_preview_state_path(project.state_file());
  migrated = captured_preview();
  expect(migrated["documents"].value("notes/restored.md", Json{}) == value,
         "quarantined preview key migrates after its note is restored");
  expect(migrated["legacyDocuments"].empty(),
         "successfully retried preview key leaves quarantine");
}
} // namespace

int main()
{
  test_preview_key_collision_preserves_both_values();
  test_preview_state_stale_save_preserves_both_versions();
  test_failed_preview_load_does_not_apply_placeholder_mutation();
  test_quarantined_preview_key_is_retried();
  if(failures != 0)
  {
    std::cerr << failures << " markdown_support test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "markdown_support tests passed\n";
  return EXIT_SUCCESS;
}
