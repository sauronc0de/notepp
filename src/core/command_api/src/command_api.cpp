#include "command_api.hpp"

#include "atomic_file.hpp"
#include "markdown_sections.hpp"
#include "note_index.hpp"
#include "note_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace notepp::command_api
{
namespace
{
using Json = nlohmann::json;

Response success(Json value)
{
  return {true, std::move(value)};
}
Response failure(std::string code, std::string message)
{
  return {false, Json{{"error", {{"code", std::move(code)}, {"message", std::move(message)}}}}};
}
struct IndexData
{
  Json document = Json{{"schemaVersion", notepp::note_index::current_schema_version},
                       {"folders", Json::array()}};
};

std::filesystem::path index_path(const project::ProjectInfo &project)
{
  return project.config / "notes_index.json";
}

std::filesystem::path note_path(const project::ProjectInfo &project, const Json &note);

bool load_index(const project::ProjectInfo &project, IndexData &out, std::string &error)
{
  const auto loaded = atomic_file::read_text(index_path(project));
  if(!loaded)
  {
    error = loaded.message;
    return false;
  }
  if(loaded.snapshot.existed)
  {
    try { out.document = Json::parse(loaded.snapshot.content); }
    catch(const std::exception &e) { error = e.what(); return false; }
    if(!out.document.is_object() || !out.document.value("folders", Json{}).is_array())
    {
      error = "notes index is not a valid object";
      return false;
    }
    for(const auto &folder : out.document["folders"])
      for(const auto &note : folder.value("notes", Json::array()))
        if(note_path(project, note).empty())
        {
          error = "notes index contains a path outside the project notes root";
          return false;
        }
  }
  return true;
}

bool save_index(const project::ProjectInfo &project, const Json &document, std::string &error)
{
  std::string text = document.dump(2);
  text.push_back('\n');
  const auto saved = atomic_file::save_text(index_path(project), text);
  if(!saved) { error = saved.message; return false; }
  return true;
}

bool path_has_parent_component(const std::filesystem::path &path)
{
  return std::any_of(path.begin(), path.end(), [](const auto &part) { return part == ".."; });
}

bool path_within_notes(const project::ProjectInfo &project, const std::filesystem::path &candidate,
                       bool reject_absolute, std::filesystem::path &canonical_out)
{
  if(candidate.empty() || (reject_absolute && candidate.is_absolute()) || path_has_parent_component(candidate)) return false;
  std::error_code ec;
  const auto root = std::filesystem::weakly_canonical(project.notes, ec);
  if(ec) return false;
  const auto absolute = candidate.is_absolute() ? candidate : project.root / candidate;
  const auto canonical = std::filesystem::weakly_canonical(absolute, ec);
  if(ec) return false;
  const auto relative = canonical.lexically_relative(root);
  if(relative.empty() || relative == "." || path_has_parent_component(relative)) return false;
  canonical_out = canonical;
  return true;
}

std::filesystem::path note_path(const project::ProjectInfo &project, const Json &note)
{
  std::filesystem::path path = note.value("path", std::string{});
  if(path.empty()) return {};
  std::filesystem::path checked;
  if(!path_within_notes(project, path, false, checked)) return {};
  return checked;
}

bool select_note(const project::ProjectInfo &project, const Json &args,
                 IndexData &index, std::size_t &folder_idx, std::size_t &note_idx,
                 std::string &error)
{
  const Json &folders = index.document["folders"];
  const std::string id = args.value("id", std::string{});
  const std::string path = args.value("path", std::string{});
  const std::string name = args.value("name", args.value("title", std::string{}));
  const std::filesystem::path requested_path(path);
  std::filesystem::path requested_canonical;
  if(!path.empty() && !path_within_notes(project, requested_path, true, requested_canonical))
  {
    error = "absolute, parent, or escaping paths are not allowed";
    return false;
  }
  for(std::size_t fi = 0; fi < folders.size(); ++fi)
  {
    const Json &notes = folders[fi].value("notes", Json::array());
    if(!notes.is_array()) continue;
    for(std::size_t ni = 0; ni < notes.size(); ++ni)
    {
      const Json &note = notes[ni];
      const bool match = (!id.empty() && note.value("id", std::string{}) == id) ||
                         (!path.empty() && (note.value("path", std::string{}) == path ||
                                            note_path(project, note) == requested_canonical)) ||
                         (!name.empty() && (note.value("title", std::string{}) == name ||
                                            note.value("name", std::string{}) == name));
      if(match) { folder_idx = fi; note_idx = ni; return true; }
    }
  }
  error = "note was not found; provide id, path, or name";
  return false;
}

Json note_summary(const project::ProjectInfo &project, const Json &note)
{
  Json result = note;
  const auto path = note_path(project, note);
  if(!path.empty()) result["path"] = std::filesystem::relative(path, project.root).generic_string();
  return result;
}

std::vector<std::tuple<std::size_t, std::size_t, std::string>> headings(std::string_view text)
{
  std::vector<std::tuple<std::size_t, std::size_t, std::string>> result;
  std::size_t start = 0;
  while(start < text.size())
  {
    const std::size_t end = text.find('\n', start);
    const std::size_t next = end == std::string_view::npos ? text.size() : end + 1U;
    int level = 0; std::string_view title;
    if(parse_heading_line(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start), level, title))
      result.emplace_back(start, static_cast<std::size_t>(level), std::string(title));
    start = next;
  }
  return result;
}

Json header_json(std::size_t offset, std::size_t level, std::string title)
{
  return Json{{"offset", offset}, {"level", level}, {"title", std::move(title)}};
}

} // namespace

std::string Response::serialize() const
{
  Json envelope;
  envelope["success"] = ok;
  envelope["ok"] = ok;
  if(ok) envelope["result"] = body;
  else if(body.contains("error")) envelope["error"] = body.at("error");
  else envelope["error"] = body;
  return envelope.dump();
}

Api::Api(project::ProjectInfo project) : project_(std::move(project)) {}

void Api::set_variable_adapter(VariableAdapter adapter)
{
  variable_adapter_ = std::move(adapter);
}

Json Api::capabilities() const
{
  static constexpr std::string_view names[] = {
      "app.capabilities", "note.list", "note.get", "note.create",
      "note.header.list", "note.header.get", "note.header.create",
      "note.color.set", "note.variable.get", "note.variable.set"};
  Json commands = Json::array();
  for(const auto name : names) commands.push_back(Json{{"name", name}});
  return Json{{"protocol", protocol_version}, {"commands", std::move(commands)}};
}

Response Api::execute(const Json &request)
{
  if(!request.is_object()) return failure("invalid_request", "request must be a JSON object");
  const std::string command = request.value("command", request.value("method", std::string{}));
  if(command.empty()) return failure("invalid_request", "missing command");
  const Json args = request.contains("args") ? request.at("args") :
                    request.value("arguments", Json::object());
  if(!args.is_object()) return failure("invalid_request", "args must be an object");
  if(command == "app.capabilities") return success(capabilities());

  IndexData index; std::string error;
  if(!load_index(project_, index, error)) return failure("index_error", error);
  if(command == "note.list")
  {
    Json result = Json::array();
    const std::string folder_filter = args.value("folder", std::string{});
    for(const auto &folder : index.document["folders"])
    {
      if(!folder_filter.empty() && folder.value("name", std::string{}) != folder_filter) continue;
      for(const auto &note : folder.value("notes", Json::array())) result.push_back(note_summary(project_, note));
    }
    return success(Json{{"notes", std::move(result)}});
  }

  const bool is_create = command == "note.create";
  std::size_t fi = 0, ni = 0;
  if(!is_create && !select_note(project_, args, index, fi, ni, error)) return failure("not_found", error);

  if(command == "note.get")
  {
    const Json &note = index.document["folders"][fi]["notes"][ni];
    const auto path = note_path(project_, note);
    const auto loaded = atomic_file::read_text(path);
    if(!loaded) return failure("io_error", loaded.message);
    return success(Json{{"note", note_summary(project_, note)}, {"content", loaded.snapshot.content}});
  }
  if(command == "note.create")
  {
    std::string title = args.value("title", args.value("name", std::string{"Note"}));
    std::vector<std::string> titles;
    std::string folder_name = args.value("folder", std::string{"General"});
    const auto invalid_text = [](std::string_view value) {
      return value.find('\r') != std::string_view::npos || value.find('\n') != std::string_view::npos;
    };
    const std::filesystem::path folder_path(folder_name);
    const std::filesystem::path title_path(title);
    if(invalid_text(title) || invalid_text(folder_name) || folder_path.is_absolute() ||
       title_path.is_absolute() || path_has_parent_component(folder_path) ||
       path_has_parent_component(title_path))
      return failure("invalid_args", "folder and title contain an unsafe path or line ending");
    std::size_t target = index.document["folders"].size();
    for(std::size_t i = 0; i < index.document["folders"].size(); ++i)
      if(index.document["folders"][i].value("name", std::string{}) == folder_name) target = i;
    if(target == index.document["folders"].size())
    {
      index.document["folders"].push_back(Json{{"name", folder_name}, {"notes", Json::array()}});
    }
    for(const auto &note : index.document["folders"][target].value("notes", Json::array()))
      titles.push_back(note.value("title", std::string{}));
    title = note_storage::make_unique_note_title(titles, title);
    const auto relative = note_storage::make_note_path(project_.notes, folder_name, title);
    std::filesystem::path absolute;
    if(!path_within_notes(project_, relative, false, absolute))
      return failure("invalid_args", "note path escapes the project notes root");
    std::error_code directory_error;
    std::filesystem::create_directories(absolute.parent_path(), directory_error);
    if(directory_error) return failure("io_error", directory_error.message());
    const std::string content = args.value("content", std::string{});
    const auto saved = atomic_file::save_text(absolute, content);
    if(!saved) return failure("io_error", saved.message);
    Json note{{"id", relative.lexically_relative(project_.notes).generic_string()},
              {"title", title}, {"path", absolute.lexically_relative(project_.root).generic_string()},
              {"content_fingerprint", note_index::content_fingerprint(content)}};
    index.document["folders"][target]["notes"].push_back(note);
    if(!save_index(project_, index.document, error)) return failure("io_error", error);
    return success(Json{{"note", note_summary(project_, note)}});
  }

  const Json &note = index.document["folders"][fi]["notes"][ni];
  const auto path = note_path(project_, note);
  const auto loaded = atomic_file::read_text(path);
  if(!loaded) return failure("io_error", loaded.message);
  const std::string text = loaded.snapshot.content;
  if(command == "note.variable.get" || command == "note.variable.set")
  {
    if(!variable_adapter_.get || (command == "note.variable.set" && !variable_adapter_.set))
      return failure("adapter_unavailable", "variable adapter is not registered by the GUI");
    const std::string name = args.value("name", args.value("variable", std::string{}));
    if(name.empty()) return failure("invalid_args", "variable name is required");
    if(command == "note.variable.get")
    {
      const auto result = variable_adapter_.get(path, text, name);
      if(!result.success) return failure("variable_error", result.error);
      return success(Json{{"name", name}, {"value", result.value}});
    }
    std::string updated = text;
    const auto result = variable_adapter_.set(path, updated, name, args.value("value", Json{}));
    if(!result.success) return failure("variable_error", result.error);
    if(updated != text)
    {
      const auto saved = atomic_file::save_text(path, updated);
      if(!saved) return failure("io_error", saved.message);
      index.document["folders"][fi]["notes"][ni]["content_fingerprint"] =
          note_index::content_fingerprint(updated);
      if(!save_index(project_, index.document, error)) return failure("io_error", error);
    }
    return success(Json{{"name", name}, {"value", result.value}});
  }
  const auto hs = headings(text);
  if(command == "note.header.list")
  {
    Json result = Json::array();
    for(const auto &h : hs) result.push_back(header_json(std::get<0>(h), std::get<1>(h), std::get<2>(h)));
    return success(Json{{"headers", std::move(result)}, {"note", note_summary(project_, note)}});
  }
  if(command == "note.header.get")
  {
    const std::string wanted = args.value("title", args.value("name", std::string{}));
    const auto found = std::find_if(hs.begin(), hs.end(), [&](const auto &h) { return std::get<2>(h) == wanted; });
    if(found == hs.end()) return failure("not_found", "header was not found");
    return success(Json{{"header", header_json(std::get<0>(*found), std::get<1>(*found), std::get<2>(*found))}});
  }
  if(command == "note.header.create")
  {
    const std::string title = args.value("title", std::string{});
    if(title.empty() || title.find('\r') != std::string::npos || title.find('\n') != std::string::npos)
      return failure("invalid_args", "header title must be a single non-empty line");
    const std::string parent = args.value("parent", std::string{});
    if(parent.find('\r') != std::string::npos || parent.find('\n') != std::string::npos)
      return failure("invalid_args", "parent header must be a single line");
    std::size_t insert = text.size();
    if(args.contains("level") && !args["level"].is_number_integer())
      return failure("invalid_args", "header level must be an integer");
    int level = args.value("level", 1);
    if(level < 1 || level > 6) return failure("invalid_args", "header level must be between 1 and 6");
    if(!parent.empty())
    {
      const auto found = std::find_if(hs.begin(), hs.end(), [&](const auto &h) { return std::get<2>(h) == parent; });
      if(found == hs.end()) return failure("not_found", "parent header was not found");
      level = static_cast<int>(std::get<1>(*found)) + 1;
      for(const auto &candidate : hs)
        if(std::get<0>(candidate) > std::get<0>(*found) && std::get<1>(candidate) <= std::get<1>(*found)) { insert = std::get<0>(candidate); break; }
    }
    if(level < 1 || level > 6) return failure("invalid_args", "parent heading would exceed level 6");
    std::string updated = text;
    if(insert != 0 && updated[insert - 1] != '\n') updated.insert(insert++, 1, '\n');
    updated.insert(insert, std::string(static_cast<std::size_t>(level), '#') + " " + title + "\n\n");
    const auto saved = atomic_file::save_text(path, updated);
    if(!saved) return failure("io_error", saved.message);
    index.document["folders"][fi]["notes"][ni]["content_fingerprint"] =
        note_index::content_fingerprint(updated);
    if(!save_index(project_, index.document, error)) return failure("io_error", error);
    return success(Json{{"header", Json{{"level", level}, {"title", title}}}});
  }
  if(command == "note.color.set")
  {
    for(const char *arg : {"r", "g", "b"})
      if(args.contains(arg) && (!args[arg].is_number_integer() || args[arg].get<int>() < 0 || args[arg].get<int>() > 255))
        return failure("invalid_args", "color channels must be integers in the range 0..255");
    const auto set_color = [&](const char *key, const char *arg) {
      if(args.contains(arg)) index.document["folders"][fi]["notes"][ni][key] = args[arg];
    };
    set_color("color_r", "r"); set_color("color_g", "g"); set_color("color_b", "b");
    index.document["folders"][fi]["notes"][ni]["use_custom_color"] = true;
    if(!save_index(project_, index.document, error)) return failure("io_error", error);
    return success(Json{{"note", note_summary(project_, index.document["folders"][fi]["notes"][ni])}});
  }
  return failure("unknown_command", "unknown command: " + command);
}

std::string endpoint_for_project(const project::ProjectInfo &project)
{
  std::error_code ec;
  const auto canonical_root = std::filesystem::weakly_canonical(project.root, ec);
  const auto identity_path = ec ? project.root.lexically_normal() : canonical_root;
  const std::string identity = note_index::content_fingerprint(identity_path.generic_string()).substr(0, 48);
#ifdef _WIN32
  return "\\\\.\\pipe\\notepp-" + identity;
#else
  return "/tmp/notepp-" + std::to_string(static_cast<unsigned long long>(::getuid())) + "-" + identity + ".sock";
#endif
}
} // namespace notepp::command_api
