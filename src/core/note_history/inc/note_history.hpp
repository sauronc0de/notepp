#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NoteHistory
{
struct DebugEntry
{
  std::string label;
  std::string context;
};

struct NavigationLocation
{
  std::string note_path;
  bool editing = false;
  std::size_t source_offset = 0;
  float scroll_x = 0.0F;
  float scroll_y = 0.0F;
  int cursor = -1;

  bool operator==(const NavigationLocation &) const = default;
};

class NavigationHistory
{
public:
  explicit NavigationHistory(std::size_t limit = 128);
  void clear();
  void visit(NavigationLocation location);
  std::optional<NavigationLocation> back();
  std::optional<NavigationLocation> forward();
  bool can_back() const noexcept;
  bool can_forward() const noexcept;

private:
  std::size_t limit_ = 128;
  std::vector<NavigationLocation> entries_;
  std::size_t current_ = 0;
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
  std::string_view last_error() const noexcept;

private:
  void trim_if_needed(std::vector<std::unique_ptr<Command>> &stack);

  size_t limit_ = 128;
  std::vector<std::unique_ptr<Command>> undo_stack_;
  std::vector<std::unique_ptr<Command>> redo_stack_;
  std::string last_error_;
};
} // namespace NoteHistory
