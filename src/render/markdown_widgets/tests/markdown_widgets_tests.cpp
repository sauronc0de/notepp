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
  test_cross_note_variable_lookup();
  test_invalid_command_actions();
  if(failures != 0)
  {
    std::cerr << failures << " markdown widget test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "markdown widget tests passed\n";
  return EXIT_SUCCESS;
}
