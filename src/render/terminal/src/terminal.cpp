#include "terminal.hpp"

#include "pty.hpp"
#include "terminal_key_map.hpp"
#include "terminal_selection.hpp"

#include <vterm.h>
#include <vterm_keycodes.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{

constexpr std::size_t kMaxPendingOutputBytes = 4U * 1024U * 1024U;

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
  constexpr int kLevels[6] = {0, 95, 135, 175, 215, 255};
  return IM_COL32(kLevels[r], kLevels[g], kLevels[b], 0xFF);
}

ImU32 colorToU32(const VTermColor &col, const VTermColor &defaultFg, const VTermColor &defaultBg, bool wantBg)
{
  if(wantBg ? VTERM_COLOR_IS_DEFAULT_BG(&col) : VTERM_COLOR_IS_DEFAULT_FG(&col))
  {
    const VTermColor &fallback = wantBg ? defaultBg : defaultFg;
    if(VTERM_COLOR_IS_RGB(&fallback))
      return IM_COL32(fallback.rgb.red, fallback.rgb.green, fallback.rgb.blue, 0xFF);
    if(VTERM_COLOR_IS_INDEXED(&fallback)) return indexed256(fallback.indexed.idx);
    return wantBg ? IM_COL32(0, 0, 0, 0xFF) : IM_COL32(0xE5, 0xE5, 0xE5, 0xFF);
  }
  if(VTERM_COLOR_IS_RGB(&col)) return IM_COL32(col.rgb.red, col.rgb.green, col.rgb.blue, 0xFF);
  if(VTERM_COLOR_IS_INDEXED(&col)) return indexed256(col.indexed.idx);
  return wantBg ? IM_COL32(0, 0, 0, 0xFF) : IM_COL32(0xE5, 0xE5, 0xE5, 0xFF);
}

std::string shellCommand()
{
  if(const char *shell = std::getenv("SHELL"); shell != nullptr && *shell != '\0')
  {
#if !defined(_WIN32)
    const std::string configured(shell);
    const std::string name = std::filesystem::path(configured).filename().string();
    // /bin/sh and dash intentionally provide no Readline history, reverse
    // search, or completion. Prefer Bash for the embedded interactive
    // terminal so its controls behave like a normal desktop terminal.
    if((name == "sh" || name == "dash") && std::filesystem::exists("/bin/bash"))
      return "/bin/bash";
#endif
    return shell;
  }
#if defined(_WIN32)
  return {};
#else
  return std::filesystem::exists("/bin/bash") ? "/bin/bash" : "/bin/sh";
#endif
}

} // namespace

struct Terminal::Impl
{
  struct Session
  {
    Session(SessionId sessionId, std::string sessionTitle, std::filesystem::path workingDirectory)
        : id(sessionId), title(std::move(sessionTitle)), cwd(std::move(workingDirectory))
    {
    }

    ~Session()
    {
      stop();
    }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    static void outputCallback(const char *bytes, size_t length, void *user)
    {
      auto *self = static_cast<Session *>(user);
      if(self->pty == nullptr) return;
      std::lock_guard<std::mutex> lock(self->write_mutex);
      self->pty->write(std::string_view(bytes, length));
    }

    void readerLoop()
    {
      if(pty == nullptr) return;

      std::array<std::uint8_t, 4096> bytes{};
      while(!stop_requested.load(std::memory_order_acquire))
      {
        const int count = pty->read(bytes.data(), bytes.size());
        if(count <= 0) break;

        const auto byteCount = static_cast<std::size_t>(count);
        std::lock_guard<std::mutex> lock(buffer_mutex);
        const std::size_t required = read_buffer.size() + byteCount;
        if(required > kMaxPendingOutputBytes)
        {
          const std::size_t eraseCount = std::min(read_buffer.size(), kMaxPendingOutputBytes / 2U);
          read_buffer.erase(read_buffer.begin(), read_buffer.begin() + static_cast<std::ptrdiff_t>(eraseCount));
        }
        read_buffer.insert(read_buffer.end(), bytes.begin(), bytes.begin() + count);
      }

      // Natural EOF/error means the shell has finished. Finalize the backend
      // here so child processes are reaped and platform handles are released
      // even while the exited tab remains open. The write mutex serializes
      // this with UI-thread writes, resize, restart, and explicit teardown.
      if(!stop_requested.load(std::memory_order_acquire))
      {
        std::lock_guard<std::mutex> lock(write_mutex);
        if(!stop_requested.load(std::memory_order_acquire) && pty != nullptr)
          pty->stop();
      }
      running.store(false, std::memory_order_release);
      exited_naturally = true;
    }

    bool start(int initialRows, int initialCols)
    {
      stop();

      rows = std::max(1, initialRows);
      cols = std::max(1, initialCols);
      stop_requested.store(false, std::memory_order_relaxed);
      exited_naturally = false;
      {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        read_buffer.clear();
      }
      scrollback.clear();
      scroll_offset = 0;

      if(pty == nullptr) pty.reset(notepp::terminal::createPtyBackend());
      if(pty == nullptr || !pty->start(cwd, shellCommand(), rows, cols)) return false;

      vt = vterm_new(rows, cols);
      if(vt == nullptr)
      {
        pty->stop();
        return false;
      }
      vterm_set_utf8(vt, 1);
      vterm_output_set_callback(vt, outputCallback, this);

      screen = vterm_obtain_screen(vt);
      vterm_screen_set_callbacks(screen, &kScreenCallbacks, this);
      vterm_screen_enable_altscreen(screen, 1);
      vterm_screen_set_damage_merge(screen, VTERM_DAMAGE_SCROLL);
      vterm_screen_reset(screen, 1);

      vterm_state_get_default_colors(vterm_obtain_state(vt), &defaultFg, &defaultBg);
      vterm_state_convert_color_to_rgb(vterm_obtain_state(vt), &defaultFg);
      vterm_state_convert_color_to_rgb(vterm_obtain_state(vt), &defaultBg);

      running.store(true, std::memory_order_release);
      reader = std::thread([this]() { readerLoop(); });
      return true;
    }

    void stop()
    {
      clearSelection();
      stop_requested.store(true, std::memory_order_release);
      {
        std::lock_guard<std::mutex> lock(write_mutex);
        if(pty != nullptr) pty->interruptRead();
      }
      if(reader.joinable()) reader.join();
      {
        std::lock_guard<std::mutex> lock(write_mutex);
        if(pty != nullptr) pty->stop();
      }

      if(screen != nullptr)
      {
        vterm_screen_set_callbacks(screen, nullptr, nullptr);
        screen = nullptr;
      }
      if(vt != nullptr)
      {
        vterm_free(vt);
        vt = nullptr;
      }
      running.store(false, std::memory_order_release);
      was_focused = false;
    }

    bool isRunning() const noexcept
    {
      return running.load(std::memory_order_acquire);
    }

    void drainOutput()
    {
      if(vt == nullptr || screen == nullptr) return;

      std::vector<std::uint8_t> bytes;
      {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        bytes.swap(read_buffer);
      }
      if(!bytes.empty())
        vterm_input_write(vt, reinterpret_cast<const char *>(bytes.data()), bytes.size());
      vterm_screen_flush_damage(screen);
      vterm_state_get_cursorpos(vterm_obtain_state(vt), &cursor);
    }

    void setFocused(bool isFocused)
    {
      if(isFocused == was_focused || vt == nullptr) return;
      VTermState *state = vterm_obtain_state(vt);
      if(isFocused)
        vterm_state_focus_in(state);
      else
        vterm_state_focus_out(state);
      was_focused = isFocused;
    }

    void resize(int newRows, int newCols)
    {
      if(newRows <= 0 || newCols <= 0 || (newRows == rows && newCols == cols)) return;
      clearSelection();
      rows = newRows;
      cols = newCols;
      if(vt != nullptr) vterm_set_size(vt, rows, cols);
      std::lock_guard<std::mutex> lock(write_mutex);
      if(pty != nullptr) pty->resize(rows, cols);
    }

    void write(std::string_view bytes)
    {
      if(pty == nullptr || !isRunning()) return;
      std::lock_guard<std::mutex> lock(write_mutex);
      (void)pty->write(bytes);
    }

    void sendKey(int imguiKey, bool ctrl, bool shift, bool alt)
    {
      if(vt == nullptr || !isRunning()) return;

      // Shell editing controls are byte-oriented. Send them directly through
      // the PTY so they do not depend on the emulator's keyboard state.
      if(ctrl && !alt)
      {
        const std::uint32_t character = notepp::terminal_detail::terminalControlCharacterFromImGui(imguiKey);
        if(character != 0)
        {
          const char byte = static_cast<char>(character & 0x1FU);
          write(std::string_view(&byte, 1));
          return;
        }
      }
      VTermModifier modifier = VTERM_MOD_NONE;
      if(shift) modifier = static_cast<VTermModifier>(modifier | VTERM_MOD_SHIFT);
      if(alt) modifier = static_cast<VTermModifier>(modifier | VTERM_MOD_ALT);
      if(ctrl) modifier = static_cast<VTermModifier>(modifier | VTERM_MOD_CTRL);

      const VTermKey key = notepp::terminal_detail::terminalKeyFromImGui(imguiKey);
      if(key != VTERM_KEY_NONE)
      {
        vterm_keyboard_key(vt, key, modifier);
        return;
      }

      // Printable control keys use a separate libvterm entry point. Keep this
      // in Session::sendKey so every caller (including the public Terminal
      // facade) follows the same PTY path as special keys.
      if(ctrl && !alt)
      {
        const std::uint32_t character = notepp::terminal_detail::terminalControlCharacterFromImGui(imguiKey);
        if(character != 0) vterm_keyboard_unichar(vt, character, VTERM_MOD_CTRL);
      }
    }

    void copySelectionToClipboard() const
    {
      if(screen == nullptr || !hasSelection()) return;

      const VTermPos first = selectionStart();
      const VTermPos last = selectionEnd();
      const VTermRect rect{first.row, last.row + 1, first.col, last.col + 1};
      const std::size_t length = vterm_screen_get_text(screen, nullptr, 0, rect);
      std::string text(length, '\0');
      if(length != 0)
      {
        const std::size_t copied = vterm_screen_get_text(screen, text.data(), text.size(), rect);
        text.resize(copied);
      }
      (void)SDL_SetClipboardText(text.c_str());
    }

    void pasteFromClipboard()
    {
      clearSelection();
      char *text = SDL_GetClipboardText();
      if(text == nullptr) return;
      write(text);
      SDL_free(text);
    }

    SessionId id;
    std::string title;
    std::filesystem::path cwd;
    std::unique_ptr<notepp::terminal::PtyBackend> pty;
    VTerm *vt = nullptr;
    VTermScreen *screen = nullptr;
    VTermColor defaultFg{};
    VTermColor defaultBg{};
    VTermPos cursor{};
    std::thread reader;
    std::mutex buffer_mutex;
    std::vector<std::uint8_t> read_buffer;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> running{false};
    std::mutex write_mutex;
    int rows = 24;
    int cols = 80;
    bool was_focused = false;
    bool exited_naturally = false;
    std::deque<std::vector<VTermScreenCell>> scrollback;
    std::size_t scroll_offset = 0;
    VTermPos selection_start{-1, -1};
    VTermPos selection_end{-1, -1};
    bool selecting = false;

    void clearSelection() noexcept
    {
      selection_start = VTermPos{-1, -1};
      selection_end = VTermPos{-1, -1};
      selecting = false;
    }

    bool hasSelection() const noexcept
    {
      return selection_start.row >= 0 && selection_start.col >= 0 && selection_end.row >= 0 && selection_end.col >= 0;
    }

    VTermPos normalizeSelectionCell(VTermPos position) const noexcept
    {
      if(screen == nullptr) return position;
      VTermScreenCell cell{};
      while(position.col > 0 &&
            vterm_screen_get_cell(screen, position, &cell) != 0 && cell.width == 0)
      {
        --position.col;
      }
      return position;
    }

    VTermPos selectionStart() const noexcept
    {
      const VTermPos first = normalizeSelectionCell(selection_start);
      const VTermPos last = normalizeSelectionCell(selection_end);
      const notepp::terminal_detail::TerminalCell start = notepp::terminal_detail::selectionStart(
          {first.row, first.col}, {last.row, last.col});
      return VTermPos{start.row, start.col};
    }

    VTermPos selectionEnd() const noexcept
    {
      const VTermPos first = normalizeSelectionCell(selection_start);
      const VTermPos last = normalizeSelectionCell(selection_end);
      const notepp::terminal_detail::TerminalCell end = notepp::terminal_detail::selectionEnd(
          {first.row, first.col}, {last.row, last.col});
      return VTermPos{end.row, end.col};
    }

    bool isSelected(int row, int col) const noexcept
    {
      const VTermPos first = normalizeSelectionCell(selection_start);
      const VTermPos last = normalizeSelectionCell(selection_end);
      return notepp::terminal_detail::terminalSelectionContains(
          {first.row, first.col}, {last.row, last.col}, row, col);
    }

    static int screenDamageCallback(VTermRect, void *) { return 1; }
    static int screenMoveRectCallback(VTermRect, VTermRect, void *) { return 1; }
    static int screenMoveCursorCallback(VTermPos, VTermPos, int, void *) { return 1; }
    static int screenSetTermPropCallback(VTermProp, VTermValue *, void *) { return 1; }
    static int screenBellCallback(void *) { return 1; }
    static int screenResizeCallback(int, int, void *) { return 1; }
    static int screenSbPushLineCallback(int cols, const VTermScreenCell *cells, void *user)
    {
      auto *session = static_cast<Session *>(user);
      if(session == nullptr || cells == nullptr || cols <= 0) return 0;
      session->scrollback.emplace_back(cells, cells + cols);
      constexpr std::size_t maxScrollbackLines = 10000U;
      while(session->scrollback.size() > maxScrollbackLines)
        session->scrollback.pop_front();
      return 1;
    }

    static int screenSbPopLineCallback(int cols, VTermScreenCell *cells, void *user)
    {
      auto *session = static_cast<Session *>(user);
      if(session == nullptr || cells == nullptr || cols <= 0 || session->scrollback.empty()) return 0;
      const std::vector<VTermScreenCell> &line = session->scrollback.back();
      const int copied = std::min(cols, static_cast<int>(line.size()));
      std::copy_n(line.data(), copied, cells);
      session->scrollback.pop_back();
      return 1;
    }

    static int screenSbClearCallback(void *user)
    {
      auto *session = static_cast<Session *>(user);
      if(session != nullptr)
      {
        session->scrollback.clear();
        session->scroll_offset = 0;
      }
      return 1;
    }

    static const VTermScreenCallbacks kScreenCallbacks;
  };

  Session *find(SessionId id) noexcept
  {
    const auto it = std::find_if(sessions.begin(), sessions.end(),
                                 [id](const auto &session) { return session->id == id; });
    return it == sessions.end() ? nullptr : it->get();
  }

  const Session *find(SessionId id) const noexcept
  {
    const auto it = std::find_if(sessions.begin(), sessions.end(),
                                 [id](const auto &session) { return session->id == id; });
    return it == sessions.end() ? nullptr : it->get();
  }

  Session *selected() noexcept
  {
    Session *session = find(selected_id);
    if(session == nullptr && !sessions.empty())
    {
      selected_id = sessions.front()->id;
      session = sessions.front().get();
    }
    return session;
  }

  bool renderSession(Session &session, ImFont *font, std::string_view pendingText, bool acceptKeyboardInput, bool windowFocused)
  {
    if(!session.isRunning())
    {
      if(session.exited_naturally) return true;
      const bool hasScreen = session.vt != nullptr && session.screen != nullptr;
      ImGui::TextDisabled("%s", hasScreen ? "Shell exited." : "Terminal session failed to start.");
      ImGui::SameLine();
      if(ImGui::SmallButton("Restart")) session.start(session.rows, session.cols);
      ImGui::SameLine();
      if(ImGui::SmallButton("Close")) return true;
      if(!hasScreen) return false;
      ImGui::Separator();
    }

    const float fontSize = font->FontSize > 0.0F ? font->FontSize : ImGui::GetFontSize();
    const float measuredWidth = font->GetCharAdvance(static_cast<ImWchar>('M'));
    const float cellWidth = measuredWidth > 0.0F ? measuredWidth : 8.0F;
    const float cellHeight = std::max(fontSize + 2.0F, 14.0F);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int cols = std::max(1, static_cast<int>(available.x / cellWidth));
    const int rows = std::max(1, static_cast<int>(available.y / cellHeight));
    session.resize(rows, cols);

    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 contentMin = origin;
    const ImVec2 contentMax(origin.x + cols * cellWidth, origin.y + rows * cellHeight);
    const ImU32 defaultBackground = colorToU32(session.defaultBg, session.defaultFg, session.defaultBg, true);
    draw->AddRectFilled(contentMin, contentMax, defaultBackground);

    const ImGuiIO &io = ImGui::GetIO();
    const bool terminalHovered = ImGui::IsMouseHoveringRect(contentMin, contentMax, true);
    const std::size_t scrollbackLines = session.scrollback.size();
    const std::size_t totalLines = scrollbackLines + static_cast<std::size_t>(rows);
    const std::size_t maxScrollOffset = totalLines > static_cast<std::size_t>(rows)
                                            ? totalLines - static_cast<std::size_t>(rows)
                                            : 0U;
    if(terminalHovered && io.MouseWheel != 0.0F && maxScrollOffset > 0U)
    {
      const int step = io.MouseWheel > 0.0F ? 3 : -3;
      const int current = static_cast<int>(session.scroll_offset);
      session.scroll_offset = static_cast<std::size_t>(std::clamp(current - step, 0, static_cast<int>(maxScrollOffset)));
      session.clearSelection();
    }
    session.scroll_offset = std::min(session.scroll_offset, maxScrollOffset);
    const int firstLine = static_cast<int>(totalLines - static_cast<std::size_t>(rows) - session.scroll_offset);
    const auto getCell = [&](int displayRow, int col, VTermScreenCell &target) {
      const int logicalRow = firstLine + displayRow;
      if(logicalRow < static_cast<int>(scrollbackLines))
      {
        const std::vector<VTermScreenCell> &line = session.scrollback[static_cast<std::size_t>(logicalRow)];
        if(col < 0 || col >= static_cast<int>(line.size())) return false;
        target = line[static_cast<std::size_t>(col)];
        return target.width != 0;
      }
      const VTermPos position{logicalRow - static_cast<int>(scrollbackLines), col};
      return vterm_screen_get_cell(session.screen, position, &target) != 0 && target.width != 0;
    };

    VTermScreenCell cell{};
    char utf8[8] = {};
    for(int row = 0; row < rows; ++row)
    {
      for(int col = 0; col < cols; ++col)
      {
        const bool hasCell = getCell(row, col, cell);
        const ImVec2 cellMin(origin.x + col * cellWidth, origin.y + row * cellHeight);
        const ImVec2 cellMax(cellMin.x + cellWidth * (hasCell ? std::max(1, static_cast<int>(cell.width)) : 1),
                             cellMin.y + cellHeight);
        const bool selected = session.isSelected(row, col);
        if(selected)
          draw->AddRectFilled(cellMin, cellMax, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg)));
        else if(hasCell)
        {
          const ImU32 background = colorToU32(cell.bg, session.defaultFg, session.defaultBg, true);
          if(background != defaultBackground) draw->AddRectFilled(cellMin, cellMax, background);
        }

        if(!hasCell) continue;
        const std::uint32_t codepoint = cell.chars[0];
        if(codepoint == 0) continue;
        (void)ImTextCharToUtf8(utf8, codepoint);
        const ImU32 foreground = colorToU32(cell.fg, session.defaultFg, session.defaultBg, false);
        draw->AddText(font, fontSize, cellMin, foreground, utf8);
      }
    }

    if(session.scroll_offset == 0U && session.cursor.row >= 0 && session.cursor.row < rows && session.cursor.col >= 0 && session.cursor.col < cols)
    {
      const ImVec2 cursorMin(origin.x + session.cursor.col * cellWidth, origin.y + session.cursor.row * cellHeight);
      const ImVec2 cursorMax(cursorMin.x + cellWidth, cursorMin.y + cellHeight);
      const ImU32 cursorColor = colorToU32(session.defaultFg, session.defaultFg, session.defaultBg, false);
      draw->AddRect(cursorMin, cursorMax, cursorColor, 0.0F, 0, 1.5F);
    }

    // Capture mouse input in the terminal grid without drawing an ImGui
    // selection rectangle; the terminal selection highlight is rendered above.
    ImGui::InvisibleButton("##terminal_grid", ImVec2(cols * cellWidth, rows * cellHeight));
    const bool gridHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const auto cellAt = [&](const ImVec2 &mouse) {
      const int col = std::clamp(static_cast<int>((mouse.x - origin.x) / cellWidth), 0, cols - 1);
      const int row = std::clamp(static_cast<int>((mouse.y - origin.y) / cellHeight), 0, rows - 1);
      return VTermPos{row, col};
    };
    if(gridHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsDragDropActive())
    {
      session.selection_start = cellAt(io.MousePos);
      session.selection_end = session.selection_start;
      session.selecting = true;
    }
    if(session.selecting)
    {
      if(ImGui::IsDragDropActive())
        session.clearSelection();
      else if(ImGui::IsMouseDown(ImGuiMouseButton_Left))
        session.selection_end = cellAt(io.MousePos);
      else
        session.selecting = false;
    }

    focused = windowFocused;
    session.setFocused(windowFocused);
    if(!windowFocused || !acceptKeyboardInput || !session.isRunning()) return false;

    // This is a custom terminal widget rather than ImGui::InputText, so the
    // SDL backend cannot infer that it needs composed text, IME input, and
    // paste text. Request SDL text events while the terminal owns focus.
    SDL_StartTextInput();
    if(!pendingText.empty()) session.write(pendingText);

    if(!pendingText.empty()) session.clearSelection();
    const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if(escapePressed) session.clearSelection();

    return false;
  }

  std::vector<std::unique_ptr<Session>> sessions;
  std::filesystem::path default_cwd;
  SessionId next_id = 1;
  SessionId selected_id = 0;
  SessionId selection_request_id = 0;
  ImFont *font = nullptr;
  bool first_frame = true;
  bool focused = false;
  float window_height = 360.0F;
  float window_left = 0.0F;
  float window_width = -1.0F;
};

const VTermScreenCallbacks Terminal::Impl::Session::kScreenCallbacks = {
    screenDamageCallback,
    screenMoveRectCallback,
    screenMoveCursorCallback,
    screenSetTermPropCallback,
    screenBellCallback,
    screenResizeCallback,
    screenSbPushLineCallback,
    screenSbPopLineCallback,
    screenSbClearCallback,
};

Terminal::Terminal()
    : impl_(new Impl())
{
}

Terminal::~Terminal()
{
  delete impl_;
}

void Terminal::start(const std::filesystem::path &cwd, int rows, int cols)
{
  impl_->default_cwd = cwd;
  if(Impl::Session *session = impl_->selected(); session != nullptr)
  {
    session->cwd = cwd;
    session->start(rows, cols);
    return;
  }
  (void)addSession(cwd, rows, cols);
}

Terminal::SessionId Terminal::addSession(const std::filesystem::path &cwd, int rows, int cols)
{
  impl_->default_cwd = cwd;
  const SessionId id = impl_->next_id++;
  auto session = std::make_unique<Impl::Session>(id, "Terminal " + std::to_string(id), cwd);
  session->start(rows, cols);
  impl_->sessions.push_back(std::move(session));
  impl_->selected_id = id;
  impl_->selection_request_id = id;
  return id;
}

bool Terminal::selectAdjacentSession(bool previous)
{
  if(impl_->sessions.empty()) return false;
  const auto current = std::find_if(
      impl_->sessions.begin(), impl_->sessions.end(),
      [id = impl_->selected_id](const auto &session) { return session->id == id; });
  const std::size_t currentIndex = current == impl_->sessions.end()
                                       ? 0U
                                       : static_cast<std::size_t>(std::distance(impl_->sessions.begin(), current));
  const std::size_t count = impl_->sessions.size();
  const std::size_t nextIndex = previous
                                    ? (currentIndex + count - 1U) % count
                                    : (currentIndex + 1U) % count;
  impl_->selected_id = impl_->sessions[nextIndex]->id;
  impl_->selection_request_id = impl_->selected_id;
  return true;
}

bool Terminal::closeSession(SessionId id)
{
  const auto it = std::find_if(impl_->sessions.begin(), impl_->sessions.end(),
                               [id](const auto &session) { return session->id == id; });
  if(it == impl_->sessions.end()) return false;

  const auto index = static_cast<std::size_t>(std::distance(impl_->sessions.begin(), it));
  impl_->sessions.erase(it);
  if(impl_->selected_id == id)
  {
    if(impl_->sessions.empty())
      impl_->selected_id = 0;
    else
      impl_->selected_id = impl_->sessions[std::min(index, impl_->sessions.size() - 1)]->id;
    impl_->selection_request_id = impl_->selected_id;
  }
  return true;
}

void Terminal::setDefaultWorkingDirectory(const std::filesystem::path &cwd)
{
  impl_->default_cwd = cwd;
}

void Terminal::stop()
{
  impl_->sessions.clear();
  impl_->selected_id = 0;
  impl_->selection_request_id = 0;
  impl_->focused = false;
}

bool Terminal::isRunning() const noexcept
{
  return std::any_of(impl_->sessions.begin(), impl_->sessions.end(),
                     [](const auto &session) { return session->isRunning(); });
}

bool Terminal::hasFocus() const noexcept
{
  return impl_->focused;
}

void Terminal::releaseFocus()
{
  impl_->focused = false;
  for(const auto &session : impl_->sessions) session->setFocused(false);
}

std::size_t Terminal::sessionCount() const noexcept
{
  return impl_->sessions.size();
}

bool Terminal::isSessionRunning(SessionId id) const noexcept
{
  const Impl::Session *session = impl_->find(id);
  return session != nullptr && session->isRunning();
}

void Terminal::resize(int rows, int cols)
{
  if(Impl::Session *session = impl_->selected(); session != nullptr) session->resize(rows, cols);
}

void Terminal::write(std::string_view bytes)
{
  if(Impl::Session *session = impl_->selected(); session != nullptr) session->write(bytes);
}

void Terminal::clearSelection()
{
  if(Impl::Session *session = impl_->selected(); session != nullptr) session->clearSelection();
}

void Terminal::copySelectionToClipboard()
{
  if(Impl::Session *session = impl_->selected(); session != nullptr)
    session->copySelectionToClipboard();
}

void Terminal::pasteClipboard()
{
  if(Impl::Session *session = impl_->selected(); session != nullptr)
    session->pasteFromClipboard();
}

void Terminal::sendKey(int imguiKey, bool ctrl, bool shift, bool alt)
{
  if(Impl::Session *session = impl_->selected(); session != nullptr)
    session->sendKey(imguiKey, ctrl, shift, alt);
}

void Terminal::setFont(ImFont *font) noexcept
{
  impl_->font = font;
}

void Terminal::setWindowHeight(float height) noexcept
{
  impl_->window_height = std::max(180.0F, height);
}

float Terminal::windowHeight() const noexcept
{
  return impl_->window_height;
}

void Terminal::setWindowHorizontalBounds(const float left, const float width) noexcept
{
  impl_->window_left = left;
  impl_->window_width = width;
}

void Terminal::render(bool *open, std::string_view pendingText, bool acceptKeyboardInput)
{
  impl_->focused = false;
  if(open == nullptr || !*open) return;

  for(const auto &session : impl_->sessions) session->drainOutput();

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float minHeight = 180.0F;
  const float maxHeight = std::max(minHeight, viewport->WorkSize.y - 80.0F);
  impl_->window_height = std::clamp(impl_->window_height, minHeight, maxHeight);
  const float workRight = viewport->WorkPos.x + viewport->WorkSize.x;
  const float windowLeft = std::clamp(impl_->window_left, viewport->WorkPos.x, workRight);
  const float defaultWidth = workRight - windowLeft;
  const float windowWidth = std::clamp(
      impl_->window_width < 0.0F ? defaultWidth : impl_->window_width, 1.0F, defaultWidth);
  ImGui::SetNextWindowPos(
      ImVec2(windowLeft, viewport->WorkPos.y + viewport->WorkSize.y - impl_->window_height),
      ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(windowWidth, impl_->window_height), ImGuiCond_Always);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(windowWidth, minHeight), ImVec2(windowWidth, maxHeight));
  ImGui::SetNextWindowBgAlpha(1.0F);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.065F, 0.080F, 0.105F, 1.0F));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F, 4.0F));
  if(!ImGui::Begin("Terminal##notepp", open,
                   ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoDocking |
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoNavInputs |
                       ImGuiWindowFlags_NoTitleBar))
  {
    impl_->first_frame = false;
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    return;
  }
  impl_->window_height = ImGui::GetWindowSize().y;
  ImDrawList *windowDraw = ImGui::GetWindowDrawList();
  const ImVec2 windowMin = ImGui::GetWindowPos();
  windowDraw->AddRectFilled(
      windowMin, ImVec2(windowMin.x + ImGui::GetWindowWidth(), windowMin.y + 1.0F),
      ImGui::GetColorU32(ImGuiCol_Border));
  if(ImGui::IsWindowHovered() && ImGui::GetIO().MousePos.y <= windowMin.y + 5.0F)
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

  ImFont *font = impl_->font != nullptr ? impl_->font : ImGui::GetFont();
  std::vector<SessionId> closeRequests;
  bool addRequested = false;
  bool activeRendered = false;

  if(ImGui::BeginTabBar("##terminal_sessions", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable))
  {
    for(const auto &session : impl_->sessions)
    {
      bool tabOpen = true;
      ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
      if(session->id == impl_->selection_request_id) flags |= ImGuiTabItemFlags_SetSelected;
      const std::string label = session->title + "###terminal_session_" + std::to_string(session->id);
      if(ImGui::BeginTabItem(label.c_str(), &tabOpen, flags))
      {
        impl_->selected_id = session->id;
        activeRendered = true;
        const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if(impl_->renderSession(*session, font, pendingText, acceptKeyboardInput, windowFocused))
          closeRequests.push_back(session->id);
        ImGui::EndTabItem();
      }
      if(!tabOpen) closeRequests.push_back(session->id);
    }
    if(ImGui::TabItemButton("+##new_terminal", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
      addRequested = true;
    ImGui::EndTabBar();
  }

  impl_->selection_request_id = 0;
  if(!activeRendered && !impl_->sessions.empty())
  {
    Impl::Session *session = impl_->selected();
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if(session != nullptr && impl_->renderSession(*session, font, pendingText, acceptKeyboardInput, windowFocused))
      closeRequests.push_back(session->id);
  }
  else if(!activeRendered)
  {
    ImGui::TextDisabled("No terminal sessions.");
    if(ImGui::Button("New Terminal")) addRequested = true;
  }

  for(const auto &session : impl_->sessions)
  {
    if(session->id != impl_->selected_id) session->setFocused(false);
  }

  std::sort(closeRequests.begin(), closeRequests.end());
  closeRequests.erase(std::unique(closeRequests.begin(), closeRequests.end()), closeRequests.end());
  for(const SessionId id : closeRequests) closeSession(id);
  if(addRequested) (void)addSession(impl_->default_cwd);

  impl_->first_frame = false;
  ImGui::End();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
}
