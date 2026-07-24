#pragma once

#include <imgui.h>
#include <vterm_keycodes.h>

#include <cstdint>

namespace notepp::terminal_detail
{

constexpr uint32_t terminalControlCharacterFromImGui(int key) noexcept
{
  if(key < ImGuiKey_A || key > ImGuiKey_Z) return 0;
  return static_cast<uint32_t>('a' + (key - ImGuiKey_A));
}

constexpr VTermKey terminalKeyFromImGui(int key) noexcept
{
  switch(key)
  {
  case ImGuiKey_Tab:
    return VTERM_KEY_TAB;
  case ImGuiKey_LeftArrow:
    return VTERM_KEY_LEFT;
  case ImGuiKey_RightArrow:
    return VTERM_KEY_RIGHT;
  case ImGuiKey_UpArrow:
    return VTERM_KEY_UP;
  case ImGuiKey_DownArrow:
    return VTERM_KEY_DOWN;
  case ImGuiKey_PageUp:
    return VTERM_KEY_PAGEUP;
  case ImGuiKey_PageDown:
    return VTERM_KEY_PAGEDOWN;
  case ImGuiKey_Home:
    return VTERM_KEY_HOME;
  case ImGuiKey_End:
    return VTERM_KEY_END;
  case ImGuiKey_Insert:
    return VTERM_KEY_INS;
  case ImGuiKey_Delete:
    return VTERM_KEY_DEL;
  case ImGuiKey_Backspace:
    return VTERM_KEY_BACKSPACE;
  case ImGuiKey_Enter:
  case ImGuiKey_KeypadEnter:
    return VTERM_KEY_ENTER;
  case ImGuiKey_Escape:
    return VTERM_KEY_ESCAPE;
  case ImGuiKey_F1:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(1));
  case ImGuiKey_F2:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(2));
  case ImGuiKey_F3:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(3));
  case ImGuiKey_F4:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(4));
  case ImGuiKey_F5:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(5));
  case ImGuiKey_F6:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(6));
  case ImGuiKey_F7:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(7));
  case ImGuiKey_F8:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(8));
  case ImGuiKey_F9:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(9));
  case ImGuiKey_F10:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(10));
  case ImGuiKey_F11:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(11));
  case ImGuiKey_F12:
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(12));
  default:
    return VTERM_KEY_NONE;
  }
}

} // namespace notepp::terminal_detail
