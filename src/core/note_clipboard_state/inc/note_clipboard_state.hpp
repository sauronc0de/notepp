#pragma once

#include <nlohmann/json.hpp>

#include <vector>

namespace notepp::note_clipboard_state
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
std::vector<nlohmann::json> read_items(const nlohmann::json &document);
nlohmann::json make_document(const std::vector<nlohmann::json> &items);
} // namespace notepp::note_clipboard_state
