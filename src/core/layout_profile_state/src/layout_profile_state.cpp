#include "layout_profile_state.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

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

bool integer_in_int_range(const Json &value)
{
  if(!value.is_number_integer()) return false;
  try
  {
    if(value.is_number_unsigned())
      return value.get<std::uint64_t>() <=
             static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    const std::int64_t number = value.get<std::int64_t>();
    return number >= std::numeric_limits<int>::min() &&
           number <= std::numeric_limits<int>::max();
  }
  catch(...)
  {
    return false;
  }
}

bool positive_int(const Json &value)
{
  return integer_in_int_range(value) && value.get<int>() > 0;
}

bool nonnegative_int(const Json &value)
{
  return integer_in_int_range(value) && value.get<int>() >= 0;
}

template <typename Predicate>
bool optional_field(const Json &object, const char *key, Predicate predicate)
{
  const auto found = object.find(key);
  return found == object.end() || predicate(*found);
}
} // namespace

DocumentState validate_document(const nlohmann::json &document, bool existed) noexcept
{
  if(!existed) return DocumentState::missing;
  if(!document.is_object()) return DocumentState::malformed;
  for(const char *key : {"active_profile_id", "maximized_profile_id", "reduced_profile_id"})
    if(!optional_field(document, key, [](const Json &value) { return value.is_string(); }))
      return DocumentState::malformed;

  const auto profiles = document.find("profiles");
  if(profiles == document.end() || !profiles->is_array()) return DocumentState::malformed;
  for(const auto &profile : *profiles)
  {
    if(!profile.is_object()) return DocumentState::malformed;
    const auto id = profile.find("id");
    if(id == profile.end() || !id->is_string() || id->get_ref<const std::string &>().empty())
      return DocumentState::malformed;
    if(!optional_field(profile, "name", [](const Json &value) { return value.is_string(); }) ||
       !optional_field(profile, "window_maximized", [](const Json &value) { return value.is_boolean(); }))
      return DocumentState::malformed;
    for(const char *key : {"window_x", "window_y"})
      if(!optional_field(profile, key, integer_in_int_range)) return DocumentState::malformed;
    for(const char *key : {"window_w", "window_h"})
      if(!optional_field(profile, key, positive_int)) return DocumentState::malformed;

    const auto layouts = profile.find("note_layouts");
    if(layouts != profile.end())
    {
      if(!layouts->is_array()) return DocumentState::malformed;
      for(const auto &layout : *layouts)
      {
        if(!layout.is_object()) return DocumentState::malformed;
        const auto note_id = layout.find("note_id");
        if(note_id == layout.end() || !note_id->is_string() ||
           note_id->get_ref<const std::string &>().empty())
          return DocumentState::malformed;
        for(const char *key : {"x", "y"})
          if(!optional_field(layout, key, integer_in_int_range))
            return DocumentState::malformed;
        for(const char *key : {"w", "h"})
          if(!optional_field(layout, key, positive_int))
            return DocumentState::malformed;
        if(!optional_field(layout, "dock_id", nonnegative_int))
          return DocumentState::malformed;
        for(const char *key : {"hidden", "always_on_top", "has_layout"})
          if(!optional_field(layout, key, [](const Json &value) { return value.is_boolean(); }))
            return DocumentState::malformed;
      }
    }
  }
  const auto schema = document.find("schemaVersion");
  if(schema != document.end())
  {
    if(!integer_in_int_range(*schema)) return DocumentState::malformed;
    try
    {
      const int value = schema->get<int>();
      if(value < 1) return DocumentState::malformed;
      if(value > current_schema_version) return DocumentState::future_schema;
    }
    catch(...)
    {
      return DocumentState::malformed;
    }
  }
  return DocumentState::supported;
}

std::optional<Document> decode_document(const nlohmann::json &document) noexcept
{
  if(validate_document(document, true) != DocumentState::supported)
    return std::nullopt;

  try
  {
    Document decoded;
    decoded.active_profile_id = document.value("active_profile_id", std::string{});
    decoded.maximized_profile_id = document.value("maximized_profile_id", std::string{});
    decoded.reduced_profile_id = document.value("reduced_profile_id", std::string{});
    for(const Json &profile_value : document.at("profiles"))
    {
      ProfileRecord profile;
      profile.id = profile_value.at("id").get<std::string>();
      profile.name = profile_value.value("name", std::string{"Profile"});
      if(profile.name.empty()) profile.name = "Profile";
      profile.window_maximized = profile_value.value("window_maximized", true);
      profile.window_x = profile_value.value("window_x", -1);
      profile.window_y = profile_value.value("window_y", -1);
      profile.window_w = profile_value.value("window_w", 1100);
      profile.window_h = profile_value.value("window_h", 700);

      const auto layouts = profile_value.find("note_layouts");
      if(layouts != profile_value.end())
      {
        for(const Json &layout_value : *layouts)
        {
          NoteLayoutRecord layout;
          layout.x = layout_value.value("x", 0);
          layout.y = layout_value.value("y", 0);
          layout.width = layout_value.value("w", 520);
          layout.height = layout_value.value("h", 260);
          layout.dock_id = layout_value.value("dock_id", 0);
          layout.hidden = layout_value.value("hidden", false);
          layout.always_on_top = layout_value.value("always_on_top", false);
          layout.has_layout = layout_value.value("has_layout", false);
          profile.note_layouts.emplace(
              layout_value.at("note_id").get<std::string>(), layout);
        }
      }
      decoded.profiles.push_back(std::move(profile));
    }
    return decoded;
  }
  catch(...)
  {
    return std::nullopt;
  }
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
