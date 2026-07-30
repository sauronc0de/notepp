#include "note_index.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
using Json = nlohmann::json;
int failures = 0;

void expect(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

void test_preserves_unknown_fields_during_migration()
{
  const Json source = {
      {"schemaVersion", 1},
      {"pluginRoot", Json{{"enabled", true}}},
      {"folders", Json::array({Json{
                      {"name", "work"},
                      {"pluginFolder", 42},
                      {"notes", Json::array({Json{{"title", "plan"},
                                                  {"path", "/old/notes/work/plan.md"},
                                                  {"pluginNote", "keep"}}})}}})}};
  const Json current = {
      {"schemaVersion", 2},
      {"active_note", 0},
      {"folders", Json::array({Json{
                      {"name", "work"},
                      {"notes", Json::array({Json{{"id", "stable-id"},
                                                  {"title", "plan"},
                                                  {"path", "notes/work/plan.md"}}})},
                      {"images", Json::array()}}})}};

  const Json merged = notepp::note_index::merge_unknown_fields(source, current);
  expect(merged["schemaVersion"] == 2, "known root fields are updated");
  expect(merged["pluginRoot"]["enabled"] == true, "unknown root fields are preserved");
  expect(merged["folders"][0]["pluginFolder"] == 42, "unknown folder fields are preserved");
  expect(merged["folders"][0]["notes"][0]["pluginNote"] == "keep",
         "unknown note fields are preserved when a missing UUID is generated");
  expect(merged["folders"][0]["notes"][0]["id"] == "stable-id",
         "new stable note identity is written");
  expect(merged["folders"][0]["notes"][0]["path"] == "notes/work/plan.md",
         "portable note path replaces the legacy path");
}

void test_schema_migration_never_downgrades_v2()
{
  expect(notepp::note_index::schema_after_path_migration(2, true) == 2,
         "an invalid schema-v2 path cannot enable legacy fallback on the next launch");
  expect(notepp::note_index::schema_after_path_migration(1, true) == 1,
         "a partially migrated legacy index remains legacy until every path resolves");
  expect(notepp::note_index::schema_after_path_migration(1, false) == 2,
         "a complete legacy migration upgrades to portable schema v2");
}

void test_schema_reader_prefers_established_key_and_accepts_compatibility_key()
{
  expect(notepp::note_index::read_schema_version(Json{{"schemaVersion", 2}}) == 2,
         "the established camel-case schema key is read");
  expect(notepp::note_index::read_schema_version(Json{{"schema_version", 2}}) == 2,
         "the briefly emitted snake-case schema key remains readable");
  expect(notepp::note_index::read_schema_version(
             Json{{"schemaVersion", 3}, {"schema_version", 1}}) == 3,
         "the established key wins when both spellings are present");
  expect(notepp::note_index::read_schema_version(Json::object()) == 1,
         "a legacy index without a schema key defaults to version one");
}

void test_document_validation_and_fingerprint()
{
  expect(notepp::note_index::validate_document(Json{}, false) ==
             notepp::note_index::DocumentState::missing,
         "missing indexes are distinguished from malformed indexes");
  expect(notepp::note_index::validate_document(Json::parse("{", nullptr, false), true) ==
             notepp::note_index::DocumentState::malformed,
         "malformed indexes block reconstruction");
  expect(notepp::note_index::validate_document(Json{{"schemaVersion", 2}, {"folders", Json::array()}}, true) ==
             notepp::note_index::DocumentState::supported,
         "current indexes are writable");
  expect(notepp::note_index::validate_document(Json{{"schemaVersion", 3}, {"folders", Json::array()}}, true) ==
             notepp::note_index::DocumentState::future_schema,
         "future indexes are read-only");
  expect(notepp::note_index::content_fingerprint("same") ==
             notepp::note_index::content_fingerprint("same"),
         "fingerprints are stable");
  expect(notepp::note_index::content_fingerprint("same") !=
             notepp::note_index::content_fingerprint("different"),
         "different content has different rename evidence");
  expect(notepp::note_index::unique_rename_evidence(1, 1),
         "one missing note and one exact disk match retain identity");
  expect(!notepp::note_index::unique_rename_evidence(2, 1) &&
             !notepp::note_index::unique_rename_evidence(1, 2),
         "ambiguous rename evidence never selects a metadata winner");
}

void test_identity_fallback_is_legacy_only()
{
  const Json source = {{"folders", Json::array({Json{{"name", "old"},
                                                     {"pluginFolder", 1},
                                                     {"notes", Json::array({Json{{"id", "old-id"}, {"plugin", 2}}})}}})}};
  const Json current = {{"folders", Json::array({Json{{"name", "new"},
                                                      {"notes", Json::array({Json{{"id", "new-id"}}})}}})}};
  const Json merged = notepp::note_index::merge_unknown_fields(source, current);
  expect(!merged["folders"][0].contains("pluginFolder"),
         "position does not transfer folder fields across named identities");
  expect(!merged["folders"][0]["notes"][0].contains("plugin"),
         "position does not transfer note fields across stable identities");
}

void test_does_not_restore_removed_entities()
{
  const Json source = {{"folders", Json::array({Json{{"name", "removed"},
                                                     {"notes", Json::array({Json{{"id", "old"}}})}}})}};
  const Json current = {{"folders", Json::array()}};
  const Json merged = notepp::note_index::merge_unknown_fields(source, current);
  expect(merged["folders"].empty(), "removed folders are not resurrected from unknown-field source data");
}
} // namespace

int main()
{
  test_preserves_unknown_fields_during_migration();
  test_schema_migration_never_downgrades_v2();
  test_schema_reader_prefers_established_key_and_accepts_compatibility_key();
  test_document_validation_and_fingerprint();
  test_identity_fallback_is_legacy_only();
  test_does_not_restore_removed_entities();
  if(failures != 0)
  {
    std::cerr << failures << " note_index test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "note_index tests passed\n";
  return EXIT_SUCCESS;
}
