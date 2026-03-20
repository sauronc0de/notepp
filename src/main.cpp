#include "log.hpp"
#include "app.hpp"

int main(int argc, char **argv)
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("🗒️  Notepp started 🗒️");
  App app(argc > 1 ? argv[1] : "");
  return app.run();
}
