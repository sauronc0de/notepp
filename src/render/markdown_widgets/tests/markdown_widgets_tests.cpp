#include "button_action.hpp"
#include "markdown_widgets.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace
{
int failures = 0;

void expect(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

void test_valid_command_actions()
{
  using MarkdownWidgets::detail::CommandActionStatus;
  using MarkdownWidgets::detail::parseCommandAction;

  const auto literal = parseCommandAction(R"(command("make"))");
  expect(literal.status == CommandActionStatus::Valid, "string literal command is valid");
  expect(literal.argument == R"("make")", "string literal expression is preserved");

  const auto expression = parseCommandAction(R"( command (prefix + " --all") )");
  expect(expression.status == CommandActionStatus::Valid, "command permits whitespace and an expression");
  expect(expression.argument == R"(prefix + " --all")", "command expression is trimmed and preserved");

  const auto nested = parseCommandAction(R"(command(format("a,b", [1, 2])))");
  expect(nested.status == CommandActionStatus::Valid, "nested commas do not create extra command arguments");
}

void test_non_command_action()
{
  using MarkdownWidgets::detail::CommandActionStatus;
  const auto assignment = MarkdownWidgets::detail::parseCommandAction("count=0");
  expect(assignment.status == CommandActionStatus::NotCommand, "assignment remains a non-command action");
}

void test_command_set_variable_string()
{
  MarkdownWidgets::reset_persistence_state();
  MarkdownWidgets::set_widget_document_path({});
  std::string markdown = R"~~~(```ui
status("todo")
```
)~~~";
  const auto changed = MarkdownWidgets::command_set_variable(markdown, "status", "done");
  expect(changed.success, "command variable set updates a declared local variable");
  expect(markdown.find(R"(status("done"))") != std::string::npos,
         "command variable set preserves string values without command quotes");
}

void test_cross_note_variable_lookup()
{
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "notepp-widget-variable-test" / "notepp";
  fs::remove_all(root.parent_path());
  fs::create_directories(root / "notes" / "inbox");
  const fs::path target = root / "notes" / "inbox" / "template.md";
  {
    std::ofstream output(target);
    output << "```ui\nstatus(\"done\")\n```\n";
  }
  MarkdownWidgets::set_widget_project_root(root);
  MarkdownWidgets::set_widget_document_path(root / "notes" / "current.md");
  const std::string rendered = MarkdownWidgets::resolve_ui_mermaid_template(
      {}, "${get_variables(projects/notepp/inbox/template.md).status}");
  expect(rendered == "done", "get_variables reads a safe explicit note path");
  const std::string escaped = MarkdownWidgets::resolve_ui_mermaid_template(
      {}, "${get_variables(../outside.md).status}");
  expect(escaped.find("get_variables") != std::string::npos,
         "get_variables rejects paths outside the project");
  fs::remove_all(root.parent_path());
}

const MarkdownWidgets::InspectedVariable *find_variable(
    const MarkdownWidgets::VariableSnapshot &snapshot,
    MarkdownWidgets::VariableScope scope,
    std::size_t block_index,
    std::string_view name)
{
  for(const auto &group : snapshot.groups)
    if(group.scope == scope && group.block_index == block_index)
      for(const auto &variable : group.variables)
        if(variable.locator.name == name) return &variable;
  return nullptr;
}

void test_variable_inspection_and_exact_editing()
{
  MarkdownWidgets::reset_persistence_state();
  MarkdownWidgets::set_widget_document_path({});
  std::string markdown =
      "```ui\n"
      "title(\"first\")\n"
      "count(2)\n"
      "enabled(true)\n"
      "items([1, \"two\"])\n"
      "config({\"mode\": \"fast\"})\n"
      "doubled(count * 2)\n"
      "constant(1 + 2)\n"
      "length(len([1, 2]))\n"
      "```\n"
      "```ui\n"
      "title(\"second\")\n"
      "```\n";

  const auto snapshot = MarkdownWidgets::inspect_variables(markdown);
  expect(snapshot.variable_count == 9, "inspection enumerates declarations in every UI block");
  const auto *first_title = find_variable(
      snapshot, MarkdownWidgets::VariableScope::local, 0, "title");
  const auto *second_title = find_variable(
      snapshot, MarkdownWidgets::VariableScope::local, 1, "title");
  const auto *count = find_variable(
      snapshot, MarkdownWidgets::VariableScope::local, 0, "count");
  const auto *enabled = find_variable(
      snapshot, MarkdownWidgets::VariableScope::local, 0, "enabled");
  const auto *items = find_variable(
      snapshot, MarkdownWidgets::VariableScope::local, 0, "items");
  const auto *config = find_variable(
      snapshot, MarkdownWidgets::VariableScope::local, 0, "config");
  const auto *doubled = find_variable(
      snapshot, MarkdownWidgets::VariableScope::local, 0, "doubled");
  const auto *constant = find_variable(
      snapshot, MarkdownWidgets::VariableScope::local, 0, "constant");
  const auto *length = find_variable(
      snapshot, MarkdownWidgets::VariableScope::local, 0, "length");
  expect(first_title != nullptr && second_title != nullptr,
         "duplicate names retain separate block locators");
  expect(count != nullptr && count->value.is_number(), "number value is evaluated");
  expect(enabled != nullptr && enabled->value == true, "boolean value is evaluated");
  expect(items != nullptr && items->value.is_array() && items->value.size() == 2,
         "array value is evaluated");
  expect(config != nullptr && config->value.is_object() && config->value["mode"] == "fast",
         "object value is evaluated");
  expect(doubled != nullptr && doubled->computed && doubled->value == 4.0,
         "computed value is exposed as readonly with its result");
  expect(constant != nullptr && constant->computed && constant->value == 3.0,
         "constant arithmetic is classified as computed");
  expect(length != nullptr && length->computed && length->value == 2.0,
         "constant builtin calls are classified as computed");

  if(second_title != nullptr)
  {
    const auto changed = MarkdownWidgets::command_set_variable_at(
        markdown, second_title->locator, "changed");
    expect(changed.success, "exact locator edits the selected duplicate declaration");
    expect(markdown.find("title(\"first\")") != std::string::npos,
           "editing the second duplicate leaves the first unchanged");
    expect(markdown.find("title(\"changed\")") != std::string::npos,
           "editing the second duplicate updates its expression");
    const auto stale = MarkdownWidgets::command_set_variable_at(
        markdown, second_title->locator, "again");
    expect(!stale.success && stale.error.find("stale") != std::string::npos,
           "a locator is rejected after its source span changes");
  }
  if(doubled != nullptr)
  {
    const auto readonly = MarkdownWidgets::command_set_variable_at(
        markdown, doubled->locator, 8);
    expect(!readonly.success && readonly.error.find("readonly") != std::string::npos,
           "computed declarations reject exact edits");
  }
  if(constant != nullptr)
  {
    const auto readonly = MarkdownWidgets::command_set_variable_at(
        markdown, constant->locator, 9);
    expect(!readonly.success && readonly.error.find("readonly") != std::string::npos,
           "constant arithmetic declarations reject exact edits");
  }
  if(length != nullptr)
  {
    const auto readonly = MarkdownWidgets::command_set_variable_at(
        markdown, length->locator, 9);
    expect(!readonly.success && readonly.error.find("readonly") != std::string::npos,
           "constant builtin declarations reject exact edits");
  }
  if(first_title != nullptr)
  {
    const auto null_value = MarkdownWidgets::command_set_variable_at(
        markdown, first_title->locator, nlohmann::json{});
    expect(!null_value.success && null_value.error.find("null") != std::string::npos,
           "null variable edits are rejected");
    const auto wrong_type = MarkdownWidgets::command_set_variable_at(
        markdown, first_title->locator, 12);
    expect(!wrong_type.success && wrong_type.error.find("type changes") != std::string::npos,
           "inspector edits preserve the declared value type");
  }
  if(items != nullptr)
  {
    const auto structured = MarkdownWidgets::command_set_variable_at(
        markdown, items->locator, nlohmann::json::array({3, 4, 5}));
    expect(structured.success && markdown.find("items([3, 4, 5])") != std::string::npos,
           "structured values are serialized through exact edits");
  }
}

void test_global_variable_inspection_and_editing()
{
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "notepp-widget-inspector-global-test";
  fs::remove_all(root);
  fs::create_directories(root / "notes" / "chapter");
  const fs::path globals_path = root / "notes" / ".globals.md";
  {
    std::ofstream output(globals_path);
    output << "```ui\ngold(10)\nlabel(\"shared\")\n"
              "constant(2 * 3)\nlength(len([1, 2, 3]))\n```\n";
  }

  MarkdownWidgets::reset_persistence_state();
  MarkdownWidgets::set_widget_document_path(root / "notes" / "chapter" / "note.md");
  std::string markdown = "```ui\nlocal(true)\n```\n";
  const auto snapshot = MarkdownWidgets::inspect_variables(markdown);
  const MarkdownWidgets::InspectedVariable *gold = nullptr;
  const MarkdownWidgets::InspectedVariable *constant = nullptr;
  const MarkdownWidgets::InspectedVariable *length = nullptr;
  for(const auto &group : snapshot.groups)
    if(group.scope == MarkdownWidgets::VariableScope::global)
      for(const auto &variable : group.variables)
      {
        if(variable.locator.name == "gold") gold = &variable;
        if(variable.locator.name == "constant") constant = &variable;
        if(variable.locator.name == "length") length = &variable;
      }
  expect(gold != nullptr && gold->value == 10.0, "inspection includes inherited globals");
  expect(constant != nullptr && constant->computed && constant->value == 6.0,
         "constant arithmetic globals are readonly");
  expect(length != nullptr && length->computed && length->value == 3.0,
         "constant builtin globals are readonly");
  if(constant != nullptr)
  {
    const auto readonly = MarkdownWidgets::command_set_variable_at(
        markdown, constant->locator, 12);
    expect(!readonly.success && readonly.error.find("readonly") != std::string::npos,
           "constant arithmetic globals reject exact edits");
  }
  if(length != nullptr)
  {
    const auto readonly = MarkdownWidgets::command_set_variable_at(
        markdown, length->locator, 12);
    expect(!readonly.success && readonly.error.find("readonly") != std::string::npos,
           "constant builtin globals reject exact edits");
  }
  if(gold != nullptr)
  {
    const auto changed = MarkdownWidgets::command_set_variable_at(
        markdown, gold->locator, 25);
    expect(changed.success, "exact global edit uses guarded global persistence");
    std::ifstream input(globals_path);
    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    expect(content.find("gold(25)") != std::string::npos,
           "global edit updates its owning globals file");
  }
  MarkdownWidgets::reset_persistence_state();
  fs::remove_all(root);
}

void test_invalid_command_actions()
{
  using MarkdownWidgets::detail::CommandActionStatus;
  using MarkdownWidgets::detail::parseCommandAction;

  expect(parseCommandAction("command()").status == CommandActionStatus::Invalid,
         "empty command action is rejected");
  expect(parseCommandAction(R"(command("one", "two"))").status == CommandActionStatus::Invalid,
         "multiple command arguments are rejected");
  expect(parseCommandAction(R"(command("missing close")").status == CommandActionStatus::Invalid,
         "missing closing parenthesis is rejected");
  expect(parseCommandAction(R"(command("unterminated))").status == CommandActionStatus::Invalid,
         "unterminated command string is rejected");
  expect(parseCommandAction(R"(command("ok") trailing)").status == CommandActionStatus::Invalid,
         "trailing command action text is rejected");
  expect(parseCommandAction(R"(command "no parens")").status == CommandActionStatus::Invalid,
         "missing opening parenthesis is rejected");
}
} // namespace

int main()
{
  test_valid_command_actions();
  test_non_command_action();
  test_command_set_variable_string();
  test_cross_note_variable_lookup();
  test_variable_inspection_and_exact_editing();
  test_global_variable_inspection_and_editing();
  test_invalid_command_actions();
  if(failures != 0)
  {
    std::cerr << failures << " markdown widget test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "markdown widget tests passed\n";
  return EXIT_SUCCESS;
}
