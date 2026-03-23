#include "log.hpp"
#include "app.hpp"
#include "runtime_paths.hpp"

int main(int argc, char **argv)
{
  (void)argc;
  NoteppPaths::initialize(argv != nullptr ? argv[0] : nullptr);
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("🗒️  Notepp started 🗒️");
  App app;
  return app.run();
}
