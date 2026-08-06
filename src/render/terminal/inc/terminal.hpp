#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

struct ImFont;

/**
 * @brief Tabbed embedded terminal backed by independent OS-level PTYs and
 *        vt100/xterm-compatible terminal emulators (libvterm).
 *
 * One Terminal owns the window and any number of shell sessions. Hiding the
 * window preserves all sessions; @ref stop() shuts down every session.
 */
class Terminal
{
public:
  using SessionId = std::uint64_t;

  Terminal();
  ~Terminal();

  Terminal(const Terminal &) = delete;
  Terminal &operator=(const Terminal &) = delete;

  /// Start the selected session in @p cwd, or create the first session.
  void start(const std::filesystem::path &cwd, int rows, int cols);

  /// Create and select an independent shell session. Returns its stable ID.
  SessionId addSession(const std::filesystem::path &cwd, int rows = 24, int cols = 80);

  /// Select the next or previous terminal tab. Returns false when there are no sessions.
  bool selectAdjacentSession(bool previous);

  /// Stop and remove one session. Returns false when @p id does not exist.
  bool closeSession(SessionId id);

  /// Working directory used by the in-window New Terminal controls.
  void setDefaultWorkingDirectory(const std::filesystem::path &cwd);

  /// Stop every shell and remove all sessions.
  void stop();

  /// True when at least one session has a running shell.
  bool isRunning() const noexcept;

  /// True when the terminal window and its selected session owned keyboard focus.
  bool hasFocus() const noexcept;

  /// Release keyboard focus and notify focused terminal applications.
  void releaseFocus();

  std::size_t sessionCount() const noexcept;
  bool isSessionRunning(SessionId id) const noexcept;

  /// Tell the selected session its terminal grid size changed.
  void resize(int rows, int cols);

  /// Write raw bytes to the selected session's PTY.
  void write(std::string_view bytes);

  /// Clear the selected session's text selection.
  void clearSelection();

  /// Translate a focused-window key event for the selected session.
  void sendKey(int imguiKey, bool ctrl, bool shift, bool alt);

  /// Set the preferred terminal window height in pixels.
  void setWindowHeight(float height) noexcept;
  float windowHeight() const noexcept;

  /// Render the terminal window and its session tabs.
  /// @p pendingText is composed SDL text for this frame and is written before
  /// Enter/navigation events when the selected terminal has focus.
  /// Set @p acceptKeyboardInput to false when the event that opened the
  /// terminal must not also be forwarded to the shell.
  void render(bool *open, std::string_view pendingText = {}, bool acceptKeyboardInput = true);

  /// Monospaced font used for cell rendering. Defaults to ImGui's current font.
  void setFont(ImFont *font) noexcept;

private:
  struct Impl;
  Impl *impl_;
};
