#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>

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
