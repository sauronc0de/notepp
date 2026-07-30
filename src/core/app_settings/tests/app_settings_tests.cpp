#include "app_settings.hpp"

#include "atomic_file.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
namespace settings = notepp::app_settings;
using Json = nlohmann::json;

namespace
{
int failures = 0;

void expect(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

class TempDir
{
public:
  TempDir()
  {
    static int counter = 0;
    path_ = fs::temp_directory_path() /
            ("notepp_app_settings_tests_" + std::to_string(++counter));
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  ~TempDir()
  {
    std::error_code error;
    fs::remove_all(path_, error);
  }
  const fs::path &path() const { return path_; }

private:
  fs::path path_;
};

Json readJson(const fs::path &path)
{
  std::ifstream input(path);
  return Json::parse(input);
}

void testMissingSettingsDefaultToDisabled()
{
  TempDir temp;
  settings::Store store(temp.path() / "config.json");
  const auto loaded = store.load();
  expect(loaded.success, "missing settings load succeeds");
  expect(!loaded.settings.git_sync_enabled, "Git sync defaults disabled");
  expect(loaded.settings.schema_version == 2, "current schema is used");
  expect(fs::exists(temp.path() / "config.json"), "canonical defaults are persisted");
}

void testLegacyMigrationPreservesUnknownFieldsAndRecents()
{
  TempDir temp;
  const fs::path config = temp.path() / "config.json";
  const fs::path recents = temp.path() / "recent_projects.txt";
  {
    std::ofstream output(config);
    output << R"({"lastProjectPath":"/old/project","futureField":{"keep":7}})";
  }
  {
    std::ofstream output(recents);
    output << "/old/project\n/other/project\n/old/project\n";
  }

  settings::Store store(config, recents);
  const auto loaded = store.load();
  expect(loaded.success, "legacy settings migrate");
  expect(!loaded.settings.git_sync_enabled, "migration never opts in Git sync");
  expect(loaded.settings.recent_projects.size() == 2, "legacy recents are deduplicated");

  const Json migrated = readJson(config);
  expect(migrated["schemaVersion"] == 2, "schema version migrated");
  expect(migrated["gitSyncEnabled"] == false, "disabled default persisted");
  expect(migrated["futureField"]["keep"] == 7, "unknown JSON fields preserved");
  expect(fs::exists(recents), "legacy recents file is retained");
}

void testUpdatesReReadLatestDocument()
{
  TempDir temp;
  const fs::path config = temp.path() / "config.json";
  settings::Store first(config);
  settings::Store second(config);
  expect(first.load().success, "first store loads");
  expect(second.load().success, "second store loads");

  const fs::path project = temp.path() / "project";
  expect(first.record_project(project).success, "project path update succeeds");
  expect(second.set_git_sync_enabled(true).success, "toggle update re-reads project path");

  settings::Store verifier(config);
  auto loaded = verifier.load();
  expect(loaded.success, "updated settings load");
  expect(loaded.settings.git_sync_enabled, "toggle preserved");
  expect(loaded.settings.last_project_path == project.lexically_normal(),
         "project path preserved by toggle update");

  expect(first.record_project(temp.path() / "other").success,
         "project update re-reads toggle");
  loaded = verifier.load();
  expect(loaded.settings.git_sync_enabled, "project update does not erase toggle");
}

void testRecentProjectsAreNormalizedDeduplicatedAndCapped()
{
  TempDir temp;
  settings::Store store(temp.path() / "config.json");
  expect(store.load().success, "store loads for recent test");
  for(int index = 0; index < 12; ++index)
    expect(store.record_project(temp.path() / ("project" + std::to_string(index))).success,
           "recording recent project succeeds");
  expect(store.record_project(temp.path() / "project5" / ".").success,
         "normalized duplicate update succeeds");

  const auto loaded = store.load();
  expect(loaded.settings.recent_projects.size() == 10, "recent projects are capped at ten");
  expect(loaded.settings.recent_projects.front() == (temp.path() / "project5" / ".").lexically_normal(),
         "most recently used normalized path is first");
}

void testMalformedSettingsAreNotOverwritten()
{
  TempDir temp;
  const fs::path config = temp.path() / "config.json";
  const std::string malformed = "{not-json";
  {
    std::ofstream output(config);
    output << malformed;
  }
  settings::Store store(config);
  expect(!store.load().success, "malformed settings report an error");
  expect(!store.set_git_sync_enabled(true).success, "malformed settings block updates");
  const auto loaded = atomic_file::read_text(config);
  expect(loaded && loaded.snapshot.content == malformed,
         "malformed canonical settings are preserved");
}

void testNewerSchemaIsNotDowngraded()
{
  TempDir temp;
  const fs::path config = temp.path() / "config.json";
  const std::string future = R"({"schemaVersion":99,"gitSyncEnabled":true,"recentProjects":[]})";
  {
    std::ofstream output(config);
    output << future;
  }
  settings::Store store(config);
  expect(!store.load().success, "newer settings schema is rejected");
  expect(!store.record_project(temp.path() / "project").success,
         "newer settings schema blocks destructive updates");
  const auto loaded = atomic_file::read_text(config);
  expect(loaded && loaded.snapshot.content == future,
         "newer settings document is not downgraded");
}

void testOutOfRangeUnsignedSchemaIsNotNarrowedOrRewritten()
{
  TempDir temp;
  const fs::path config = temp.path() / "config.json";
  const std::string future =
      R"({"schemaVersion":18446744073709551615,"gitSyncEnabled":true,"recentProjects":[]})";
  {
    std::ofstream output(config);
    output << future;
  }

  settings::Store store(config);
  expect(!store.load().success, "UINT64_MAX schema version is rejected");
  expect(!store.set_git_sync_enabled(false).success,
         "out-of-range unsigned schema blocks destructive updates");
  const auto loaded = atomic_file::read_text(config);
  expect(loaded && loaded.snapshot.content == future,
         "out-of-range unsigned schema document is not rewritten");
}

void testPollReportsExternalChangesWithoutChangingProject()
{
  TempDir temp;
  const fs::path config = temp.path() / "config.json";
  settings::Store store(config);
  expect(store.load().success, "poll store loads");
  expect(!store.poll().changed, "unchanged settings do not notify");

  Json external = readJson(config);
  external["gitSyncEnabled"] = true;
  external["lastProjectPath"] = "/external/project";
  std::string content = external.dump(2) + "\n";
  expect(atomic_file::save_text(config, content).operator bool(),
         "external settings replacement succeeds");

  const auto polled = store.poll();
  expect(polled.success && polled.changed, "external settings change is reported");
  expect(polled.settings.git_sync_enabled, "external toggle is reloaded");
  expect(polled.settings.last_project_path == fs::path("/external/project"),
         "external project path is observed, not acted upon by store");
}
} // namespace

int main()
{
  testMissingSettingsDefaultToDisabled();
  testLegacyMigrationPreservesUnknownFieldsAndRecents();
  testUpdatesReReadLatestDocument();
  testRecentProjectsAreNormalizedDeduplicatedAndCapped();
  testMalformedSettingsAreNotOverwritten();
  testNewerSchemaIsNotDowngraded();
  testOutOfRangeUnsignedSchemaIsNotNarrowedOrRewritten();
  testPollReportsExternalChangesWithoutChangingProject();
  if(failures != 0)
  {
    std::cerr << failures << " app_settings expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "app_settings tests passed\n";
  return EXIT_SUCCESS;
}
