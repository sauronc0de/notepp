#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace notepp::layout_profile_state
{
constexpr int current_schema_version = 1;

enum class DocumentState
{
  missing,
  supported,
  malformed,
  future_schema
};

DocumentState validate_document(const nlohmann::json &document, bool existed) noexcept;

struct NoteLayoutRecord
{
  int x = 0;
  int y = 0;
  int width = 520;
  int height = 260;
  int dock_id = 0;
  bool hidden = false;
  bool always_on_top = false;
  bool has_layout = false;
};

struct ProfileRecord
{
  std::string id;
  std::string name = "Profile";
  int window_x = -1;
  int window_y = -1;
  int window_w = 1100;
  int window_h = 700;
  bool window_maximized = true;
  std::unordered_map<std::string, NoteLayoutRecord> note_layouts;
};

struct Document
{
  std::string active_profile_id;
  std::string maximized_profile_id;
  std::string reduced_profile_id;
  std::vector<ProfileRecord> profiles;
};

// Decode only canonical, validated fields. Unknown extension objects are never
// searched recursively and therefore cannot shadow canonical keys.
std::optional<Document> decode_document(const nlohmann::json &document) noexcept;

nlohmann::json merge_unknown_fields(const nlohmann::json &source,
                                    const nlohmann::json &current);
} // namespace notepp::layout_profile_state
