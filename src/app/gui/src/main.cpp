#include "log.hpp"
#include "app.hpp"
#include "note_project.hpp"

#include <filesystem>

int main(int, char **)
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("Notepp started");

  std::filesystem::path assetsPath = ASSETS_PATH;
  std::filesystem::path dataPath;
  std::filesystem::path configPath;

#if USE_PORTABLE_PATHS
  auto project = notepp::project::initialize_project();

  if(!project)
    return 0;

  dataPath = project->notes;
  configPath = project->config;
#else
  dataPath = DATA_PATH;
  configPath = std::filesystem::path(DATA_PATH).parent_path() / "config";
  std::filesystem::create_directories(configPath);
#endif

  AppConfig config;
  config.assetsPath = assetsPath;
  config.dataPath = dataPath;
  config.configPath = configPath;

  App app(config);
  return app.run();
}