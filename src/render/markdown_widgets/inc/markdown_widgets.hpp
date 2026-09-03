#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

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

enum class VariableScope
{
  local,
  global
};

enum class VariableValueType
{
  invalid,
  number,
  string,
  boolean,
  array,
  object
};

struct VariableLocator
{
  VariableScope scope = VariableScope::local;
  std::size_t block_index = 0;
  std::size_t expression_start = 0;
  std::size_t expression_end = 0;
  std::string name;
  std::string source_file;
  std::string original_expression;
};

struct InspectedVariable
{
  VariableLocator locator;
  std::string expression;
  std::size_t source_line = 0;
  VariableValueType type = VariableValueType::invalid;
  nlohmann::json value;
  bool computed = false;
  std::string error;
};

struct VariableGroup
{
  VariableScope scope = VariableScope::local;
  std::size_t block_index = 0;
  std::string source_file;
  std::vector<InspectedVariable> variables;
  std::vector<std::string> errors;
};

struct VariableSnapshot
{
  std::vector<VariableGroup> groups;
  std::size_t variable_count = 0;
};

// Headless adapters for the same declarations, evaluator, serializer, and
// persistence used by rendered UI blocks. Locators address a declaration by
// source span so duplicate names in separate UI blocks remain unambiguous.
VariableSnapshot inspect_variables(std::string_view markdown);
VariableResult command_get_variable(std::string_view markdown, std::string_view name);
VariableResult command_set_variable(std::string &markdown, std::string_view name,
                                    const nlohmann::json &value);
VariableResult command_set_variable_at(std::string &markdown,
                                       const VariableLocator &locator,
                                       const nlohmann::json &value);

void set_widget_document_path(std::filesystem::path path);
void set_widget_project_root(std::filesystem::path path);
std::filesystem::path widget_document_path();
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
