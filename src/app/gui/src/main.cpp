#include "log.hpp"
#include "app.hpp"
#include "app_settings.hpp"
#include "git_sync.hpp"
#include "note_project.hpp"
#include "process.hpp"
#include "project_settings.hpp"

#include <exception>
#include <filesystem>

int main(int, char **)
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("Notepp started");

  std::filesystem::path assetsPath = ASSETS_PATH;
  std::filesystem::path projectRoot;
  std::filesystem::path dataPath;
  std::filesystem::path configPath;
  std::filesystem::path workspacePath;
  const std::filesystem::path app_settings_path = notepp::project::get_config_file();
  notepp::app_settings::Store settings_store(app_settings_path);
  const auto settings = settings_store.load();
  notepp::git_sync::Status initial_git_status;
  bool has_initial_git_status = false;

#if USE_PORTABLE_PATHS
  const auto selected_root = notepp::project::select_initial_project_root();
  if(!selected_root) return 0;
  projectRoot = *selected_root;

  const std::filesystem::path project_settings_path =
      projectRoot / "config" / "project_settings.json";
  notepp::project_settings::Store project_settings_store(project_settings_path);
  // Read the local project setting without creating it before startup sync.
  // This file is intentionally local and is not part of Git Sync.
  const auto project_settings = project_settings_store.load(false);
  const bool project_git_sync_enabled =
      project_settings && project_settings.settings.has_git_sync_enabled
          ? project_settings.settings.git_sync_enabled
          : (settings && settings.settings.git_sync_enabled);

  // Pull before create_or_open_project can create directories or upgrade the
  // shared manifest. Every failure is advisory and the local tree still opens.
  if(project_git_sync_enabled)
  {
    process::SystemRunner runner;
    notepp::git_sync::Client client(runner);
    notepp::git_sync::OperationResult pulled;
    try
    {
      pulled = client.pull_on_open(projectRoot);
    }
    catch(const std::exception &error)
    {
      pulled = notepp::git_sync::exception_result("Startup Git Sync", error.what());
    }
    catch(...)
    {
      pulled = notepp::git_sync::exception_result("Startup Git Sync");
    }
    initial_git_status = pulled.status;
    has_initial_git_status = true;
    const notepp::app_settings::GitSyncRecord record{
        std::string(notepp::git_sync::state_name(pulled.status.state)), pulled.status.summary,
        pulled.status.detail, notepp::app_settings::current_utc_timestamp()};
    const auto recorded = settings_store.record_git_sync_status(record);
    if(!recorded)
      LOG_ERROR("Cannot persist startup Git Sync status: ", recorded.message);
    if(!pulled.success)
    {
      LOG_ERROR("Startup Git Sync failed; opening local project: ", pulled.status.summary,
                pulled.status.detail.empty() ? "" : " - ", pulled.status.detail);
      (void)0;
    }
  }

  const auto project = notepp::project::create_or_open_project(projectRoot);
  dataPath = project.notes;
  configPath = project.config;
  workspacePath = project.workspace;
#else
  dataPath = DATA_PATH;
  projectRoot = std::filesystem::path(dataPath).parent_path();
  configPath = projectRoot / "config";
  std::filesystem::create_directories(configPath);
  workspacePath = notepp::project::get_appdata_dir() / "projects" / "standalone";
  std::filesystem::create_directories(workspacePath);
#endif

  AppConfig config;
  config.assetsPath = assetsPath;
  config.appSettingsPath = app_settings_path;
  config.projectRoot = projectRoot;
  config.dataPath = dataPath;
  config.configPath = configPath;
  config.workspacePath = workspacePath;
  config.hasInitialGitStatus = has_initial_git_status;
  config.initialGitStatus = std::move(initial_git_status);

  App app(std::move(config));
  return app.run();
}