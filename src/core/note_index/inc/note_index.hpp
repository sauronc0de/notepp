#pragma once

#include <nlohmann/json_fwd.hpp>

namespace notepp::note_index
{
// Overlay a fully serialized current index on an earlier index while retaining
// fields unknown to this version. Folders match by name and notes by stable ID,
// with an index fallback for the one-time migration that creates missing IDs.
nlohmann::json merge_unknown_fields(const nlohmann::json &source,
                                    const nlohmann::json &current);

// Upgrade a fully migrated legacy index, but never downgrade a newer schema
// because one entry was invalid or unresolved.
int schema_after_path_migration(int loaded_schema, bool migration_failed) noexcept;
} // namespace notepp::note_index
