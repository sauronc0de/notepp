// Headless tests for the embedded terminal.
//
// The Terminal class is the only thing we exercise here. Tests are
// limited to behaviour that does not require an ImGui context (i.e. they
// touch the PTY + libvterm plumbing, not the render() method).

#include "terminal.hpp"
#include "terminal_key_map.hpp"
#include "terminal_selection.hpp"
#include "terminal_shortcuts.hpp"
#include "pty.hpp"

#include <imgui.h>
#include <vterm.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <cerrno>
#include <poll.h>
#include <sys/wait.h>
#endif

namespace
{
int failures = 0;

void expect(bool cond, const char *msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void captureVtermOutput(const char *bytes, size_t len, void *user)
{
  static_cast<std::string *>(user)->append(bytes, len);
}

void test_construction_is_idle()
{
  Terminal t;
  expect(!t.isRunning(), "freshly constructed Terminal is not running");
}

void test_start_and_stop()
{
  Terminal t;
  // /tmp is a sane cwd on every supported platform.
  std::filesystem::path cwd = std::filesystem::temp_directory_path();
  t.start(cwd, 24, 80);
  expect(t.isRunning(), "Terminal reports running after start()");
  t.stop();
  // stop() is synchronous from the caller's perspective: by the time it
  // returns the reader thread has been joined.
  expect(!t.isRunning(), "Terminal reports not running after stop()");
}

void test_double_start_is_safe()
{
  Terminal t;
  std::filesystem::path cwd = std::filesystem::temp_directory_path();
  t.start(cwd, 24, 80);
  expect(t.isRunning(), "first start() succeeds");
  t.start(cwd, 24, 80);
  expect(t.isRunning(), "second start() re-uses the session");
  t.stop();
}

void test_stop_without_start_is_safe()
{
  Terminal t;
  t.stop();
  expect(!t.isRunning(), "stop() on a non-running Terminal is a no-op");
  expect(t.sessionCount() == 0, "stopping an empty terminal preserves the zero-session state");
}

void test_multiple_sessions_are_independent()
{
  Terminal t;
  const std::filesystem::path cwd = std::filesystem::temp_directory_path();
  const Terminal::SessionId first = t.addSession(cwd);
  const Terminal::SessionId second = t.addSession(cwd);

  expect(first != second, "terminal sessions receive stable unique IDs");
  expect(t.sessionCount() == 2, "two terminal sessions can be created");
  expect(t.isSessionRunning(first), "first terminal session is running");
  expect(t.isSessionRunning(second), "second terminal session is running");
  expect(t.closeSession(first), "an existing terminal session can be closed");
  expect(t.sessionCount() == 1, "closing one session leaves the other session");
  expect(t.isSessionRunning(second), "closing one session does not stop the other shell");

  t.stop();
  expect(t.sessionCount() == 0, "stop removes every terminal session");
  expect(!t.isRunning(), "stop shuts down every terminal shell");
  expect(!t.closeSession(second), "closing a session after stop is a safe no-op");
}

void test_resize_does_not_crash()
{
  Terminal t;
  std::filesystem::path cwd = std::filesystem::temp_directory_path();
  t.start(cwd, 24, 80);
  t.resize(40, 120);
  t.stop();
}

#if !defined(_WIN32)
std::string read_pty_for(notepp::terminal::PtyBackend &pty, std::chrono::milliseconds duration)
{
  std::string output;
  const auto deadline = std::chrono::steady_clock::now() + duration;
  std::array<char, 4096> buffer{};
  while(std::chrono::steady_clock::now() < deadline)
  {
    struct pollfd descriptor
    {
      pty.readHandle(), POLLIN, 0
    };
    const int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               deadline - std::chrono::steady_clock::now())
                                               .count());
    const int result = ::poll(&descriptor, 1, std::max(1, std::min(remaining, 25)));
    if(result <= 0 || (descriptor.revents & POLLIN) == 0) continue;
    const int count = pty.read(buffer.data(), buffer.size());
    if(count <= 0) break;
    output.append(buffer.data(), static_cast<std::size_t>(count));
  }
  return output;
}

std::size_t count_occurrences(std::string_view text, std::string_view needle)
{
  if(needle.empty()) return 0;
  std::size_t count = 0;
  std::size_t offset = 0;
  while((offset = text.find(needle, offset)) != std::string_view::npos)
  {
    ++count;
    offset += needle.size();
  }
  return count;
}

void test_shell_line_controls_end_to_end()
{
  std::unique_ptr<notepp::terminal::PtyBackend> pty(notepp::terminal::createPtyBackend());
  expect(pty != nullptr, "PTY backend is available for the integration harness");
  if(pty == nullptr) return;

  const std::filesystem::path cwd = std::filesystem::temp_directory_path();
  expect(pty->start(cwd, "/bin/bash", 24, 100), "bash starts for the terminal input harness");
  if(!pty->isRunning()) return;

  (void)read_pty_for(*pty, std::chrono::milliseconds(100));
  const std::string marker = "NOTEPP_HISTORY_MARKER";
  (void)pty->write("printf '" + marker + "\\n'\n");
  std::string output = read_pty_for(*pty, std::chrono::milliseconds(300));
  expect(output.find(marker) != std::string::npos, "bash executes a baseline command");

  // Bash Readline history: Up recalls the previous command and Enter executes it.
  (void)pty->write("\x1b[A\r");
  output = read_pty_for(*pty, std::chrono::milliseconds(300));
  expect(count_occurrences(output, marker) >= 2, "Up plus Enter recalls and executes history");

  // Bash Readline reverse search: Ctrl+R, query, Enter executes the match.
  (void)pty->write("\x12" + marker + "\r");
  output = read_pty_for(*pty, std::chrono::milliseconds(300));
  expect(count_occurrences(output, marker) >= 2, "Ctrl+R searches and executes a history match");

  const std::filesystem::path completion_file = cwd / "notepp_completion_target";
  const std::string completion_path = completion_file.string();
  (void)pty->write("printf 'NOTEPP_TAB_MARKER\\n' > " + completion_path + "\n");
  (void)read_pty_for(*pty, std::chrono::milliseconds(200));
  (void)pty->write("cat " + (cwd / "notepp_completion_").string() + "\t\r");
  output = read_pty_for(*pty, std::chrono::milliseconds(300));
  expect(output.find("NOTEPP_TAB_MARKER") != std::string::npos, "Tab completes a file name");
  std::error_code remove_error;
  std::filesystem::remove(completion_file, remove_error);
  pty->stop();
}

void test_natural_shell_exit_is_reaped()
{
  Terminal t;
  const Terminal::SessionId id = t.addSession(std::filesystem::temp_directory_path());
  t.write("exit\n");

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while(t.isSessionRunning(id) && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

  expect(!t.isSessionRunning(id), "natural shell exit finalizes the terminal session");
  int status = 0;
  errno = 0;
  const pid_t child = ::waitpid(-1, &status, WNOHANG);
  expect(child == -1 && errno == ECHILD, "natural shell exit is reaped without closing its tab");
  t.stop();
}
#endif

void test_selection_range()
{
  using notepp::terminal_detail::TerminalCell;
  using notepp::terminal_detail::terminalSelectionContains;

  expect(terminalSelectionContains(TerminalCell{1, 2}, TerminalCell{1, 2}, 1, 2),
         "a single terminal cell can be selected");
  expect(!terminalSelectionContains(TerminalCell{1, 2}, TerminalCell{1, 2}, 1, 3),
         "a single-cell selection excludes adjacent cells");
  expect(terminalSelectionContains(TerminalCell{2, 5}, TerminalCell{0, 3}, 1, 0),
         "a reversed drag selects cells on intermediate rows");
  expect(terminalSelectionContains(TerminalCell{2, 5}, TerminalCell{0, 3}, 0, 3),
         "a reversed drag includes its normalized start cell");
  expect(!terminalSelectionContains(TerminalCell{-1, -1}, TerminalCell{0, 0}, 0, 0),
         "an empty selection contains no cells");
}

void test_clipboard_shortcut_classification()
{
  using notepp::terminal_detail::ClipboardAction;
  using notepp::terminal_detail::ClipboardKey;
  using notepp::terminal_detail::terminalClipboardAction;
  expect(terminalClipboardAction(ClipboardKey::C, true, true, false, false) ==
             ClipboardAction::Copy,
         "Ctrl+Shift+C copies terminal selection");
  expect(terminalClipboardAction(ClipboardKey::V, true, true, false, false) ==
             ClipboardAction::Paste,
         "Ctrl+Shift+V pastes once");
  expect(terminalClipboardAction(ClipboardKey::Insert, true, false, false, false) ==
             ClipboardAction::Copy,
         "Ctrl+Insert copies terminal selection");
  expect(terminalClipboardAction(ClipboardKey::Insert, false, true, false, false) ==
             ClipboardAction::Paste,
         "Shift+Insert pastes once");
  expect(terminalClipboardAction(ClipboardKey::V, false, false, false, true) ==
             ClipboardAction::Paste,
         "Command+V follows macOS paste convention");
  expect(terminalClipboardAction(ClipboardKey::C, true, false, false, false) ==
             ClipboardAction::None,
         "Ctrl+C remains a shell interrupt");
}

void test_special_key_mapping()
{
  using notepp::terminal_detail::terminalKeyFromImGui;
  expect(terminalKeyFromImGui(ImGuiKey_Tab) == VTERM_KEY_TAB, "Tab maps to terminal Tab");
  expect(terminalKeyFromImGui(ImGuiKey_LeftArrow) == VTERM_KEY_LEFT, "Left maps to terminal Left");
  expect(terminalKeyFromImGui(ImGuiKey_RightArrow) == VTERM_KEY_RIGHT, "Right maps to terminal Right");
  expect(terminalKeyFromImGui(ImGuiKey_Enter) == VTERM_KEY_ENTER, "Enter maps to terminal Enter");
  expect(terminalKeyFromImGui(ImGuiKey_KeypadEnter) == VTERM_KEY_ENTER, "keypad Enter maps to terminal Enter");
  expect(terminalKeyFromImGui(ImGuiKey_F1) == static_cast<VTermKey>(VTERM_KEY_FUNCTION(1)), "F1 maps to terminal F1");
  expect(terminalKeyFromImGui(ImGuiKey_F12) == static_cast<VTermKey>(VTERM_KEY_FUNCTION(12)), "F12 maps to terminal F12");

  const uint32_t ctrl_c = notepp::terminal_detail::terminalControlCharacterFromImGui(ImGuiKey_C);
  expect(ctrl_c == static_cast<uint32_t>('c'), "Ctrl+C mapping passes printable c to libvterm");
  const uint32_t ctrl_r = notepp::terminal_detail::terminalControlCharacterFromImGui(ImGuiKey_R);
  expect(ctrl_r == static_cast<uint32_t>('r'), "Ctrl+R mapping passes printable r to libvterm");

  std::string output;
  VTerm *vt = vterm_new(24, 80);
  // Cursor keys consult the vterm state (unlike literal Tab), so initialize
  // the same state that Session::start() obtains before forwarding keys.
  (void)vterm_obtain_state(vt);
  vterm_output_set_callback(vt, captureVtermOutput, &output);
  vterm_keyboard_unichar(vt, ctrl_c, VTERM_MOD_CTRL);
  expect(output == std::string(1, '\x03'), "libvterm emits byte 0x03 for Ctrl+C");

  output.clear();
  vterm_keyboard_unichar(vt, ctrl_r, VTERM_MOD_CTRL);
  expect(output == std::string(1, '\x12'), "libvterm emits byte 0x12 for Ctrl+R");

  output.clear();
  vterm_keyboard_key(vt, VTERM_KEY_TAB, VTERM_MOD_NONE);
  expect(output == std::string(1, '\t'), "libvterm emits byte 0x09 for Tab completion");

  output.clear();
  vterm_keyboard_key(vt, VTERM_KEY_LEFT, VTERM_MOD_NONE);
  expect(output == "\x1b[D", "libvterm emits CSI D for the left arrow");

  output.clear();
  vterm_keyboard_key(vt, VTERM_KEY_RIGHT, VTERM_MOD_NONE);
  expect(output == "\x1b[C", "libvterm emits CSI C for the right arrow");
  vterm_free(vt);

  VTerm *screen_vt = vterm_new(2, 20);
  VTermScreen *screen = vterm_obtain_screen(screen_vt);
  vterm_screen_reset(screen, 1);
  const std::string marker = "copy-marker";
  vterm_input_write(screen_vt, marker.data(), marker.size());
  char copied[64] = {};
  const VTermRect rect{0, 2, 0, 20};
  const std::size_t copied_length = vterm_screen_get_text(screen, copied, sizeof(copied), rect);
  expect(copied_length >= marker.size() && std::string(copied, copied_length).find(marker) != std::string::npos,
         "screen text extraction uses row/column rectangle ordering");
  vterm_free(screen_vt);
}

void test_first_render_does_not_crash()
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1280.0f, 720.0f);
  io.DeltaTime = 1.0f / 60.0f;
  unsigned char *pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  Terminal t;
  t.start(std::filesystem::temp_directory_path(), 24, 80);
  std::this_thread::sleep_for(std::chrono::milliseconds(25));

  // Model the opening shortcut already queued in ImGui. Ctrl+D has an
  // observable side effect (shell EOF), so it verifies that all keyboard
  // input is ignored on the hidden-to-visible frame.
  io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
  io.AddKeyEvent(ImGuiKey_D, true);
  ImGui::NewFrame();
  bool open = true;
  t.render(&open, {}, false);
  ImGui::Render();
  io.AddKeyEvent(ImGuiKey_D, false);
  io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  expect(t.hasFocus(), "first render gives the terminal keyboard focus");
  expect(t.isRunning(), "opening-frame keyboard input is not sent to the shell");

  t.releaseFocus();
  expect(!t.hasFocus(), "hiding the terminal explicitly releases keyboard focus");

  // App buffers SDL_TEXTINPUT and passes it to render(); validate that a
  // printable command can traverse the focused terminal while the first
  // rendered session remains alive.
  io.AddKeyEvent(ImGuiKey_Enter, true);
  ImGui::NewFrame();
  t.render(&open, "printf input-ok");
  ImGui::Render();
  io.AddKeyEvent(ImGuiKey_Enter, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  ImGui::NewFrame();
  t.render(&open);
  ImGui::Render();
  expect(t.isRunning(), "printable input keeps terminal session running");

  // Current-frame focus must report false after another window takes focus;
  // App uses this to leave SDL text with the markdown editor.
  ImGui::NewFrame();
  ImGui::SetNextWindowFocus();
  ImGui::Begin("Other window");
  ImGui::End();
  t.render(&open);
  ImGui::Render();
  expect(!t.hasFocus(), "terminal releases keyboard input after focus moves away");

  // All special keys used by interactive shells must be safe to forward.
  t.sendKey(ImGuiKey_Tab, false, false, false);
  t.sendKey(ImGuiKey_LeftArrow, false, false, false);
  t.sendKey(ImGuiKey_RightArrow, false, false, false);
  t.sendKey(ImGuiKey_KeypadEnter, false, false, false);

  t.stop();
  ImGui::NewFrame();
  t.render(&open);
  ImGui::Render();
  expect(t.sessionCount() == 0, "rendering the zero-session state is safe");

  ImGui::DestroyContext();
  expect(open, "first render leaves the terminal window open");
}
} // namespace

int main()
{
  test_construction_is_idle();
  test_start_and_stop();
  test_double_start_is_safe();
  test_stop_without_start_is_safe();
  test_multiple_sessions_are_independent();
  test_resize_does_not_crash();
#if !defined(_WIN32)
  test_shell_line_controls_end_to_end();
  test_natural_shell_exit_is_reaped();
#endif
  test_selection_range();
  test_clipboard_shortcut_classification();
  test_special_key_mapping();
  test_first_render_does_not_crash();
  if(failures != 0)
  {
    std::cerr << failures << " terminal test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "terminal tests passed\n";
  return EXIT_SUCCESS;
}
