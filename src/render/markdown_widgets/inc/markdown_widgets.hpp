#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace MarkdownWidgets
{
struct RenderResult
{
  bool handled = false;
  bool markdown_changed = false;
  bool preview_state_changed = false;
  bool consumed_right_click = false;
};

using TerminalCommandHandler = std::function<void(std::string_view)>;
using CommandActionHandler = std::function<bool(std::string_view, std::string_view)>;

struct VariableResult
{
  bool success = false;
  nlohmann::json value;
  std::string error;
};

// Headless command adapter for the same declarations and persistence used by
// rendered UI blocks. Local variables update markdown; global variables use
// the existing .globals.md persistence path.
VariableResult command_get_variable(std::string_view markdown, std::string_view name);
VariableResult command_set_variable(std::string &markdown, std::string_view name,
                                    const nlohmann::json &value);

void set_widget_document_path(std::filesystem::path path);
void notify_document_moved(const std::filesystem::path &from,
                           const std::filesystem::path &to);
void notify_document_saved(const std::filesystem::path &path);
void set_terminal_command_handler(TerminalCommandHandler handler);
void set_command_action_handler(CommandActionHandler handler);
// Dispatch a command through the configured command-action handler, falling
// back to the terminal for legacy shell commands.
void execute_command_action(std::string_view command, std::string_view card_reference = {});
// Dispatch a command through the configured terminal handler.
void execute_terminal_command(std::string_view command);
std::string last_persistence_error();
void reset_persistence_state();

std::string capture_ui_state_snapshot();
void apply_ui_state_snapshot(std::string_view snapshot);

RenderResult try_render_ui_block(std::string &markdown, size_t fence_start, size_t fence_line_end, size_t block_end);

std::string resolve_ui_mermaid_template(std::string_view note_markdown, std::string_view template_body);
} // namespace MarkdownWidgets
