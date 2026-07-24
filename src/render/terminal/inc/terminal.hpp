#pragma once

#include <filesystem>
#include <string>

struct ImFont;

/**
 * @brief Embedded terminal panel backed by a real OS-level PTY and a
 *        vt100/xterm-compatible terminal emulator (libvterm).
 *
 * Lifecycle: construct → @ref start() → optional @ref resize() / @ref write() /
 * @ref sendKey() → @ref render() every frame → @ref stop() at shutdown.
 *
 * One instance lives in the App and is reused across open/close cycles.
 */
class Terminal
{
public:
  Terminal();
  ~Terminal();

  Terminal(const Terminal &) = delete;
  Terminal &operator=(const Terminal &) = delete;

  /// Spawn the shell in @p cwd with the given initial screen size.
  /// Safe to call multiple times; the previous session is stopped first.
  void start(const std::filesystem::path &cwd, int rows, int cols);

  /// Kill the shell and join the reader thread. No-op if not started.
  void stop();

  /// True between a successful @ref start() and a successful @ref stop().
  bool isRunning() const noexcept;

  /// True when the terminal window owned keyboard focus in the last frame.
  bool hasFocus() const noexcept;

  /// Tell the underlying PTY the screen size changed.
  void resize(int rows, int cols);

  /// Write raw bytes to the PTY (e.g. pasted text, special sequences).
  void write(std::string_view bytes);

  /// Translate a focused-window key event into bytes for the PTY.
  void sendKey(int imguiKey, bool ctrl, bool shift, bool alt);

  /// Render the terminal window. @p open is a toggled visibility flag.
  /// @p pendingText is composed SDL text for this frame and is written
  /// before Enter/navigation key events when the terminal has focus.
  void render(bool *open, std::string_view pendingText = {});

  /// Font used for cell rendering. Defaults to the app's regular font.
  void setFont(ImFont *font) noexcept;

private:
  struct Impl;
  Impl *impl_;

  // File-local helpers in terminal.cpp need access to Impl. Keeping these
  // out of the public API.
  friend struct Impl;
  friend void vtermOutputCb(const char *, size_t, void *);
  friend void readerLoop(Impl *);
};
