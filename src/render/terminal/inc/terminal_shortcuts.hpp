#pragma once

namespace notepp::terminal_detail
{
enum class ClipboardKey
{
  Other,
  C,
  V,
  Insert
};

enum class ClipboardAction
{
  None,
  Copy,
  Paste
};

constexpr ClipboardAction terminalClipboardAction(ClipboardKey key, bool ctrl, bool shift,
                                                  bool alt, bool command) noexcept
{
  if(alt) return ClipboardAction::None;
  if(command && !ctrl && !shift)
  {
    if(key == ClipboardKey::C) return ClipboardAction::Copy;
    if(key == ClipboardKey::V) return ClipboardAction::Paste;
  }
  if(ctrl && shift && key == ClipboardKey::C) return ClipboardAction::Copy;
  if(ctrl && shift && key == ClipboardKey::V) return ClipboardAction::Paste;
  if(ctrl && !shift && key == ClipboardKey::Insert) return ClipboardAction::Copy;
  if(!ctrl && shift && key == ClipboardKey::Insert) return ClipboardAction::Paste;
  return ClipboardAction::None;
}
} // namespace notepp::terminal_detail
