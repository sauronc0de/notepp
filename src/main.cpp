#include "log.hpp"

int main()
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("🗒️ Notepp started 🗒️");
  return 0;
}