#include "undo_redo.hpp"

#include <utility>

namespace UndoRedo
{
LambdaCommand::LambdaCommand(std::string label, std::string debug_context, Fn execute, Fn undo)
    : label_(std::move(label)),
      debug_context_(std::move(debug_context)),
      execute_(std::move(execute)),
      undo_(std::move(undo))
{
}

void LambdaCommand::execute()
{
  if(execute_) execute_();
}

void LambdaCommand::undo()
{
  if(undo_) undo_();
}

const std::string &LambdaCommand::label() const
{
  return label_;
}

const std::string &LambdaCommand::debug_context() const
{
  return debug_context_;
}

HistoryManager::HistoryManager(size_t limit)
    : limit_(limit > 0 ? limit : 1)
{
}

void HistoryManager::clear()
{
  undo_stack_.clear();
  redo_stack_.clear();
}

void HistoryManager::push_executed(std::unique_ptr<Command> command)
{
  if(!command) return;
  redo_stack_.clear();
  trim_if_needed(undo_stack_);
  undo_stack_.push_back(std::move(command));
}

bool HistoryManager::undo()
{
  if(undo_stack_.empty()) return false;

  std::unique_ptr<Command> command = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  command->undo();
  trim_if_needed(redo_stack_);
  redo_stack_.push_back(std::move(command));
  return true;
}

bool HistoryManager::redo()
{
  if(redo_stack_.empty()) return false;

  std::unique_ptr<Command> command = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  command->execute();
  trim_if_needed(undo_stack_);
  undo_stack_.push_back(std::move(command));
  return true;
}

bool HistoryManager::can_undo() const
{
  return !undo_stack_.empty();
}

bool HistoryManager::can_redo() const
{
  return !redo_stack_.empty();
}

std::string_view HistoryManager::next_undo_label() const
{
  if(undo_stack_.empty() || !undo_stack_.back()) return {};
  return undo_stack_.back()->label();
}

std::string_view HistoryManager::next_redo_label() const
{
  if(redo_stack_.empty() || !redo_stack_.back()) return {};
  return redo_stack_.back()->label();
}

std::vector<DebugEntry> HistoryManager::debug_undo_entries() const
{
  std::vector<DebugEntry> entries;
  entries.reserve(undo_stack_.size());
  for(auto it = undo_stack_.rbegin(); it != undo_stack_.rend(); ++it)
  {
    if(!(*it)) continue;
    entries.push_back(DebugEntry{(*it)->label(), (*it)->debug_context()});
  }
  return entries;
}

std::vector<DebugEntry> HistoryManager::debug_redo_entries() const
{
  std::vector<DebugEntry> entries;
  entries.reserve(redo_stack_.size());
  for(auto it = redo_stack_.rbegin(); it != redo_stack_.rend(); ++it)
  {
    if(!(*it)) continue;
    entries.push_back(DebugEntry{(*it)->label(), (*it)->debug_context()});
  }
  return entries;
}

void HistoryManager::trim_if_needed(std::vector<std::unique_ptr<Command>> &stack)
{
  if(stack.size() < limit_) return;
  stack.erase(stack.begin());
}
} // namespace UndoRedo
