#include "app.hpp"

#include <algorithm>
#include <cstdlib>

#include <SDL.h>

void App::configure_frame_limiter(bool software_gl)
{
  // Always cap rendering. VSync is not reliable in software/virtualized GL, and may be
  // disabled by drivers or user settings, so keep an explicit application-side limit.
  const int default_fps = software_gl ? 12 : 60;
  int configured_fps = default_fps;

  if(const char *env = std::getenv("NOTEPP_MAX_FPS"))
  {
    char *end = nullptr;
    const long parsed = std::strtol(env, &end, 10);
    if(end != env && parsed > 0)
      configured_fps = static_cast<int>(std::min<long>(parsed, 240));
  }

  max_fps_ = std::max(1, configured_fps);
  min_frame_ticks_ = SDL_GetPerformanceFrequency() / static_cast<unsigned long long>(max_fps_);
  last_frame_ticks_ = 0;
}

void App::limit_frame_rate()
{
  if(min_frame_ticks_ == 0)
    configure_frame_limiter(false);

  const unsigned long long now = SDL_GetPerformanceCounter();
  if(last_frame_ticks_ != 0)
  {
    const unsigned long long target = last_frame_ticks_ + min_frame_ticks_;
    if(now < target)
    {
      const unsigned long long freq = SDL_GetPerformanceFrequency();
      unsigned long long remaining = target - now;
      while(remaining > 0)
      {
        const unsigned long long ms = (remaining * 1000ULL) / freq;
        if(ms > 1)
          SDL_Delay(static_cast<Uint32>(ms - 1));
        else
          SDL_Delay(1);

        const unsigned long long after_sleep = SDL_GetPerformanceCounter();
        if(after_sleep >= target) break;
        remaining = target - after_sleep;
      }
    }
  }
  last_frame_ticks_ = SDL_GetPerformanceCounter();
}