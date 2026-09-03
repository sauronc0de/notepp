// ── app_frame_end.cpp ──────────────────────────────────────────────────────
//
// Frame-end rendering: ImGui render call, OpenGL viewport clear, platform
// window updates, and SDL swap. Extracted from app.cpp.
//
// The viewport topmost helpers and debug swap timing variable that
// `frame_end` relies on were previously file-static in `app.cpp`. They
// are duplicated here to keep the file self-contained. If a third caller
// needs them they should be moved to a small `app_viewport.cpp` module.

#include "app.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <cstdint>
#include <cstdio>
#include <unordered_set>

namespace
{
SDL_Window *viewport_platform_window_local(const ImGuiViewport *viewport)
{
  // imgui_impl_sdl2 stores the SDL window ID in PlatformHandle.
  if(viewport == nullptr) return nullptr;
  const auto window_id = static_cast<Uint32>(reinterpret_cast<std::uintptr_t>(viewport->PlatformHandle));
  if(window_id == 0) return nullptr;
  return SDL_GetWindowFromID(window_id);
}

const ImGuiViewport *find_platform_viewport_by_id_local(const ImGuiPlatformIO &platform_io, ImGuiID viewport_id)
{
  for(ImGuiViewport *viewport : platform_io.Viewports)
  {
    if(viewport != nullptr && viewport->ID == viewport_id) return viewport;
  }
  return nullptr;
}

void apply_viewport_always_on_top_local(const ImGuiViewport *viewport, SDL_Window *main_window, bool always_on_top)
{
#if SDL_VERSION_ATLEAST(2, 0, 16)
  SDL_Window *platform_window = viewport_platform_window_local(viewport);
  if(platform_window != nullptr && platform_window != main_window)
  {
    SDL_SetWindowAlwaysOnTop(platform_window, always_on_top ? SDL_TRUE : SDL_FALSE);
  }
#else
  (void)viewport;
  (void)main_window;
  (void)always_on_top;
#endif
}

bool viewport_inherits_topmost_local(
    const ImGuiViewport *viewport,
    const ImGuiPlatformIO &platform_io,
    const std::unordered_set<unsigned int> &pinned_topmost_viewports)
{
  if(viewport == nullptr) return false;
  if((viewport->Flags & ImGuiViewportFlags_TopMost) != 0) return true;

  const ImGuiViewport *current = viewport;
  while(current != nullptr && current->ID != 0)
  {
    if(pinned_topmost_viewports.count(current->ID) != 0) return true;
    if(current->ParentViewportId == 0 || current->ParentViewportId == current->ID) break;
    current = find_platform_viewport_by_id_local(platform_io, current->ParentViewportId);
  }
  return false;
}

float g_dbg_swap_ms_local [[maybe_unused]] = 0.0f;
} // namespace

void App::frame_end()
{
  render_note_comparison();
  render_history_indicator();
  ImGui::Render();

  int display_w = 0, display_h = 0;
  SDL_GL_GetDrawableSize(window_, &display_w, &display_h);

  glViewport(0, 0, display_w, display_h);
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  ImGuiIO &io = ImGui::GetIO();
  SDL_Window *backup_current_window = SDL_GL_GetCurrentWindow();
  SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
  if(detach_transition_pending_)
  {
    if(detach_transition_target_)
      io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    else if((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
    {
      // Let the backend reconcile once, then synchronously destroy secondary
      // platform windows before disabling viewport processing.
      ImGui::UpdatePlatformWindows();
      ImGui::DestroyPlatformWindows();
      SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
      io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
      pinned_topmost_viewports_.clear();
    }
    detach_transition_pending_ = false;
  }
  if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    ImGui::UpdatePlatformWindows();

    ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();
    for(ImGuiViewport *viewport : platform_io.Viewports)
    {
      if(viewport == nullptr) continue;
      const bool should_be_topmost = viewport_inherits_topmost_local(viewport, platform_io, pinned_topmost_viewports_);
      apply_viewport_always_on_top_local(viewport, window_, should_be_topmost);
    }

    ImGui::RenderPlatformWindowsDefault();
    SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
  }
#ifdef NOTEPP_DEBUG_UI
  const Uint64 t_swap = SDL_GetPerformanceCounter();
#endif
  SDL_GL_SwapWindow(window_);
#ifdef NOTEPP_DEBUG_UI
  g_dbg_swap_ms_local = (float)(SDL_GetPerformanceCounter() - t_swap) * 1000.f / (float)SDL_GetPerformanceFrequency();
#endif
}
