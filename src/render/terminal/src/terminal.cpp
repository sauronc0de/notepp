#include "terminal.hpp"

#include "pty.hpp"
#include "terminal_key_map.hpp"

#include <vterm.h>
#include <vterm_keycodes.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
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
  if(const char *shell = std::getenv("SHELL"); shell != nullptr && *shell != '\0') return shell;
#if defined(_WIN32)
  return {};
#else
  return "/bin/sh";
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
    }

    bool start(int initialRows, int initialCols)
    {
      stop();

      rows = std::max(1, initialRows);
      cols = std::max(1, initialCols);
      stop_requested.store(false, std::memory_order_relaxed);
      {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        read_buffer.clear();
      }

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
      pty->write(bytes);
    }

    void sendKey(int imguiKey, bool ctrl, bool shift, bool alt)
    {
      if(vt == nullptr || !isRunning()) return;

      VTermModifier modifier = VTERM_MOD_NONE;
      if(shift) modifier = static_cast<VTermModifier>(modifier | VTERM_MOD_SHIFT);
      if(alt) modifier = static_cast<VTermModifier>(modifier | VTERM_MOD_ALT);
      if(ctrl) modifier = static_cast<VTermModifier>(modifier | VTERM_MOD_CTRL);

      const VTermKey key = notepp::terminal_detail::terminalKeyFromImGui(imguiKey);
      if(key != VTERM_KEY_NONE) vterm_keyboard_key(vt, key, modifier);
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

    static int screenDamageCallback(VTermRect, void *) { return 1; }
    static int screenMoveRectCallback(VTermRect, VTermRect, void *) { return 1; }
    static int screenMoveCursorCallback(VTermPos, VTermPos, int, void *) { return 1; }
    static int screenSetTermPropCallback(VTermProp, VTermValue *, void *) { return 1; }
    static int screenBellCallback(void *) { return 1; }
    static int screenResizeCallback(int, int, void *) { return 1; }
    static int screenSbPushLineCallback(int, const VTermScreenCell *, void *) { return 0; }
    static int screenSbPopLineCallback(int, VTermScreenCell *, void *) { return 0; }
    static int screenSbClearCallback(void *) { return 1; }

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

    VTermScreenCell cell{};
    char utf8[8] = {};
    for(int row = 0; row < rows; ++row)
    {
      for(int col = 0; col < cols; ++col)
      {
        const VTermPos position{row, col};
        if(vterm_screen_get_cell(session.screen, position, &cell) == 0 || cell.width == 0) continue;

        const ImVec2 cellMin(origin.x + col * cellWidth, origin.y + row * cellHeight);
        const ImVec2 cellMax(cellMin.x + cellWidth * std::max(1, static_cast<int>(cell.width)), cellMin.y + cellHeight);
        const ImU32 background = colorToU32(cell.bg, session.defaultFg, session.defaultBg, true);
        if(background != defaultBackground) draw->AddRectFilled(cellMin, cellMax, background);

        const std::uint32_t codepoint = cell.chars[0];
        if(codepoint == 0) continue;
        (void)ImTextCharToUtf8(utf8, codepoint);
        const ImU32 foreground = colorToU32(cell.fg, session.defaultFg, session.defaultBg, false);
        draw->AddText(font, fontSize, cellMin, foreground, utf8);
      }
    }

    if(session.cursor.row >= 0 && session.cursor.row < rows && session.cursor.col >= 0 && session.cursor.col < cols)
    {
      const ImVec2 cursorMin(origin.x + session.cursor.col * cellWidth, origin.y + session.cursor.row * cellHeight);
      const ImVec2 cursorMax(cursorMin.x + cellWidth, cursorMin.y + cellHeight);
      const ImU32 cursorColor = colorToU32(session.defaultFg, session.defaultFg, session.defaultBg, false);
      draw->AddRect(cursorMin, cursorMax, cursorColor, 0.0F, 0, 1.5F);
    }

    ImGui::Dummy(ImVec2(cols * cellWidth, rows * cellHeight));

    focused = windowFocused;
    session.setFocused(windowFocused);
    if(!windowFocused || !acceptKeyboardInput || !session.isRunning()) return false;

    if(!pendingText.empty()) session.write(pendingText);

    const ImGuiIO &io = ImGui::GetIO();
    const bool ctrlDown = io.KeyCtrl;
    const bool shiftDown = io.KeyShift;
    const bool altDown = io.KeyAlt;

    // Terminal tabs own Ctrl+W; do not also forward it as the shell's ^W.
    if(ctrlDown && !shiftDown && !altDown && ImGui::IsKeyPressed(ImGuiKey_W, false))
      return true;

    // Plain Tab and Shift+Tab belong to the shell for completion. Ctrl+Tab
    // remains reserved for Dear ImGui's global window navigation.
    if(!ctrlDown && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
      session.sendKey(ImGuiKey_Tab, false, shiftDown, altDown);
    for(int key = ImGuiKey_LeftArrow; key <= ImGuiKey_F12; ++key)
    {
      if(ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false))
        session.sendKey(key, ctrlDown, shiftDown, altDown);
    }
    if(ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
      session.sendKey(ImGuiKey_KeypadEnter, ctrlDown, shiftDown, altDown);

    if(ctrlDown && !altDown)
    {
      for(int key = ImGuiKey_A; key <= ImGuiKey_Z; ++key)
      {
        if(!ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) continue;
        const std::uint32_t letter = notepp::terminal_detail::terminalControlCharacterFromImGui(key);
        vterm_keyboard_unichar(session.vt, letter, VTERM_MOD_CTRL);
      }
    }
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

void Terminal::sendKey(int imguiKey, bool ctrl, bool shift, bool alt)
{
  if(Impl::Session *session = impl_->selected(); session != nullptr)
    session->sendKey(imguiKey, ctrl, shift, alt);
}

void Terminal::setFont(ImFont *font) noexcept
{
  impl_->font = font;
}

void Terminal::render(bool *open, std::string_view pendingText, bool acceptKeyboardInput)
{
  impl_->focused = false;
  if(open == nullptr || !*open) return;

  for(const auto &session : impl_->sessions) session->drainOutput();

  if(impl_->first_frame || !acceptKeyboardInput)
  {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowFocus();
  }
  ImGui::SetNextWindowSize(ImVec2(720.0F, 420.0F), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(1.0F);
  if(!ImGui::Begin("Terminal##notepp", open,
                   ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoDocking |
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoNavInputs))
  {
    impl_->first_frame = false;
    ImGui::End();
    return;
  }

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
}
