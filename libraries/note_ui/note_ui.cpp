#include "note_ui.hpp"

#include "string_utils.hpp"

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_map>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_opengl.h>

namespace NoteUi
{
ImVec4 folder_accent_color(bool use_custom_color, float color_r, float color_g, float color_b, const ImGuiStyle &style)
{
  if(use_custom_color) return ImVec4(NoteCore::clamp01f(color_r), NoteCore::clamp01f(color_g), NoteCore::clamp01f(color_b), 1.0f);
  (void)style;
  return ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
}

namespace
{
std::unordered_map<std::string, GLuint> g_toolbar_icon_cache;
} // namespace

ImVec4 mix_color(ImVec4 a, ImVec4 b, float t)
{
  t = NoteCore::clamp01f(t);
  return ImVec4(
      a.x + (b.x - a.x) * t,
      a.y + (b.y - a.y) * t,
      a.z + (b.z - a.z) * t,
      a.w + (b.w - a.w) * t);
}

ImVec4 with_alpha(ImVec4 c, float a)
{
  c.w = a;
  return c;
}

NoteTheme make_note_theme(bool use_custom_color, float color_r, float color_g, float color_b, const ImGuiStyle &style)
{
  const ImVec4 accent = folder_accent_color(use_custom_color, color_r, color_g, color_b, style);
  const ImVec4 base_bg = style.Colors[ImGuiCol_WindowBg];
  const ImVec4 border = style.Colors[ImGuiCol_Border];

  NoteTheme t;
  t.window_bg = mix_color(base_bg, accent, 0.14f);
  t.window_bg.w = base_bg.w;
  t.title_bg = mix_color(base_bg, accent, 0.44f);
  t.title_bg.w = 1.0f;
  t.title_bg_active = mix_color(base_bg, accent, 0.58f);
  t.title_bg_active.w = 1.0f;
  t.title_bg_collapsed = mix_color(base_bg, accent, 0.34f);
  t.title_bg_collapsed.w = 1.0f;
  t.border = mix_color(border, accent, 0.50f);
  t.border.w = 1.0f;
  return t;
}

int push_folder_imgui_theme(const NoteTheme &nt, const ImGuiStyle &style)
{
  const ImVec4 accent = nt.title_bg_active;
  const ImVec4 soft = mix_color(nt.window_bg, accent, 0.35f);
  const ImVec4 soft_hover = mix_color(nt.window_bg, accent, 0.50f);
  const ImVec4 soft_active = mix_color(nt.window_bg, accent, 0.62f);

  ImGui::PushStyleColor(ImGuiCol_WindowBg, nt.window_bg);
  ImGui::PushStyleColor(ImGuiCol_TitleBg, nt.title_bg);
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, nt.title_bg_active);
  ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, nt.title_bg_collapsed);
  ImGui::PushStyleColor(ImGuiCol_Border, nt.border);

  ImGui::PushStyleColor(ImGuiCol_FrameBg, soft);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, soft_hover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, soft_active);
  ImGui::PushStyleColor(ImGuiCol_CheckMark, mix_color(accent, ImVec4(1, 1, 1, 1), 0.35f));

  ImGui::PushStyleColor(ImGuiCol_Button, soft);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, soft_hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, soft_active);

  ImGui::PushStyleColor(ImGuiCol_Header, soft);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, soft_hover);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, soft_active);

  ImGui::PushStyleColor(ImGuiCol_Tab, mix_color(style.Colors[ImGuiCol_Tab], accent, 0.45f));
  ImGui::PushStyleColor(ImGuiCol_TabHovered, mix_color(style.Colors[ImGuiCol_TabHovered], accent, 0.45f));
  ImGui::PushStyleColor(ImGuiCol_TabActive, mix_color(style.Colors[ImGuiCol_TabActive], accent, 0.45f));

  ImGui::PushStyleColor(ImGuiCol_SliderGrab, mix_color(accent, ImVec4(1, 1, 1, 1), 0.22f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, mix_color(accent, ImVec4(1, 1, 1, 1), 0.35f));

  return 20;
}

ImTextureID get_toolbar_icon_texture(std::string_view icon_name)
{
  if(icon_name.empty()) return static_cast<ImTextureID>(0);

  const std::string key(icon_name);
  const auto it = g_toolbar_icon_cache.find(key);
  if(it != g_toolbar_icon_cache.end()) return (ImTextureID)(uintptr_t)it->second;

  static const bool img_ready = []() {
    IMG_Init(IMG_INIT_PNG);
    return true;
  }();
  (void)img_ready;

  const std::filesystem::path p = std::filesystem::path(ASSETS_PATH) / "icons" / key;
  SDL_Surface *loaded = IMG_Load(p.string().c_str());
  if(!loaded) return static_cast<ImTextureID>(0);

  SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(loaded);
  if(!rgba) return static_cast<ImTextureID>(0);

  GLuint tex = 0;
  glGenTextures(1, &tex);
  if(tex == 0)
  {
    SDL_FreeSurface(rgba);
    return static_cast<ImTextureID>(0);
  }

  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
  SDL_FreeSurface(rgba);

  g_toolbar_icon_cache.emplace(key, tex);
  return (ImTextureID)(uintptr_t)tex;
}

void clear_toolbar_icon_cache()
{
  for(auto &[name, texture] : g_toolbar_icon_cache)
  {
    (void)name;
    if(texture == 0) continue;
    GLuint tex = texture;
    glDeleteTextures(1, &tex);
  }
  g_toolbar_icon_cache.clear();
}
} // namespace NoteUi
