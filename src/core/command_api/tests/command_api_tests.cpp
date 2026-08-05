#include "command_api.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main()
{
  const auto root = std::filesystem::temp_directory_path() / "notepp-command-api-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "notes");
  std::filesystem::create_directories(root / "config");
  {
    std::ofstream index(root / "config" / "notes_index.json");
    index << R"({"schemaVersion":3,"folders":[{"name":"General","notes":[{"id":"one","title":"One","path":"notes/one.md"}]}]})";
  }
  {
    std::ofstream note(root / "notes" / "one.md");
    note << "# Root\n\nbody\n";
  }
  notepp::project::ProjectInfo project;
  project.root = root;
  project.notes = root / "notes";
  project.config = root / "config";
  notepp::command_api::Api api(project);
  const auto capabilities = api.execute({{"command", "app.capabilities"}});
  assert(capabilities.ok);
  assert(capabilities.body.at("commands").size() == 10U);
  const auto envelope = nlohmann::json::parse(capabilities.serialize());
  assert(envelope.at("success").get<bool>() && envelope.contains("result"));
  assert(api.execute({{"command", "note.list"}}).ok);
  const auto traversal = api.execute({{"command", "note.get"},
                                      {"args", {{"path", "../outside.md"}}}});
  assert(!traversal.ok && traversal.body.at("error").at("code") == "not_found");
  const auto headers = api.execute({{"command", "note.header.list"}, {"args", {{"id", "one"}}}});
  assert(headers.ok && headers.body.at("headers").size() == 1U);
  const auto created_note = api.execute({{"command", "note.create"},
                                         {"args", {{"folder", "General"}, {"title", "Created"}, {"content", "content"}}}});
  assert(created_note.ok);
  assert(created_note.body.at("note").at("path") == "notes/General/Created.md");
  {
    std::ifstream index(root / "config" / "notes_index.json");
    const auto document = nlohmann::json::parse(index);
    assert(document.at("folders").at(0).at("notes").at(1).at("path") == "notes/General/Created.md");
  }
  const auto created = api.execute({{"command", "note.header.create"},
                                    {"args", {{"id", "one"}, {"title", "Child"}, {"parent", "Root"}}}});
  assert(created.ok);
  const auto bad_header = api.execute({{"command", "note.header.create"},
                                        {"args", {{"id", "one"}, {"title", "bad\nheader"}}}});
  assert(!bad_header.ok);
  const auto variables = api.execute({{"command", "note.variable.get"}, {"args", {{"id", "one"}}}});
  assert(!variables.ok && variables.body.at("error").at("code") == "adapter_unavailable");
  std::filesystem::remove_all(root);
}
