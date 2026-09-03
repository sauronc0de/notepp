#include "app_settings.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace notepp::app_settings
{
namespace
{
using Json = nlohmann::json;
constexpr int kSchemaVersion = 2;
constexpr std::size_t kMaxRecentProjects = 10;

struct DocumentResult
{
  bool success = false;
  Json document = Json::object();
  Settings settings;
  atomic_file::Snapshot snapshot;
  bool needsSave = false;
  std::string message;
};

std::string normalizedPathString(const std::filesystem::path &path)
{
  return path.lexically_normal().generic_string();
}

void appendRecent(std::vector<std::filesystem::path> &paths,
                  std::unordered_set<std::string> &seen,
                  const std::filesystem::path &path)
{
  const std::string normalized = normalizedPathString(path);
  if(normalized.empty() || !seen.insert(normalized).second) return;
  if(paths.size() < kMaxRecentProjects) paths.emplace_back(normalized);
}

std::vector<std::filesystem::path> parseLegacyRecents(
    const std::filesystem::path &path, bool &success, std::string &message)
{
  success = true;
  if(path.empty()) return {};

  const auto loaded = atomic_file::read_text(path);
  if(!loaded)
  {
    success = false;
    message = loaded.message;
    return {};
  }
  if(!loaded.snapshot.existed) return {};

  std::vector<std::filesystem::path> result;
  std::unordered_set<std::string> seen;
  std::istringstream input(loaded.snapshot.content);
  std::string line;
  while(std::getline(input, line)) appendRecent(result, seen, std::filesystem::path(line));
  return result;
}

DocumentResult readDocument(const std::filesystem::path &configFile,
                            const std::filesystem::path &legacyRecentProjectsFile)
{
  DocumentResult result;
  const auto loaded = atomic_file::read_text(configFile);
  if(!loaded)
  {
    result.message = loaded.message;
    return result;
  }
  result.snapshot = loaded.snapshot;

  if(loaded.snapshot.existed)
  {
    try
    {
      result.document = Json::parse(loaded.snapshot.content);
    }
    catch(const std::exception &error)
    {
      result.message = "Cannot parse app settings '" + configFile.generic_string() + "': " + error.what();
      return result;
    }
    if(!result.document.is_object())
    {
      result.message = "App settings root must be a JSON object: " + configFile.generic_string();
      return result;
    }
  }
  else
  {
    result.needsSave = true;
  }

  const bool hasSchemaVersion = result.document.contains("schemaVersion") &&
                                result.document["schemaVersion"].is_number_integer();
  if(hasSchemaVersion)
  {
    try
    {
      const Json &schema = result.document["schemaVersion"];
      if(schema.is_number_unsigned())
      {
        const auto schema_version = schema.get<std::uint64_t>();
        if(schema_version > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        {
          result.message = "App settings schema version is outside the supported numeric range";
          return result;
        }
        result.settings.schema_version = static_cast<int>(schema_version);
      }
      else
      {
        const auto schema_version = schema.get<std::int64_t>();
        if(schema_version < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
           schema_version > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
        {
          result.message = "App settings schema version is outside the supported numeric range";
          return result;
        }
        result.settings.schema_version = static_cast<int>(schema_version);
      }
    }
    catch(const std::exception &error)
    {
      result.message = std::string("Invalid app settings schema version: ") + error.what();
      return result;
    }
  }
  else
  {
    result.needsSave = true;
  }
  if(result.settings.schema_version > kSchemaVersion)
  {
    result.message = "App settings schema is newer than this Notepp version: " +
                     std::to_string(result.settings.schema_version);
    return result;
  }
  if(result.settings.schema_version != kSchemaVersion)
  {
    result.settings.schema_version = kSchemaVersion;
    result.needsSave = true;
  }

  if(result.document.contains("gitSyncEnabled") &&
     result.document["gitSyncEnabled"].is_boolean())
  {
    result.settings.git_sync_enabled = result.document["gitSyncEnabled"].get<bool>();
  }
  else
  {
    result.settings.git_sync_enabled = false;
    result.needsSave = true;
  }

  if(result.document.contains("language") && result.document["language"].is_string())
  {
    const std::string language = result.document["language"].get<std::string>();
    if(!language.empty()) result.settings.language = language;
  }
  else if(result.document.contains("language") && !result.document["language"].is_null())
  {
    result.document["language"] = nullptr;
    result.needsSave = true;
  }

  if(result.document.contains("lastGitSync") && result.document["lastGitSync"].is_object())
  {
    const Json &sync = result.document["lastGitSync"];
    if(sync.contains("state") && sync["state"].is_string())
      result.settings.last_git_sync.state = sync["state"].get<std::string>();
    if(sync.contains("summary") && sync["summary"].is_string())
      result.settings.last_git_sync.summary = sync["summary"].get<std::string>();
    if(sync.contains("detail") && sync["detail"].is_string())
      result.settings.last_git_sync.detail = sync["detail"].get<std::string>();
    if(sync.contains("attemptedAt") && sync["attemptedAt"].is_string())
      result.settings.last_git_sync.attempted_at = sync["attemptedAt"].get<std::string>();
  }

  if(result.document.contains("lastProjectPath") &&
     result.document["lastProjectPath"].is_string())
  {
    const std::string value = result.document["lastProjectPath"].get<std::string>();
    if(!value.empty()) result.settings.last_project_path = std::filesystem::path(value);
  }
  else if(result.document.contains("lastProjectPath") &&
          !result.document["lastProjectPath"].is_null())
  {
    result.document["lastProjectPath"] = nullptr;
    result.needsSave = true;
  }

  std::unordered_set<std::string> seen;
  if(result.document.contains("recentProjects") &&
     result.document["recentProjects"].is_array())
  {
    for(const auto &entry : result.document["recentProjects"])
      if(entry.is_string())
        appendRecent(result.settings.recent_projects, seen,
                     std::filesystem::path(entry.get<std::string>()));
  }
  else
  {
    bool legacySuccess = false;
    std::string legacyMessage;
    const auto legacyPaths = parseLegacyRecents(legacyRecentProjectsFile,
                                                legacySuccess, legacyMessage);
    if(!legacySuccess)
    {
      result.message = std::move(legacyMessage);
      return result;
    }
    for(const auto &path : legacyPaths)
      appendRecent(result.settings.recent_projects, seen, path);
    result.needsSave = true;
  }

  result.document["schemaVersion"] = kSchemaVersion;
  result.document["gitSyncEnabled"] = result.settings.git_sync_enabled;
  result.document["language"] = result.settings.language ? Json(*result.settings.language) : Json(nullptr);
  result.document["lastGitSync"] = {
      {"state", result.settings.last_git_sync.state},
      {"summary", result.settings.last_git_sync.summary},
      {"detail", result.settings.last_git_sync.detail},
      {"attemptedAt", result.settings.last_git_sync.attempted_at}};
  if(result.settings.last_project_path)
    result.document["lastProjectPath"] = normalizedPathString(*result.settings.last_project_path);
  else if(!result.document.contains("lastProjectPath"))
  {
    result.document["lastProjectPath"] = nullptr;
    result.needsSave = true;
  }

  Json recent = Json::array();
  for(const auto &path : result.settings.recent_projects)
    recent.push_back(normalizedPathString(path));
  if(!result.document.contains("recentProjects") ||
     result.document["recentProjects"] != recent)
    result.needsSave = true;
  result.document["recentProjects"] = std::move(recent);
  result.success = true;
  return result;
}

std::string serialize(const Json &document)
{
  std::string content = document.dump(2);
  content.push_back('\n');
  return content;
}
} // namespace

std::string current_utc_timestamp()
{
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

Store::Store(std::filesystem::path configFile,
             std::filesystem::path legacyRecentProjectsFile)
    : config_file_(std::move(configFile)),
      legacy_recent_projects_file_(std::move(legacyRecentProjectsFile))
{
  if(legacy_recent_projects_file_.empty())
    legacy_recent_projects_file_ = config_file_.parent_path() / "recent_projects.txt";
}

LoadResult Store::load() noexcept
{
  try
  {
    DocumentResult document = readDocument(config_file_, legacy_recent_projects_file_);
    if(!document.success) return {false, document.settings, document.message};

    if(document.needsSave)
    {
      const auto saved = atomic_file::save_text(config_file_, serialize(document.document),
                                                &document.snapshot);
      if(!saved)
        return {false, document.settings, saved.message};
      document.snapshot = saved.new_snapshot;
    }

    observed_snapshot_ = document.snapshot;
    observed_settings_ = document.settings;
    have_observed_settings_ = true;
    return {true, document.settings, {}};
  }
  catch(const std::exception &error)
  {
    return {false, {}, error.what()};
  }
}

UpdateResult Store::update(bool setGitSync, bool git_sync_enabled,
                           const std::optional<std::string> &language,
                           const std::optional<std::filesystem::path> &projectPath,
                           const std::optional<GitSyncRecord> &gitSyncRecord) noexcept
{
  try
  {
    DocumentResult document = readDocument(config_file_, legacy_recent_projects_file_);
    if(!document.success) return {false, document.settings, document.message};

    if(setGitSync) document.settings.git_sync_enabled = git_sync_enabled;
    if(language)
    {
      if(language->empty()) return {false, document.settings, "language must not be empty"};
      document.settings.language = *language;
    }
    if(gitSyncRecord) document.settings.last_git_sync = *gitSyncRecord;
    if(projectPath)
    {
      document.settings.last_project_path = projectPath->lexically_normal();
      std::vector<std::filesystem::path> updated;
      std::unordered_set<std::string> seen;
      appendRecent(updated, seen, *projectPath);
      for(const auto &path : document.settings.recent_projects)
        appendRecent(updated, seen, path);
      document.settings.recent_projects = std::move(updated);
    }

    document.document["schemaVersion"] = kSchemaVersion;
    document.document["gitSyncEnabled"] = document.settings.git_sync_enabled;
    document.document["language"] = document.settings.language ? Json(*document.settings.language) : Json(nullptr);
    document.document["lastGitSync"] = {
        {"state", document.settings.last_git_sync.state},
        {"summary", document.settings.last_git_sync.summary},
        {"detail", document.settings.last_git_sync.detail},
        {"attemptedAt", document.settings.last_git_sync.attempted_at}};
    if(document.settings.last_project_path)
      document.document["lastProjectPath"] = normalizedPathString(*document.settings.last_project_path);
    else
      document.document["lastProjectPath"] = nullptr;
    Json recent = Json::array();
    for(const auto &path : document.settings.recent_projects)
      recent.push_back(normalizedPathString(path));
    document.document["recentProjects"] = std::move(recent);

    const auto saved = atomic_file::save_text(config_file_, serialize(document.document),
                                              &document.snapshot);
    if(!saved) return {false, document.settings, saved.message};

    observed_snapshot_ = saved.new_snapshot;
    observed_settings_ = document.settings;
    have_observed_settings_ = true;
    return {true, document.settings, {}};
  }
  catch(const std::exception &error)
  {
    return {false, {}, error.what()};
  }
}

UpdateResult Store::set_git_sync_enabled(bool enabled) noexcept
{
  return update(true, enabled, std::nullopt, std::nullopt, std::nullopt);
}

UpdateResult Store::set_language(std::string language) noexcept
{
  return update(false, false, std::move(language), std::nullopt, std::nullopt);
}

UpdateResult Store::record_project(const std::filesystem::path &path) noexcept
{
  return update(false, false, std::nullopt, path, std::nullopt);
}

UpdateResult Store::record_git_sync_status(const GitSyncRecord &record) noexcept
{
  return update(false, false, std::nullopt, std::nullopt, record);
}

PollResult Store::poll() noexcept
{
  const auto current = atomic_file::read_text(config_file_);
  if(!current) return {false, false, observed_settings_, current.message};
  if(observed_snapshot_ && current.snapshot == *observed_snapshot_)
    return {true, false, observed_settings_, {}};

  const LoadResult loaded = load();
  if(!loaded) return {false, false, loaded.settings, loaded.message};
  return {true, true, loaded.settings, {}};
}
} // namespace notepp::app_settings
