#pragma once

#include <nlohmann/json_fwd.hpp>

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
nlohmann::json merge_unknown_fields(const nlohmann::json &source,
                                    const nlohmann::json &current);
} // namespace notepp::layout_profile_state
