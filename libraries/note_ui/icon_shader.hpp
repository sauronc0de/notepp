#pragma once
#include <imgui.h>

namespace NoteUi
{

// Call once after GL context + ImGui_ImplOpenGL3_Init().
void init_icon_shader();

// Call before GL context is destroyed.
void destroy_icon_shader();

// Shaded icon button — handles layout, hover animation, and GLSL rendering.
// 'active': draws a filled background (for toggle/mode buttons).
// Returns true when clicked (left button, released). Right-click can be
// detected with ImGui::IsItemClicked(ImGuiMouseButton_Right) after the call.
bool shaded_icon_button(const char *id,
                        ImTextureID tex,
                        ImVec2      size,
                        const char *fallback,
                        bool        active = false);

} // namespace NoteUi
