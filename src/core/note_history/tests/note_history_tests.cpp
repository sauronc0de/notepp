#include "note_history.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <stdexcept>

namespace
{
int failures = 0;

void expect_true(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

std::unique_ptr<NoteHistory::Command> make_command(
    std::string_view label,
    int &value,
    int execute_delta,
    int undo_delta)
{
  return std::make_unique<NoteHistory::LambdaCommand>(
      std::string(label),
      std::string("debug-") + std::string(label),
      [&value, execute_delta]() { value += execute_delta; },
      [&value, undo_delta]() { value += undo_delta; });
}

void push_executed_command(NoteHistory::HistoryManager &history,
                           std::string_view label,
                           int &value,
                           int execute_delta,
                           int undo_delta)
{
  value += execute_delta;
  history.push_executed(make_command(label, value, execute_delta, undo_delta));
}

void test_empty_history()
{
  NoteHistory::HistoryManager history;

  expect_true(!history.can_undo(), "new history cannot undo");
  expect_true(!history.can_redo(), "new history cannot redo");
  expect_true(!history.undo(), "undo on empty history returns false");
  expect_true(!history.redo(), "redo on empty history returns false");
  expect_true(history.next_undo_label().empty(), "empty undo label");
  expect_true(history.next_redo_label().empty(), "empty redo label");
}

void test_undo_redo_round_trip()
{
  NoteHistory::HistoryManager history;
  int value = 0;

  push_executed_command(history, "increment", value, 5, -5);

  expect_true(value == 5, "push helper applies command effect");
  expect_true(history.can_undo(), "history can undo after command");
  expect_true(!history.can_redo(), "history cannot redo before undo");
  expect_true(history.next_undo_label() == "increment", "undo label is latest command");

  expect_true(history.undo(), "undo succeeds");
  expect_true(value == 0, "undo restores value");
  expect_true(!history.can_undo(), "history cannot undo after undoing only command");
  expect_true(history.can_redo(), "history can redo after undo");
  expect_true(history.next_redo_label() == "increment", "redo label is command");

  expect_true(history.redo(), "redo succeeds");
  expect_true(value == 5, "redo reapplies value");
}

void test_new_command_clears_redo_stack()
{
  NoteHistory::HistoryManager history;
  int value = 0;

  push_executed_command(history, "first", value, 1, -1);
  expect_true(history.undo(), "first undo succeeds");
  expect_true(history.can_redo(), "redo available after undo");

  push_executed_command(history, "second", value, 10, -10);
  expect_true(!history.can_redo(), "new command clears redo stack");
  expect_true(history.next_undo_label() == "second", "latest undo label after new command");
}

void test_stack_limit_keeps_latest_commands()
{
  NoteHistory::HistoryManager history(2);
  int value = 0;

  push_executed_command(history, "one", value, 1, -1);
  push_executed_command(history, "two", value, 1, -1);
  push_executed_command(history, "three", value, 1, -1);

  const auto entries = history.debug_undo_entries();
  expect_true(entries.size() == 2, "history respects stack limit");
  expect_true(entries.size() >= 1 && entries[0].label == "three", "latest command retained first in debug list");
  expect_true(entries.size() >= 2 && entries[1].label == "two", "older retained command is second latest");
}

void test_navigation_history_back_forward_and_divergence()
{
  NoteHistory::NavigationHistory history(3);
  history.visit({"one.md", false, 1U});
  history.visit({"two.md", true, 2U});
  history.visit({"three.md", false, 3U});
  expect_true(history.can_back(), "navigation can go back");
  const auto two = history.back();
  expect_true(two && two->note_path == "two.md" && two->editing,
              "navigation restores note and mode");
  const auto one = history.back();
  expect_true(one && one->source_offset == 1U, "navigation restores source offset");
  expect_true(!history.can_back(), "navigation reaches oldest retained entry");
  expect_true(history.forward()->note_path == "two.md", "navigation can go forward");
  history.visit({"four.md", false, 4U});
  expect_true(!history.can_forward(), "divergent visit truncates forward history");
  history.visit({"five.md", false, 5U});
  expect_true(history.back()->note_path == "four.md", "capacity keeps latest navigation visits");
}

void test_command_exceptions_do_not_escape_or_corrupt_stacks()
{
  NoteHistory::HistoryManager history;
  history.push_executed(std::make_unique<NoteHistory::LambdaCommand>(
      "throwing", "test", []() { throw std::runtime_error("redo failed"); },
      []() { throw std::runtime_error("undo failed"); }));

  expect_true(!history.undo(), "throwing undo reports failure");
  expect_true(history.can_undo(), "failed undo command stays on undo stack");
  expect_true(!history.can_redo(), "failed undo does not enter redo stack");
  expect_true(history.last_error() == "undo failed", "failed undo exposes the error");

  NoteHistory::HistoryManager redo_history;
  int value = 0;
  redo_history.push_executed(std::make_unique<NoteHistory::LambdaCommand>(
      "redo throwing", "test", []() { throw std::runtime_error("redo failed"); },
      [&value]() { value = 0; }));
  value = 1;
  expect_true(redo_history.undo(), "setup undo succeeds");
  expect_true(!redo_history.redo(), "throwing redo reports failure");
  expect_true(redo_history.can_redo(), "failed redo command stays on redo stack");
  expect_true(redo_history.last_error() == "redo failed", "failed redo exposes the error");
}

void test_clear_resets_stacks()
{
  NoteHistory::HistoryManager history;
  int value = 0;

  push_executed_command(history, "command", value, 1, -1);
  expect_true(history.undo(), "undo before clear succeeds");
  history.clear();

  expect_true(!history.can_undo(), "clear removes undo stack");
  expect_true(!history.can_redo(), "clear removes redo stack");
  expect_true(history.debug_undo_entries().empty(), "clear removes undo debug entries");
  expect_true(history.debug_redo_entries().empty(), "clear removes redo debug entries");
}
} // namespace

int main()
{
  test_empty_history();
  test_undo_redo_round_trip();
  test_new_command_clears_redo_stack();
  test_stack_limit_keeps_latest_commands();
  test_navigation_history_back_forward_and_divergence();
  test_command_exceptions_do_not_escape_or_corrupt_stacks();
  test_clear_resets_stacks();

  if(failures != 0)
  {
    std::cerr << failures << " note_history test expectation(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "note_history tests passed\n";
  return EXIT_SUCCESS;
}
