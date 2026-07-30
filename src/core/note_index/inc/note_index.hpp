#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace notepp::note_index
{
constexpr int current_schema_version = 2;

enum class DocumentState
{
  missing,
  supported,
  malformed,
  future_schema
};

DocumentState validate_document(const nlohmann::json &document, bool existed) noexcept;

struct NoteRecord
{
  std::string id;
  std::string title = "Note";
  std::string path;
  std::string font_path;
  std::string content_fingerprint;
  int x = 0;
  int y = 0;
  int width = 520;
  int height = 260;
  int dock_id = 0;
  int color_r = 0;
  int color_g = 0;
  int color_b = 0;
  float font_size = 0.0F;
  bool has_layout = false;
  bool hidden = false;
  bool always_on_top = false;
  bool use_custom_color = false;
};

struct FolderRecord
{
  std::string name = "General";
  std::vector<NoteRecord> notes;
  std::vector<std::string> images;
  bool layout_locked = false;
  bool detached_note_windows = false;
  bool dockers_enabled = false;
  bool drawings_visible = true;
  bool grid_visible = false;
};

struct Document
{
  int schema_version = 1;
  int active_folder = 0;
  int active_note = 0;
  std::string language;
  std::vector<FolderRecord> folders;
  bool folder_view = false;
  bool layout_locked = false;
  bool detached_note_windows = false;
  bool dockers_enabled = false;
};

// Decode only canonical, validated fields. Unknown extension objects are never
// searched recursively and therefore cannot shadow canonical keys.
std::optional<Document> decode_document(const nlohmann::json &document) noexcept;

// Stable SHA-256 content fingerprint persisted with note metadata. It is used
// only as conservative rename evidence after unique matching; an available
// source snapshot is additionally verified byte-for-byte.
std::string content_fingerprint(std::string_view content);
constexpr bool strong_content_fingerprint(std::string_view fingerprint) noexcept
{
  return fingerprint.size() == 64U;
}
constexpr bool unique_rename_evidence(int missing_matches, int disk_matches) noexcept
{
  return missing_matches == 1 && disk_matches == 1;
}

// Overlay a fully serialized current index on an earlier index while retaining
// fields unknown to this version. Folders match by name and notes by stable ID.
// Positional fallback is limited to legacy records that genuinely lack identity.
nlohmann::json merge_unknown_fields(const nlohmann::json &source,
                                    const nlohmann::json &current);

// Read the established camel-case key while accepting the briefly emitted
// snake-case spelling for compatibility. The established key wins when both
// are present.
int read_schema_version(const nlohmann::json &document,
                        int default_version = 1) noexcept;

// Upgrade a fully migrated legacy index, but never downgrade a newer schema
// because one entry was invalid or unresolved.
int schema_after_path_migration(int loaded_schema, bool migration_failed) noexcept;
} // namespace notepp::note_index
