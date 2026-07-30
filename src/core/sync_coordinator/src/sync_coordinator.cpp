#include "sync_coordinator.hpp"

namespace notepp::sync_coordinator
{
Result open(bool enabled, const GitAction &pull, const VoidAction &load)
{
  Result result;
  if(enabled)
  {
    result.git_attempted = true;
    const auto [success, changed] = pull();
    result.git_succeeded = success;
    result.worktree_changed = changed;
  }
  load();
  return result;
}

Result close(bool enabled, const SaveAction &save, const GitAction &push)
{
  Result result;
  result.save_succeeded = save();
  if(result.save_succeeded && enabled)
  {
    result.git_attempted = true;
    const auto [success, changed] = push();
    result.git_succeeded = success;
    result.worktree_changed = changed;
  }
  return result;
}

Result switch_project(bool enabled, const SaveAction &save_old,
                      const GitAction &push_old, const GitAction &pull_new,
                      const VoidAction &load_new)
{
  Result result;
  result.save_succeeded = save_old();
  if(!result.save_succeeded)
  {
    result.continued = false;
    return result;
  }
  if(enabled)
  {
    result.git_attempted = true;
    const auto [push_success, push_changed] = push_old();
    const auto [pull_success, pull_changed] = pull_new();
    result.git_succeeded = push_success && pull_success;
    result.worktree_changed = push_changed || pull_changed;
  }
  load_new();
  return result;
}

Result manual(bool enabled, const SaveAction &save, const GitAction &sync,
              const VoidAction &reload)
{
  Result result;
  result.save_succeeded = save();
  if(!result.save_succeeded || !enabled)
  {
    result.continued = false;
    return result;
  }
  result.git_attempted = true;
  const auto [success, changed] = sync();
  result.git_succeeded = success;
  result.worktree_changed = changed;
  if(success && changed)
  {
    reload();
    result.reloaded = true;
  }
  return result;
}
} // namespace notepp::sync_coordinator
