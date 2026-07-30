#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace notepp::project
{
struct ProjectInfo
{
  std::filesystem::path root;
  std::filesystem::path notes;
  std::filesystem::path assets;
  std::filesystem::path config;
  std::filesystem::path projectFile;
  std::string projectId;
  int schemaVersion = 0;
};

std::filesystem::path get_appdata_dir();
std::filesystem::path get_config_file();

std::optional<std::filesystem::path> load_last_project_path();
void save_last_project_path(const std::filesystem::path &path);

std::vector<std::filesystem::path> load_recent_projects();

std::optional<std::filesystem::path> select_project_folder();
// Select the startup root without creating or upgrading any project files.
std::optional<std::filesystem::path> select_initial_project_root();

ProjectInfo create_or_open_project(const std::filesystem::path &root);

std::optional<ProjectInfo> initialize_project();
} // namespace notepp::project