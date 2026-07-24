#include "terminal.hpp"

#include "pty.hpp"
#include "terminal_key_map.hpp"

#include <vterm.h>
#include <vterm_keycodes.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{

// Default xterm 256-color palette. Hardcoded because libvterm only stores
// the entries the program has overridden.
constexpr std::array<int, 16> kAnsi16 = {
    0x000000,
    0xcd0000,
    0x00cd00,
    0xcdcd00,
    0x0000ee,
    0xcd00cd,
    0x00cdcd,
    0xe5e5e5,
    0x7f7f7f,
    0xff0000,
    0x00ff00,
    0xffff00,
    0x5c5cff,
    0xff00ff,
    0x00ffff,
    0xffffff,
};

ImU32 indexed256(int idx)
{
  if(idx < 16) return IM_COL32((kAnsi16[idx] >> 16) & 0xFF, (kAnsi16[idx] >> 8) & 0xFF, kAnsi16[idx] & 0xFF, 0xFF);
  if(idx >= 232)
  {
    const int level = 8 + 10 * (idx - 232);
    return IM_COL32(level, level, level, 0xFF);
  }
  idx -= 16;
  const int r = idx / 36;
  const int g = (idx / 6) % 6;
  const int b = idx % 6;
  const int levels[6] = {0, 95, 135, 175, 215, 255};
  return IM_COL32(levels[r], levels[g], levels[b], 0xFF);
}

ImU32 colorToU32(const VTermColor &col, const VTermColor &defaultFg, const VTermColor &defaultBg, bool wantBg)
{
  if(wantBg ? VTERM_COLOR_IS_DEFAULT_BG(&col) : VTERM_COLOR_IS_DEFAULT_FG(&col))
  {
    const VTermColor &fallback = wantBg ? defaultBg : defaultFg;
    if(VTERM_COLOR_IS_RGB(&fallback))
    {
      return IM_COL32(fallback.rgb.red, fallback.rgb.green, fallback.rgb.blue, 0xFF);
    }
    if(VTERM_COLOR_IS_INDEXED(&fallback))
    {
      return indexed256(fallback.indexed.idx);
    }
    return wantBg ? IM_COL32(0, 0, 0, 0xFF) : IM_COL32(0xE5, 0xE5, 0xE5, 0xFF);
  }
  if(VTERM_COLOR_IS_RGB(&col))
  {
    return IM_COL32(col.rgb.red, col.rgb.green, col.rgb.blue, 0xFF);
  }
  if(VTERM_COLOR_IS_INDEXED(&col))
  {
    return indexed256(col.indexed.idx);
  }
  return wantBg ? IM_COL32(0, 0, 0, 0xFF) : IM_COL32(0xE5, 0xE5, 0xE5, 0xFF);
}

} // namespace

struct Terminal::Impl
{
  notepp::terminal::PtyBackend *pty = nullptr;

  VTerm *vt = nullptr;
  VTermScreen *screen = nullptr;

  // Default colors captured after vterm is created. vterm may flip the
  // DEFAULT_FG/DEFAULT_BG type bits depending on terminfo, so reading them
  // back out and resolving to RGB is the only safe way to get a stable
  // background for the empty cells.
  VTermColor defaultFg{};
  VTermColor defaultBg{};

  // Cached cursor position (read every frame from vterm).
  VTermPos cursor{};

  // Thread + cross-thread byte buffer for PTY -> vterm direction.
  std::thread reader;
  std::mutex buffer_mutex;
  std::vector<uint8_t> read_buffer;
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> running{false};

  // Last writer buffer for vterm -> PTY direction.
  std::mutex write_mutex;

  int rows = 24;
  int cols = 80;
  ImFont *font = nullptr;
  bool first_frame = true;
  bool focused = false;
  bool was_focused = false;
};

// ── vterm → PTY (output callback) ─────────────────────────────────────
void vtermOutputCb(const char *s, size_t len, void *user)
{
  auto *self = static_cast<Terminal::Impl *>(user);
  std::lock_guard<std::mutex> lock(self->write_mutex);
  self->pty->write(std::string_view(s, len));
}

// ── screen callbacks: we redraw every frame, so these are no-ops ──────
static int screenDamageCb(VTermRect, void *) { return 1; }
static int screenMoveRectCb(VTermRect, VTermRect, void *) { return 1; }
static int screenMoveCursorCb(VTermPos, VTermPos, int, void *) { return 1; }
static int screenSetTermPropCb(VTermProp, VTermValue *, void *) { return 1; }
static int screenBellCb(void *) { return 1; }
static int screenResizeCb(int, int, void *) { return 1; }
static int screenSbPushLineCb(int, const VTermScreenCell *, void *) { return 0; }
static int screenSbPopLineCb(int, VTermScreenCell *, void *) { return 0; }
static int screenSbClearCb(void *) { return 1; }

static const VTermScreenCallbacks kScreenCallbacks = {
    screenDamageCb,
    screenMoveRectCb,
    screenMoveCursorCb,
    screenSetTermPropCb,
    screenBellCb,
    screenResizeCb,
    screenSbPushLineCb,
    screenSbPopLineCb,
    screenSbClearCb,
};

// ── reader thread: PTY → vterm byte buffer ───────────────────────────
void readerLoop(Terminal::Impl *self)
{
  if(self->pty == nullptr) return;

  std::array<uint8_t, 4096> buf{};
  while(!self->stop_requested.load(std::memory_order_acquire))
  {
    const int n = self->pty->read(buf.data(), buf.size());
    if(n > 0)
    {
      std::lock_guard<std::mutex> lock(self->buffer_mutex);
      self->read_buffer.insert(self->read_buffer.end(), buf.data(), buf.data() + n);
      continue;
    }
    if(n == 0)
    {
      // EOF: shell closed the slave (or the pipe was closed by stop()).
      break;
    }
    // n < 0: a real error.
    break;
  }
  self->running.store(false, std::memory_order_release);
}

Terminal::Terminal()
    : impl_(new Impl())
{
}

Terminal::~Terminal()
{
  stop();
  delete impl_;
}

void Terminal::start(const std::filesystem::path &cwd, int rows, int cols)
{
  stop();

  impl_->rows = rows > 0 ? rows : 24;
  impl_->cols = cols > 0 ? cols : 80;
  impl_->stop_requested.store(false, std::memory_order_relaxed);
  impl_->read_buffer.clear();

  if(impl_->pty == nullptr) impl_->pty = notepp::terminal::createPtyBackend();
  if(impl_->pty == nullptr) return;

  // Determine the shell to spawn.
  std::string shell;
  if(const char *sh = std::getenv("SHELL"); sh != nullptr && *sh != '\0')
    shell = sh;
  else
    shell = "/bin/sh";

  if(!impl_->pty->start(cwd, shell, impl_->rows, impl_->cols))
  {
    return;
  }

  // Build vterm and screen.
  impl_->vt = vterm_new(impl_->rows, impl_->cols);
  if(impl_->vt == nullptr)
  {
    impl_->pty->stop();
    return;
  }
  vterm_set_utf8(impl_->vt, 1);
  vterm_output_set_callback(impl_->vt, vtermOutputCb, impl_);

  impl_->screen = vterm_obtain_screen(impl_->vt);
  vterm_screen_set_callbacks(impl_->screen, &kScreenCallbacks, impl_);
  vterm_screen_enable_altscreen(impl_->screen, 1);
  vterm_screen_set_damage_merge(impl_->screen, VTERM_DAMAGE_SCROLL);

  // vterm_new() allocates state but deliberately does not initialise its
  // active encodings, pen, cursor, or screen buffers. Upstream's documented
  // setup sequence resets the screen before feeding the first PTY byte.
  // Without this, the first printable shell-prompt character dereferences a
  // null encoding pointer in libvterm state.c:on_text().
  vterm_screen_reset(impl_->screen, 1);

  // Initialise the default colors so we can render background cells cleanly.
  vterm_state_get_default_colors(vterm_obtain_state(impl_->vt), &impl_->defaultFg, &impl_->defaultBg);
  vterm_state_convert_color_to_rgb(vterm_obtain_state(impl_->vt), &impl_->defaultFg);
  vterm_state_convert_color_to_rgb(vterm_obtain_state(impl_->vt), &impl_->defaultBg);

  impl_->running.store(true, std::memory_order_release);
  impl_->reader = std::thread(readerLoop, impl_);
}

void Terminal::stop()
{
  impl_->stop_requested.store(true, std::memory_order_release);

  // Closing the PTY first interrupts the reader's read/poll loop. Joining
  // before closing would deadlock whenever the shell was idle.
  if(impl_->pty != nullptr) impl_->pty->stop();
  if(impl_->reader.joinable()) impl_->reader.join();

  if(impl_->screen != nullptr)
  {
    vterm_screen_set_callbacks(impl_->screen, nullptr, nullptr);
    impl_->screen = nullptr;
  }
  if(impl_->vt != nullptr)
  {
    vterm_free(impl_->vt);
    impl_->vt = nullptr;
  }

  impl_->running.store(false, std::memory_order_release);
}

bool Terminal::isRunning() const noexcept
{
  return impl_->running.load(std::memory_order_acquire);
}

bool Terminal::hasFocus() const noexcept
{
  return impl_->focused;
}

void Terminal::resize(int rows, int cols)
{
  if(rows <= 0 || cols <= 0) return;
  if(rows == impl_->rows && cols == impl_->cols) return;
  impl_->rows = rows;
  impl_->cols = cols;
  if(impl_->vt != nullptr) vterm_set_size(impl_->vt, rows, cols);
  if(impl_->pty != nullptr) impl_->pty->resize(rows, cols);
}

void Terminal::write(std::string_view bytes)
{
  if(impl_->pty == nullptr) return;
  std::lock_guard<std::mutex> lock(impl_->write_mutex);
  impl_->pty->write(bytes);
}

void Terminal::sendKey(int imguiKey, bool ctrl, bool shift, bool alt)
{
  if(impl_->vt == nullptr) return;

  VTermModifier mod = VTERM_MOD_NONE;
  if(shift) mod = static_cast<VTermModifier>(mod | VTERM_MOD_SHIFT);
  if(alt) mod = static_cast<VTermModifier>(mod | VTERM_MOD_ALT);
  if(ctrl) mod = static_cast<VTermModifier>(mod | VTERM_MOD_CTRL);

  const VTermKey key = notepp::terminal_detail::terminalKeyFromImGui(imguiKey);
  if(key != VTERM_KEY_NONE) vterm_keyboard_key(impl_->vt, key, mod);
}

void Terminal::setFont(ImFont *font) noexcept
{
  impl_->font = font;
}

void Terminal::render(bool *open, std::string_view pendingText)
{
  if(open == nullptr || !*open) return;

  // ── Window chrome ──────────────────────────────────────────────────
  // Center the window in the main viewport on first use so it cannot land
  // at (0,0) under the Explorer sidebar. The flag set mirrors the search
  // dialog: not dockable, not collapsible, no saved settings — a
  // workspace-level tool.
  if(impl_->first_frame)
  {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowFocus();
  }
  ImGui::SetNextWindowSize(ImVec2(720.0f, 420.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(1.0f);
  if(!ImGui::Begin("Terminal##notepp", open,
                   ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoDocking |
                       ImGuiWindowFlags_NoSavedSettings))
  {
    ImGui::End();
    return;
  }

  if(!impl_->running.load(std::memory_order_acquire) || impl_->vt == nullptr || impl_->screen == nullptr)
  {
    ImGui::TextDisabled("Terminal not running. Close (Esc) and press Ctrl+Shift+P to start a new session.");
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
      if(open != nullptr) *open = false;
    }
    ImGui::End();
    return;
  }

  // ── Drain bytes the reader thread has accumulated ──────────────────
  std::vector<uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock(impl_->buffer_mutex);
    bytes.swap(impl_->read_buffer);
  }
  if(!bytes.empty())
  {
    vterm_input_write(impl_->vt, reinterpret_cast<const char *>(bytes.data()), bytes.size());
  }
  vterm_screen_flush_damage(impl_->screen);
  vterm_state_get_cursorpos(vterm_obtain_state(impl_->vt), &impl_->cursor);

  // Focus tracking: tell the emulator whether we are the focused window
  // so applications that react to focus events (e.g. stop cursor blinking)
  // behave correctly.
  const bool now_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  if(now_focused != impl_->was_focused)
  {
    VTermState *state = vterm_obtain_state(impl_->vt);
    if(now_focused)
      vterm_state_focus_in(state);
    else
      vterm_state_focus_out(state);
    impl_->was_focused = now_focused;
  }

  // ── Compute grid metrics from the current ImGui content region ───
  ImFont *font = impl_->font != nullptr ? impl_->font : ImGui::GetFont();
  const float font_size = font->FontSize > 0.0f ? font->FontSize : ImGui::GetFontSize();
  const float char_w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, "M").x;
  const float char_h = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, "Mg").y;
  const float cell_w = char_w > 0.0f ? char_w : 8.0f;
  const float cell_h = char_h > 0.0f ? char_h : 14.0f;

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const int cols = std::max(1, (int)(avail.x / cell_w));
  const int rows = std::max(1, (int)(avail.y / cell_h));
  if(cols != impl_->cols || rows != impl_->rows)
  {
    resize(rows, cols);
  }

  // ── Render cells ──────────────────────────────────────────────────
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 content_min = origin;
  const ImVec2 content_max(origin.x + cols * cell_w, origin.y + rows * cell_h);

  // Background: solid fill of the default background color.
  const ImU32 bg_default = colorToU32(impl_->defaultBg, impl_->defaultFg, impl_->defaultBg, true);
  draw->AddRectFilled(content_min, content_max, bg_default);

  VTermScreenCell cell{};
  char utf8[8] = {};

  for(int r = 0; r < rows; ++r)
  {
    for(int c = 0; c < cols; ++c)
    {
      VTermPos pos{r, c};
      if(vterm_screen_get_cell(impl_->screen, pos, &cell) == 0) continue;
      if(cell.width == 0) continue; // combining mark — can't draw cleanly

      const ImVec2 cell_min(origin.x + c * cell_w, origin.y + r * cell_h);
      const ImVec2 cell_max(cell_min.x + cell_w * (cell.width > 0 ? cell.width : 1), cell_min.y + cell_h);

      // Per-cell background.
      const ImU32 cell_bg = colorToU32(cell.bg, impl_->defaultFg, impl_->defaultBg, true);
      if(cell_bg != bg_default) draw->AddRectFilled(cell_min, cell_max, cell_bg);

      // Character.
      const uint32_t cp = cell.chars[0];
      if(cp == 0) continue;
      // ImTextCharToUtf8 already writes a NUL terminator.
      (void)ImTextCharToUtf8(utf8, cp);

      const ImU32 cell_fg = colorToU32(cell.fg, impl_->defaultFg, impl_->defaultBg, false);
      draw->AddText(font, font_size, cell_min, cell_fg, utf8);
    }
  }

  // Cursor.
  if(impl_->cursor.row >= 0 && impl_->cursor.row < rows && impl_->cursor.col >= 0 && impl_->cursor.col < cols)
  {
    const ImVec2 cur_min(origin.x + impl_->cursor.col * cell_w, origin.y + impl_->cursor.row * cell_h);
    const ImVec2 cur_max(cur_min.x + cell_w, cur_min.y + cell_h);
    const ImU32 cell_fg_default = colorToU32(impl_->defaultFg, impl_->defaultFg, impl_->defaultBg, false);
    draw->AddRectFilled(cur_min, cur_max, cell_fg_default);
  }

  // Reserve the content region so ImGui lays out the rest of the window.
  ImGui::Dummy(ImVec2(cols * cell_w, rows * cell_h));

  // ── Keyboard input (only when this window is focused) ─────────────
  const bool terminal_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  impl_->focused = terminal_focused;
  if(terminal_focused)
  {
    // SDL_TEXTINPUT events precede the Enter keydown generated for a typed
    // command. Preserve that ordering so a fast "ls" + Enter cannot execute
    // an empty line and leave "ls" waiting at the next prompt.
    if(!pendingText.empty()) write(pendingText);

    const ImGuiIO &io = ImGui::GetIO();
    const bool ctrl_down = io.KeyCtrl;
    const bool shift_down = io.KeyShift;
    const bool alt_down = io.KeyAlt;

    // Special non-text keys (Tab, arrows, F-keys, navigation, editing).
    for(int k = ImGuiKey_Tab; k <= ImGuiKey_F12; ++k)
    {
      if(ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false))
      {
        sendKey(k, ctrl_down, shift_down, alt_down);
      }
    }
    if(ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
      sendKey(ImGuiKey_KeypadEnter, ctrl_down, shift_down, alt_down);

    // Ctrl + A..Z: send the matching control byte (0x01..0x1A). The shell
    // interprets these as e.g. SIGINT (^C), EOF (^D), clear (^L), etc.
    if(ctrl_down && !alt_down)
    {
      for(int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k)
      {
        if(ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false))
        {
          // libvterm expects the printable lowercase letter plus CTRL and
          // performs the 0x1f translation itself (e.g. 'c' -> 0x03).
          const uint32_t letter = notepp::terminal_detail::terminalControlCharacterFromImGui(k);
          vterm_keyboard_unichar(impl_->vt, letter, VTERM_MOD_CTRL);
        }
      }
    }
  }

  impl_->first_frame = false;
  ImGui::End();
}
