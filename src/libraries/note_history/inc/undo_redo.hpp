#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace UndoRedo
{
struct DebugEntry
{
  std::string label;
  std::string context;
};

class Command
{
public:
  virtual ~Command() = default;

  virtual void execute() = 0;
  virtual void undo() = 0;
  virtual const std::string &label() const = 0;
  virtual const std::string &debug_context() const = 0;
};

class LambdaCommand final : public Command
{
public:
  using Fn = std::function<void()>;

  LambdaCommand(std::string label, std::string debug_context, Fn execute, Fn undo);

  void execute() override;
  void undo() override;
  const std::string &label() const override;
  const std::string &debug_context() const override;

private:
  std::string label_;
  std::string debug_context_;
  Fn execute_;
  Fn undo_;
};

class HistoryManager
{
public:
  explicit HistoryManager(size_t limit = 128);

  void clear();
  void push_executed(std::unique_ptr<Command> command);
  bool undo();
  bool redo();

  bool can_undo() const;
  bool can_redo() const;
  std::string_view next_undo_label() const;
  std::string_view next_redo_label() const;
  std::vector<DebugEntry> debug_undo_entries() const;
  std::vector<DebugEntry> debug_redo_entries() const;

private:
  void trim_if_needed(std::vector<std::unique_ptr<Command>> &stack);

  size_t limit_ = 128;
  std::vector<std::unique_ptr<Command>> undo_stack_;
  std::vector<std::unique_ptr<Command>> redo_stack_;
};
} // namespace UndoRedo
