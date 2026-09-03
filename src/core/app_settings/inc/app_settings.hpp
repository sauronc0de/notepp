#pragma once

#include "atomic_file.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace notepp::app_settings
{
[[nodiscard]] std::string current_utc_timestamp();

struct GitSyncRecord
{
  std::string state;
  std::string summary;
  std::string detail;
  std::string attempted_at;
};

struct Settings
{
  int schema_version = 2;
  bool git_sync_enabled = false;
  std::optional<std::string> language;
  std::optional<std::filesystem::path> last_project_path;
  std::vector<std::filesystem::path> recent_projects;
  GitSyncRecord last_git_sync;
};

struct LoadResult
{
  bool success = false;
  Settings settings;
  std::string message;

  explicit operator bool() const noexcept { return success; }
};

struct UpdateResult
{
  bool success = false;
  Settings settings;
  std::string message;

  explicit operator bool() const noexcept { return success; }
};

struct PollResult
{
  bool success = false;
  bool changed = false;
  Settings settings;
  std::string message;

  explicit operator bool() const noexcept { return success; }
};

class Store
{
public:
  explicit Store(std::filesystem::path configFile,
                 std::filesystem::path legacyRecentProjectsFile = {});

  LoadResult load() noexcept;
  UpdateResult set_git_sync_enabled(bool enabled) noexcept;
  UpdateResult set_language(std::string language) noexcept;
  UpdateResult record_project(const std::filesystem::path &path) noexcept;
  UpdateResult record_git_sync_status(const GitSyncRecord &record) noexcept;
  PollResult poll() noexcept;

  const std::filesystem::path &path() const noexcept { return config_file_; }

private:
  UpdateResult update(bool setGitSync, bool git_sync_enabled,
                      const std::optional<std::string> &language,
                      const std::optional<std::filesystem::path> &projectPath,
                      const std::optional<GitSyncRecord> &gitSyncRecord) noexcept;

  std::filesystem::path config_file_;
  std::filesystem::path legacy_recent_projects_file_;
  std::optional<atomic_file::Snapshot> observed_snapshot_;
  Settings observed_settings_;
  bool have_observed_settings_ = false;
};
} // namespace notepp::app_settings
