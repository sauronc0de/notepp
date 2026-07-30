#include "layout_profile_state.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace notepp::layout_profile_state
{
namespace
{
using Json = nlohmann::json;

const Json *find_by_id(const Json &items, const char *key, const std::string &id)
{
  if(!items.is_array() || id.empty()) return nullptr;
  for(const Json &item : items)
    if(item.is_object() && item.value(key, std::string{}) == id) return &item;
  return nullptr;
}

Json merge_layouts(const Json &source, const Json &current)
{
  if(!current.is_array()) return current;
  Json result = Json::array();
  for(const Json &layout : current)
  {
    if(!layout.is_object())
    {
      result.push_back(layout);
      continue;
    }
    const std::string id = layout.value("note_id", std::string{});
    const Json *matched = find_by_id(source, "note_id", id);
    Json merged = matched != nullptr ? *matched : Json::object();
    for(const auto &[key, value] : layout.items()) merged[key] = value;
    result.push_back(std::move(merged));
  }
  return result;
}
} // namespace

DocumentState validate_document(const nlohmann::json &document, bool existed) noexcept
{
  if(!existed) return DocumentState::missing;
  if(!document.is_object()) return DocumentState::malformed;
  const auto profiles = document.find("profiles");
  if(profiles == document.end() || !profiles->is_array()) return DocumentState::malformed;
  for(const auto &profile : *profiles)
  {
    if(!profile.is_object()) return DocumentState::malformed;
    const auto id = profile.find("id");
    if(id == profile.end() || !id->is_string()) return DocumentState::malformed;
    const auto layouts = profile.find("note_layouts");
    if(layouts != profile.end())
    {
      if(!layouts->is_array()) return DocumentState::malformed;
      for(const auto &layout : *layouts)
      {
        if(!layout.is_object()) return DocumentState::malformed;
        const auto note_id = layout.find("note_id");
        if(note_id == layout.end() || !note_id->is_string())
          return DocumentState::malformed;
      }
    }
  }
  const auto schema = document.find("schemaVersion");
  if(schema != document.end())
  {
    if(!schema->is_number_integer()) return DocumentState::malformed;
    try
    {
      if(schema->is_number_unsigned())
      {
        if(schema->get<std::uint64_t>() >
           static_cast<std::uint64_t>(current_schema_version))
          return DocumentState::future_schema;
      }
      else if(schema->get<std::int64_t>() > current_schema_version)
        return DocumentState::future_schema;
    }
    catch(...)
    {
      return DocumentState::future_schema;
    }
  }
  return DocumentState::supported;
}

nlohmann::json merge_unknown_fields(const nlohmann::json &source,
                                    const nlohmann::json &current)
{
  using Json = nlohmann::json;
  Json result = source.is_object() ? source : Json::object();
  if(!current.is_object()) return result;
  for(const auto &[key, value] : current.items())
    if(key != "profiles") result[key] = value;

  const Json source_profiles = result.value("profiles", Json::array());
  const Json current_profiles = current.value("profiles", Json::array());
  if(!current_profiles.is_array())
  {
    result["profiles"] = current_profiles;
    return result;
  }

  Json profiles = Json::array();
  for(const Json &profile : current_profiles)
  {
    if(!profile.is_object())
    {
      profiles.push_back(profile);
      continue;
    }
    const std::string id = profile.value("id", std::string{});
    const Json *matched = find_by_id(source_profiles, "id", id);
    Json merged = matched != nullptr ? *matched : Json::object();
    const Json source_layouts = merged.value("note_layouts", Json::array());
    for(const auto &[key, value] : profile.items())
      if(key != "note_layouts") merged[key] = value;
    merged["note_layouts"] = merge_layouts(source_layouts,
                                           profile.value("note_layouts", Json::array()));
    profiles.push_back(std::move(merged));
  }
  result["profiles"] = std::move(profiles);
  return result;
}
} // namespace notepp::layout_profile_state
