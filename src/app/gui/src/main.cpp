#include "log.hpp"
#include "app.hpp"
#include "app_settings.hpp"
#include "git_sync.hpp"
#include "note_project.hpp"
#include "process.hpp"

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

#if USE_PORTABLE_PATHS
  auto project = notepp::project::initialize_project();

  if(!project)
    return 0;

  projectRoot = project->root;
  dataPath = project->notes;
  configPath = project->config;
#else
  dataPath = DATA_PATH;
  projectRoot = std::filesystem::path(dataPath).parent_path();
  configPath = projectRoot / "config";
  std::filesystem::create_directories(configPath);
#endif

  const std::filesystem::path app_settings_path = notepp::project::get_config_file();
  notepp::app_settings::Store settings_store(app_settings_path);
  const auto settings = settings_store.load();
  notepp::git_sync::Status initial_git_status;
  bool has_initial_git_status = false;
  if(settings && settings.settings.git_sync_enabled)
  {
    process::SystemRunner runner;
    notepp::git_sync::Client client(runner);
    const auto pulled = client.pull_on_open(projectRoot);
    initial_git_status = pulled.status;
    has_initial_git_status = true;
    const notepp::app_settings::GitSyncRecord record{
        std::string(notepp::git_sync::state_name(pulled.status.state)), pulled.status.summary,
        pulled.status.detail, notepp::app_settings::current_utc_timestamp()};
    const auto recorded = settings_store.record_git_sync_status(record);
    if(!recorded)
    {
      LOG_ERROR("Cannot persist startup Git Sync status: ", recorded.message);
    }
    if(!pulled.success)
    {
      LOG_ERROR("Startup Git Sync failed; opening local project: ", pulled.status.summary,
                pulled.status.detail.empty() ? "" : " - ", pulled.status.detail);
    }
  }

  AppConfig config;
  config.assetsPath = assetsPath;
  config.appSettingsPath = app_settings_path;
  config.projectRoot = projectRoot;
  config.dataPath = dataPath;
  config.configPath = configPath;
  config.hasInitialGitStatus = has_initial_git_status;
  config.initialGitStatus = std::move(initial_git_status);

  App app(std::move(config));
  return app.run();
}