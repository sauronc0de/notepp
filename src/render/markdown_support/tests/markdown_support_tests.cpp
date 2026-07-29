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
  test_quarantined_preview_key_is_retried();
  if(failures != 0)
  {
    std::cerr << failures << " markdown_support test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "markdown_support tests passed\n";
  return EXIT_SUCCESS;
}
