#pragma once

#include "atomic_file.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace notepp::app_settings
{
struct Settings
{
  int schema_version = 2;
  bool git_sync_enabled = false;
  std::optional<std::filesystem::path> last_project_path;
  std::vector<std::filesystem::path> recent_projects;
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
  UpdateResult record_project(const std::filesystem::path &path) noexcept;
  PollResult poll() noexcept;

  const std::filesystem::path &path() const noexcept { return config_file_; }

private:
  UpdateResult update(bool setGitSync, bool git_sync_enabled,
                      const std::optional<std::filesystem::path> &projectPath) noexcept;

  std::filesystem::path config_file_;
  std::filesystem::path legacy_recent_projects_file_;
  std::optional<atomic_file::Snapshot> observed_snapshot_;
  Settings observed_settings_;
  bool have_observed_settings_ = false;
};
} // namespace notepp::app_settings
