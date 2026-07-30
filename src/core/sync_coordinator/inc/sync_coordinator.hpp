#pragma once

#include <functional>

namespace notepp::sync_coordinator
{
struct Result
{
  bool continued = true;
  bool save_succeeded = true;
  bool git_attempted = false;
  bool git_succeeded = true;
  bool worktree_changed = false;
  bool reloaded = false;
  bool exception_caught = false;
};

using SaveAction = std::function<bool()>;
using GitAction = std::function<std::pair<bool, bool>()>;
using VoidAction = std::function<void()>;

constexpr bool project_writes_allowed(bool git_sync_in_progress) noexcept
{
  return !git_sync_in_progress;
}

enum class CloseAction
{
  wait,
  persist,
  start_git,
  exit
};

constexpr CloseAction next_close_action(bool requested, bool git_active,
                                        bool persisted, bool git_enabled,
                                        bool save_succeeded) noexcept
{
  if(!requested || git_active) return CloseAction::wait;
  if(!persisted) return CloseAction::persist;
  if(git_enabled && save_succeeded) return CloseAction::start_git;
  return CloseAction::exit;
}

[[nodiscard]] Result open(bool enabled, const GitAction &pull, const VoidAction &load);
[[nodiscard]] Result close(bool enabled, const SaveAction &save, const GitAction &push);
[[nodiscard]] Result switch_project(bool enabled, const SaveAction &save_old,
                                    const GitAction &push_old, const GitAction &pull_new,
                                    const VoidAction &load_new);
[[nodiscard]] Result manual(bool enabled, const SaveAction &save, const GitAction &sync,
                            const VoidAction &reload);
} // namespace notepp::sync_coordinator
