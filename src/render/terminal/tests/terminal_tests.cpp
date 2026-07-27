// Headless tests for the embedded terminal.
//
// The Terminal class is the only thing we exercise here. Tests are
// limited to behaviour that does not require an ImGui context (i.e. they
// touch the PTY + libvterm plumbing, not the render() method).

#include "terminal.hpp"
#include "terminal_key_map.hpp"

#include <imgui.h>
#include <vterm.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <cerrno>
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

  std::string output;
  VTerm *vt = vterm_new(24, 80);
  vterm_output_set_callback(vt, captureVtermOutput, &output);
  vterm_keyboard_unichar(vt, ctrl_c, VTERM_MOD_CTRL);
  expect(output == std::string(1, '\x03'), "libvterm emits byte 0x03 for Ctrl+C");

  output.clear();
  vterm_keyboard_key(vt, VTERM_KEY_TAB, VTERM_MOD_NONE);
  expect(output == std::string(1, '\t'), "libvterm emits byte 0x09 for Tab completion");
  vterm_free(vt);
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
  test_natural_shell_exit_is_reaped();
#endif
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
