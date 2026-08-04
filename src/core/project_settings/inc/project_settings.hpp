#pragma once

#include "atomic_file.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace notepp::project_settings
{
constexpr int current_schema_version = 1;

struct Settings
{
  int schema_version = current_schema_version;
  std::string language;
  bool git_sync_enabled = false;
  bool has_git_sync_enabled = false;
};

struct LoadResult
{
  bool success = false;
  Settings settings;
  bool existed = false;
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

class Store
{
public:
  explicit Store(std::filesystem::path config_file);

  LoadResult load(bool persist_defaults = true) noexcept;
  UpdateResult set_language(const std::string &language) noexcept;
  UpdateResult set_git_sync_enabled(bool enabled) noexcept;

  const std::filesystem::path &path() const noexcept { return config_file_; }

private:
  UpdateResult update(const std::optional<std::string> &language,
                      const std::optional<bool> &git_sync_enabled) noexcept;

  std::filesystem::path config_file_;
};
} // namespace notepp::project_settings
