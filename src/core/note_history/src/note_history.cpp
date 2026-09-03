#include "note_history.hpp"

#include <utility>

namespace NoteHistory
{
NavigationHistory::NavigationHistory(std::size_t limit)
    : limit_(limit == 0U ? 1U : limit)
{
}

void NavigationHistory::clear()
{
  entries_.clear();
  current_ = 0U;
}

void NavigationHistory::visit(NavigationLocation location)
{
  if(location.note_path.empty()) return;
  if(!entries_.empty() && entries_[current_] == location) return;
  if(!entries_.empty() && current_ + 1U < entries_.size())
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(current_ + 1U), entries_.end());
  entries_.push_back(std::move(location));
  if(entries_.size() > limit_) entries_.erase(entries_.begin());
  current_ = entries_.size() - 1U;
}

std::optional<NavigationLocation> NavigationHistory::back()
{
  if(!can_back()) return std::nullopt;
  --current_;
  return entries_[current_];
}

std::optional<NavigationLocation> NavigationHistory::forward()
{
  if(!can_forward()) return std::nullopt;
  ++current_;
  return entries_[current_];
}

bool NavigationHistory::can_back() const noexcept
{
  return !entries_.empty() && current_ > 0U;
}

bool NavigationHistory::can_forward() const noexcept
{
  return !entries_.empty() && current_ + 1U < entries_.size();
}

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
  last_error_.clear();
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
  last_error_.clear();
  if(undo_stack_.empty()) return false;

  std::unique_ptr<Command> command = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  try
  {
    command->undo();
  }
  catch(const std::exception &error)
  {
    last_error_ = error.what();
    undo_stack_.push_back(std::move(command));
    return false;
  }
  catch(...)
  {
    last_error_ = "history command failed with an unknown exception";
    undo_stack_.push_back(std::move(command));
    return false;
  }
  trim_if_needed(redo_stack_);
  redo_stack_.push_back(std::move(command));
  return true;
}

bool HistoryManager::redo()
{
  last_error_.clear();
  if(redo_stack_.empty()) return false;

  std::unique_ptr<Command> command = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  try
  {
    command->execute();
  }
  catch(const std::exception &error)
  {
    last_error_ = error.what();
    redo_stack_.push_back(std::move(command));
    return false;
  }
  catch(...)
  {
    last_error_ = "history command failed with an unknown exception";
    redo_stack_.push_back(std::move(command));
    return false;
  }
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

std::string_view HistoryManager::last_error() const noexcept
{
  return last_error_;
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
} // namespace NoteHistory
