#include "note_index.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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
  if(name.empty() && index < folders.size() && folders[index].is_object() &&
     folders[index].value("name", std::string{}).empty())
    return folders[index];
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
  if(index < notes.size() && notes[index].is_object() &&
     notes[index].value("id", std::string{}).empty())
    return notes[index];
  return Json::object();
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

bool index_value(const Json &value)
{
  return integer_in_int_range(value) && value.get<int>() >= -1;
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

bool finite_nonnegative_number(const Json &value)
{
  if(!value.is_number()) return false;
  try
  {
    const double number = value.get<double>();
    return std::isfinite(number) && number >= 0.0 && number <= 1000.0;
  }
  catch(...)
  {
    return false;
  }
}

bool valid_fingerprint(const Json &value)
{
  if(!value.is_string()) return false;
  const std::string &text = value.get_ref<const std::string &>();
  if(text.size() != 16U && text.size() != 64U) return false;
  return std::all_of(text.begin(), text.end(), [](unsigned char character) {
    return std::isxdigit(character) != 0;
  });
}

constexpr std::array<std::uint32_t, 64> kSha256Constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

void sha256_block(std::array<std::uint32_t, 8> &state, const std::uint8_t *block)
{
  std::array<std::uint32_t, 64> words{};
  for(std::size_t index = 0; index < 16U; ++index)
  {
    const std::size_t offset = index * 4U;
    words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                   (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                   static_cast<std::uint32_t>(block[offset + 3U]);
  }
  for(std::size_t index = 16U; index < words.size(); ++index)
  {
    const std::uint32_t s0 = std::rotr(words[index - 15U], 7) ^
                             std::rotr(words[index - 15U], 18) ^
                             (words[index - 15U] >> 3U);
    const std::uint32_t s1 = std::rotr(words[index - 2U], 17) ^
                             std::rotr(words[index - 2U], 19) ^
                             (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
  }

  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];
  std::uint32_t e = state[4];
  std::uint32_t f = state[5];
  std::uint32_t g = state[6];
  std::uint32_t h = state[7];
  for(std::size_t index = 0; index < words.size(); ++index)
  {
    const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const std::uint32_t choose = (e & f) ^ (~e & g);
    const std::uint32_t temp1 = h + sum1 + choose + kSha256Constants[index] + words[index];
    const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}
} // namespace

DocumentState validate_document(const nlohmann::json &document, bool existed) noexcept
{
  if(!existed) return DocumentState::missing;
  if(!document.is_object()) return DocumentState::malformed;
  for(const char *key : {"schemaVersion", "schema_version"})
  {
    const auto value = document.find(key);
    if(value == document.end()) continue;
    if(!value->is_number_integer()) return DocumentState::malformed;
    try
    {
      if(value->is_number_unsigned())
      {
        const std::uint64_t schema = value->get<std::uint64_t>();
        if(schema == 0U) return DocumentState::malformed;
        if(schema > static_cast<std::uint64_t>(current_schema_version))
          return DocumentState::future_schema;
      }
      else
      {
        const std::int64_t schema = value->get<std::int64_t>();
        if(schema < 1) return DocumentState::malformed;
        if(schema > current_schema_version) return DocumentState::future_schema;
      }
    }
    catch(...)
    {
      return DocumentState::malformed;
    }
  }
  if(!optional_field(document, "active_folder", index_value) ||
     !optional_field(document, "active_note", index_value) ||
     !optional_field(document, "folder_view", [](const Json &value) { return value.is_boolean(); }) ||
     !optional_field(document, "layout_locked", [](const Json &value) { return value.is_boolean(); }) ||
     !optional_field(document, "detached_note_windows", [](const Json &value) { return value.is_boolean(); }) ||
     !optional_field(document, "dockers_enabled", [](const Json &value) { return value.is_boolean(); }) ||
     !optional_field(document, "language", [](const Json &value) { return value.is_string(); }))
    return DocumentState::malformed;

  const auto folders = document.find("folders");
  if(folders == document.end() || !folders->is_array()) return DocumentState::malformed;
  for(const auto &folder : *folders)
  {
    if(!folder.is_object() ||
       !optional_field(folder, "name", [](const Json &value) { return value.is_string(); }) ||
       !optional_field(folder, "layout_locked", [](const Json &value) { return value.is_boolean(); }) ||
       !optional_field(folder, "detached_note_windows", [](const Json &value) { return value.is_boolean(); }) ||
       !optional_field(folder, "dockers_enabled", [](const Json &value) { return value.is_boolean(); }) ||
       !optional_field(folder, "drawings_visible", [](const Json &value) { return value.is_boolean(); }) ||
       !optional_field(folder, "grid_visible", [](const Json &value) { return value.is_boolean(); }))
      return DocumentState::malformed;
    const auto notes = folder.find("notes");
    if(notes == folder.end() || !notes->is_array()) return DocumentState::malformed;
    for(const auto &note : *notes)
    {
      if(!note.is_object()) return DocumentState::malformed;
      for(const char *key : {"id", "title", "path", "font_path"})
        if(!optional_field(note, key, [](const Json &value) { return value.is_string(); }))
          return DocumentState::malformed;
      if(!optional_field(note, "content_fingerprint", valid_fingerprint))
        return DocumentState::malformed;
      for(const char *key : {"x", "y"})
        if(!optional_field(note, key, integer_in_int_range)) return DocumentState::malformed;
      for(const char *key : {"w", "h"})
        if(!optional_field(note, key, positive_int)) return DocumentState::malformed;
      if(!optional_field(note, "dock_id", nonnegative_int)) return DocumentState::malformed;
      for(const char *key : {"color_r", "color_g", "color_b"})
      {
        if(!optional_field(note, key, [](const Json &value) {
             return integer_in_int_range(value) && value.get<int>() >= 0 && value.get<int>() <= 255;
           }))
          return DocumentState::malformed;
      }
      for(const char *key : {"has_layout", "hidden", "always_on_top", "use_custom_color"})
        if(!optional_field(note, key, [](const Json &value) { return value.is_boolean(); }))
          return DocumentState::malformed;
      if(!optional_field(note, "font_size", finite_nonnegative_number))
        return DocumentState::malformed;
    }
    const auto images = folder.find("images");
    if(images != folder.end())
    {
      if(!images->is_array() ||
         !std::all_of(images->begin(), images->end(), [](const Json &image) { return image.is_string(); }))
        return DocumentState::malformed;
    }
  }
  const int schema = read_schema_version(document);
  if(schema > current_schema_version) return DocumentState::future_schema;
  return DocumentState::supported;
}

std::optional<Document> decode_document(const nlohmann::json &document) noexcept
{
  if(validate_document(document, true) != DocumentState::supported)
    return std::nullopt;

  try
  {
    Document decoded;
    decoded.schema_version = read_schema_version(document);
    decoded.active_folder = document.value("active_folder", 0);
    decoded.active_note = document.value("active_note", 0);
    decoded.folder_view = document.value("folder_view", false);
    decoded.layout_locked = document.value("layout_locked", false);
    decoded.detached_note_windows = document.value("detached_note_windows", false);
    decoded.dockers_enabled = document.value("dockers_enabled", false);
    decoded.language = document.value("language", std::string{});

    for(const Json &folder_value : document.at("folders"))
    {
      FolderRecord folder;
      folder.name = folder_value.value("name", std::string{"General"});
      if(folder.name.empty()) folder.name = "General";
      folder.layout_locked = folder_value.value("layout_locked", decoded.layout_locked);
      folder.detached_note_windows = folder_value.value(
          "detached_note_windows", decoded.detached_note_windows);
      folder.dockers_enabled = folder_value.value("dockers_enabled", decoded.dockers_enabled);
      folder.drawings_visible = folder_value.value("drawings_visible", true);
      folder.grid_visible = folder_value.value("grid_visible", false);

      for(const Json &note_value : folder_value.at("notes"))
      {
        NoteRecord note;
        note.id = note_value.value("id", std::string{});
        note.title = note_value.value("title", std::string{"Note"});
        if(note.title.empty()) note.title = "Note";
        note.path = note_value.value("path", std::string{});
        note.font_path = note_value.value("font_path", std::string{});
        note.content_fingerprint = note_value.value("content_fingerprint", std::string{});
        note.x = note_value.value("x", 0);
        note.y = note_value.value("y", 0);
        note.width = note_value.value("w", 520);
        note.height = note_value.value("h", 260);
        note.dock_id = note_value.value("dock_id", 0);
        note.color_r = note_value.value("color_r", 0);
        note.color_g = note_value.value("color_g", 0);
        note.color_b = note_value.value("color_b", 0);
        note.font_size = static_cast<float>(note_value.value("font_size", 0.0));
        note.has_layout = note_value.value("has_layout", false);
        note.hidden = note_value.value("hidden", false);
        note.always_on_top = note_value.value("always_on_top", false);
        note.use_custom_color = note_value.value("use_custom_color", false);
        folder.notes.push_back(std::move(note));
      }

      const auto images = folder_value.find("images");
      if(images != folder_value.end())
        for(const Json &image : *images) folder.images.push_back(image.get<std::string>());
      decoded.folders.push_back(std::move(folder));
    }
    return decoded;
  }
  catch(...)
  {
    return std::nullopt;
  }
}

std::string content_fingerprint(std::string_view content)
{
  std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                     0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                     0x1f83d9abU, 0x5be0cd19U};
  std::vector<std::uint8_t> bytes;
  bytes.reserve(content.size() + 72U);
  for(const unsigned char byte : content) bytes.push_back(byte);
  bytes.push_back(0x80U);
  while(bytes.size() % 64U != 56U) bytes.push_back(0U);
  const std::uint64_t bit_length = static_cast<std::uint64_t>(content.size()) * 8U;
  for(int shift = 56; shift >= 0; shift -= 8)
    bytes.push_back(static_cast<std::uint8_t>((bit_length >> static_cast<unsigned int>(shift)) & 0xffU));
  for(std::size_t offset = 0; offset < bytes.size(); offset += 64U)
    sha256_block(state, bytes.data() + offset);

  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for(const std::uint32_t word : state) out << std::setw(8) << word;
  return out.str();
}

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
      if(found->is_number_unsigned())
      {
        const std::uint64_t value = found->get<std::uint64_t>();
        if(value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
          return current_schema_version + 1;
        return static_cast<int>(value);
      }
      const std::int64_t value = found->get<std::int64_t>();
      if(value < 1) return default_version;
      if(value > std::numeric_limits<int>::max()) return current_schema_version + 1;
      return static_cast<int>(value);
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
