#include "note_clipboard_state.hpp"

#include <cstdint>

namespace notepp::note_clipboard_state
{
namespace
{
bool valid_item(const nlohmann::json &item)
{
  if(!item.is_object()) return false;
  for(const char *key : {"title", "content", "font_path"})
  {
    const auto value = item.find(key);
    if(value != item.end() && !value->is_string()) return false;
  }
  for(const char *key : {"font_size", "color_r", "color_g", "color_b", "w", "h"})
  {
    const auto value = item.find(key);
    if(value != item.end() && !value->is_number()) return false;
  }
  for(const char *key : {"use_custom_color", "has_layout", "always_on_top"})
  {
    const auto value = item.find(key);
    if(value != item.end() && !value->is_boolean()) return false;
  }
  return true;
}
} // namespace

DocumentState validate_document(const nlohmann::json &document, bool existed) noexcept
{
  if(!existed) return DocumentState::missing;
  if(!document.is_object()) return DocumentState::malformed;
  const auto schema = document.find("schemaVersion");
  if(schema != document.end())
  {
    if(!schema->is_number_integer()) return DocumentState::malformed;
    try
    {
      if(schema->is_number_unsigned())
      {
        if(schema->get<std::uint64_t>() > current_schema_version)
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
  const auto has_note = document.find("has_note");
  if(has_note != document.end() && !has_note->is_boolean()) return DocumentState::malformed;
  const auto items = document.find("items");
  if(items != document.end())
  {
    if(!items->is_array()) return DocumentState::malformed;
    for(const auto &item : *items)
      if(!valid_item(item)) return DocumentState::malformed;
  }
  else if(has_note != document.end() && has_note->get<bool>() && !valid_item(document))
    return DocumentState::malformed;
  return DocumentState::supported;
}

std::vector<nlohmann::json> read_items(const nlohmann::json &document)
{
  std::vector<nlohmann::json> result;
  if(!document.is_object()) return result;
  if(const auto items = document.find("items"); items != document.end() && items->is_array())
  {
    for(const auto &item : *items)
      if(item.is_object()) result.push_back(item);
    return result;
  }
  if(document.value("has_note", false)) result.push_back(document);
  return result;
}

nlohmann::json make_document(const std::vector<nlohmann::json> &items)
{
  nlohmann::json document = {{"schemaVersion", current_schema_version},
                             {"has_note", !items.empty()},
                             {"items", nlohmann::json::array()}};
  for(const auto &item : items)
    if(item.is_object()) document["items"].push_back(item);
  return document;
}
} // namespace notepp::note_clipboard_state
