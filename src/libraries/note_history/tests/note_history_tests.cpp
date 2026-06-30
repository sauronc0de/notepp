#include "undo_redo.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace
{
int failures = 0;

void expect_true(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

std::unique_ptr<UndoRedo::Command> make_command(
    std::string_view label,
    int &value,
    int execute_delta,
    int undo_delta)
{
  return std::make_unique<UndoRedo::LambdaCommand>(
      std::string(label),
      std::string("debug-") + std::string(label),
      [&value, execute_delta]() { value += execute_delta; },
      [&value, undo_delta]() { value += undo_delta; });
}

void push_executed_command(UndoRedo::HistoryManager &history,
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
  UndoRedo::HistoryManager history;

  expect_true(!history.can_undo(), "new history cannot undo");
  expect_true(!history.can_redo(), "new history cannot redo");
  expect_true(!history.undo(), "undo on empty history returns false");
  expect_true(!history.redo(), "redo on empty history returns false");
  expect_true(history.next_undo_label().empty(), "empty undo label");
  expect_true(history.next_redo_label().empty(), "empty redo label");
}

void test_undo_redo_round_trip()
{
  UndoRedo::HistoryManager history;
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
  UndoRedo::HistoryManager history;
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
  UndoRedo::HistoryManager history(2);
  int value = 0;

  push_executed_command(history, "one", value, 1, -1);
  push_executed_command(history, "two", value, 1, -1);
  push_executed_command(history, "three", value, 1, -1);

  const auto entries = history.debug_undo_entries();
  expect_true(entries.size() == 2, "history respects stack limit");
  expect_true(entries.size() >= 1 && entries[0].label == "three", "latest command retained first in debug list");
  expect_true(entries.size() >= 2 && entries[1].label == "two", "older retained command is second latest");
}

void test_clear_resets_stacks()
{
  UndoRedo::HistoryManager history;
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
  test_clear_resets_stacks();

  if(failures != 0)
  {
    std::cerr << failures << " note_history test expectation(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "note_history tests passed\n";
  return EXIT_SUCCESS;
}
