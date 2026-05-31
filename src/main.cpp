#include "log.hpp"
#include "app.hpp"
#include "project_manager.hpp"

#include <filesystem>

int main(int, char **)
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("Notepp started");

  std::filesystem::path assetsPath = ASSETS_PATH;
  std::filesystem::path dataPath;

#if USE_PORTABLE_PATHS
  auto project = notepp::project::initialize_project();

  if(!project)
    return 0;

  dataPath = project->notes;
#else
  dataPath = DATA_PATH;
#endif

  AppConfig config;
  config.assetsPath = assetsPath;
  config.dataPath = dataPath;

  App app(config);
  return app.run();
}