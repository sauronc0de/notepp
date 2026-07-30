#include "note_index.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace notepp::note_index
{
namespace
{
using Json = nlohmann::json;

Json matching_folder(const Json &folders, const Json &current_folder, std::size_t index)
{
  if(!folders.is_array()) return Json::object();
  const std::string name = current_folder.value("name", std::string{});
  for(const Json &folder : folders)
  {
    if(folder.is_object() && folder.value("name", std::string{}) == name)
      return folder;
  }
  if(index < folders.size() && folders[index].is_object()) return folders[index];
  return Json::object();
}

Json matching_note(const Json &notes, const Json &current_note, std::size_t index)
{
  if(!notes.is_array()) return Json::object();
  const std::string id = current_note.value("id", std::string{});
  if(!id.empty())
  {
    for(const Json &note : notes)
    {
      if(note.is_object() && note.value("id", std::string{}) == id)
        return note;
    }
  }
  if(index < notes.size() && notes[index].is_object()) return notes[index];
  return Json::object();
}
} // namespace

int read_schema_version(const nlohmann::json &document,
                        int default_version) noexcept
{
  if(!document.is_object()) return default_version;

  const auto read_key = [&](const char *key) -> int {
    const auto found = document.find(key);
    if(found == document.end() || !found->is_number_integer())
      return default_version;
    try
    {
      return found->get<int>();
    }
    catch(...)
    {
      return default_version;
    }
  };

  if(document.contains("schemaVersion")) return read_key("schemaVersion");
  return read_key("schema_version");
}

int schema_after_path_migration(int loaded_schema, bool migration_failed) noexcept
{
  if(loaded_schema >= 2 || migration_failed) return loaded_schema;
  return 2;
}

nlohmann::json merge_unknown_fields(const nlohmann::json &source,
                                    const nlohmann::json &current)
{
  Json result = source.is_object() ? source : Json::object();
  if(!current.is_object()) return result;

  for(const auto &[key, value] : current.items())
  {
    if(key != "folders") result[key] = value;
  }

  const Json source_folders = result.value("folders", Json::array());
  const Json current_folders = current.value("folders", Json::array());
  if(!current_folders.is_array())
  {
    result["folders"] = current_folders;
    return result;
  }

  Json merged_folders = Json::array();
  for(std::size_t folder_index = 0; folder_index < current_folders.size(); ++folder_index)
  {
    const Json &current_folder = current_folders[folder_index];
    if(!current_folder.is_object())
    {
      merged_folders.push_back(current_folder);
      continue;
    }

    Json merged_folder = matching_folder(source_folders, current_folder, folder_index);
    const Json source_notes = merged_folder.value("notes", Json::array());
    for(const auto &[key, value] : current_folder.items())
    {
      if(key != "notes") merged_folder[key] = value;
    }

    const Json current_notes = current_folder.value("notes", Json::array());
    if(current_notes.is_array())
    {
      Json merged_notes = Json::array();
      for(std::size_t note_index = 0; note_index < current_notes.size(); ++note_index)
      {
        const Json &current_note = current_notes[note_index];
        if(!current_note.is_object())
        {
          merged_notes.push_back(current_note);
          continue;
        }
        Json merged_note = matching_note(source_notes, current_note, note_index);
        for(const auto &[key, value] : current_note.items())
          merged_note[key] = value;
        merged_notes.push_back(std::move(merged_note));
      }
      merged_folder["notes"] = std::move(merged_notes);
    }
    else
    {
      merged_folder["notes"] = current_notes;
    }
    merged_folders.push_back(std::move(merged_folder));
  }
  result["folders"] = std::move(merged_folders);
  return result;
}
} // namespace notepp::note_index
