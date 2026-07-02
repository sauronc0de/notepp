// ── app_sdl.cpp ────────────────────────────────────────────────────────────
//
// SDL and OpenGL context setup for the App. Extracted from app.cpp so the
// application entry point file does not need to carry the full SDL/OpenGL
// bring-up sequence.

#include "app.hpp"
#include "log.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include <SDL.h>
#include <SDL_opengl.h>

namespace
{
void apply_initial_borderless_maximized_window(SDL_Window *window)
{
  if(window == nullptr) return;

  if((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
    SDL_SetWindowFullscreen(window, 0);

  SDL_Rect bounds{};
  const int display_index = SDL_GetWindowDisplayIndex(window);
  if(display_index < 0 || SDL_GetDisplayUsableBounds(display_index, &bounds) != 0)
  {
    SDL_MaximizeWindow(window);
    return;
  }

  if((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0)
    SDL_RestoreWindow(window);

  SDL_SetWindowResizable(window, SDL_TRUE);
  SDL_SetWindowBordered(window, SDL_FALSE);
  SDL_SetWindowMinimumSize(window, 320, 200);
  SDL_SetWindowMaximumSize(window, 0, 0);

  SDL_SetWindowPosition(window, bounds.x, bounds.y);
  SDL_SetWindowSize(window, bounds.w, bounds.h);
  SDL_SetWindowPosition(window, bounds.x, bounds.y);
}
} // namespace

void App::init_sdl_gl()
{
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

  // OpenGL 3.2 Core
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  window_ = SDL_CreateWindow(
      "Notepp",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      1100, 700,
      SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS);

  if(!window_)
    throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

  // Start maximized as a regular borderless window. Avoid fullscreen-desktop:
  // profile switches should not blank or reconfigure the rest of the desktop.
  apply_initial_borderless_maximized_window(window_);

  gl_context_ = SDL_GL_CreateContext(window_);
  if(!gl_context_)
  {
    const std::string msg = std::string("OpenGL 3.2 Core context creation failed: ") + SDL_GetError() +
        "\n\nNotepp requires OpenGL 3.2 Core or newer. "
        "Very old integrated GPUs (e.g. Intel HD 2000/3000, Sandy Bridge) do not meet this requirement. "
        "Please update your graphics drivers or run on a newer GPU.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unsupported GPU", msg.c_str(), window_);
    throw std::runtime_error(msg);
  }

  SDL_GL_MakeCurrent(window_, gl_context_);

  // Disable VSync for software renderers (Mesa LLVMpipe/softpipe in Docker, D3D12 CPU-fallback).
  // Without a GPU, SDL_GL_SwapWindow blocks waiting for a sync signal that never arrives properly,
  // causing ~3 FPS. If glGetString returns null (GLEW not yet loaded), we default to VSync off.
  const char *gl_renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  const char *gl_vendor   = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
  const bool software_gl = !gl_renderer ||
      strstr(gl_renderer, "llvmpipe")  || strstr(gl_renderer, "softpipe")  ||
      strstr(gl_renderer, "Software")  || strstr(gl_renderer, "software")  ||
      strstr(gl_renderer, "Microsoft") || strstr(gl_renderer, "D3D12")     ||
      strstr(gl_renderer, "d3d12")     || strstr(gl_renderer, "SVGA")      ||
      strstr(gl_renderer, "VMware");
  SDL_GL_SetSwapInterval(software_gl ? 0 : 1);
  configure_frame_limiter(software_gl);
#if ENABLE_LOG
  LOG_DEBUG("GL Renderer : ", gl_renderer ? gl_renderer : "(null — GLEW not yet init)");
  LOG_DEBUG("GL Vendor   : ", gl_vendor ? gl_vendor : "(null)");
  LOG_DEBUG("Software GL : ", software_gl ? "YES" : "NO");
  LOG_DEBUG("VSync       : ", SDL_GL_GetSwapInterval() == 0 ? "OFF" : "ON",
            "  (SDL_GL_SetSwapInterval -> ", SDL_GL_GetSwapInterval(), ")");
  LOG_DEBUG("Max FPS     : ", max_fps_,
            std::getenv("NOTEPP_MAX_FPS") ? "  (NOTEPP_MAX_FPS)" : "");
#endif
}
