#include "sync_coordinator.hpp"

#include <utility>

namespace notepp::sync_coordinator
{
namespace
{
template <typename Action, typename Fallback>
auto invoke(Result &result, const Action &action, Fallback fallback)
{
  try
  {
    return action();
  }
  catch(...)
  {
    result.exception_caught = true;
    return fallback;
  }
}

void invoke_void(Result &result, const VoidAction &action)
{
  try
  {
    action();
  }
  catch(...)
  {
    result.exception_caught = true;
  }
}
} // namespace

Result open(bool enabled, const GitAction &pull, const VoidAction &load)
{
  Result result;
  if(enabled)
  {
    result.git_attempted = true;
    const auto [success, changed] = invoke(result, pull, std::pair{false, false});
    result.git_succeeded = success;
    result.worktree_changed = changed;
  }
  invoke_void(result, load);
  return result;
}

Result close(bool enabled, const SaveAction &save, const GitAction &push)
{
  Result result;
  result.save_succeeded = invoke(result, save, false);
  if(result.save_succeeded && enabled)
  {
    result.git_attempted = true;
    const auto [success, changed] = invoke(result, push, std::pair{false, false});
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
  result.save_succeeded = invoke(result, save_old, false);
  if(!result.save_succeeded)
  {
    result.continued = false;
    return result;
  }
  if(enabled)
  {
    result.git_attempted = true;
    const auto [push_success, push_changed] =
        invoke(result, push_old, std::pair{false, false});
    const auto [pull_success, pull_changed] =
        invoke(result, pull_new, std::pair{false, false});
    result.git_succeeded = push_success && pull_success;
    result.worktree_changed = push_changed || pull_changed;
  }
  invoke_void(result, load_new);
  return result;
}

Result manual(bool enabled, const SaveAction &save, const GitAction &sync,
              const VoidAction &reload)
{
  Result result;
  result.save_succeeded = invoke(result, save, false);
  if(!result.save_succeeded || !enabled)
  {
    result.continued = false;
    return result;
  }
  result.git_attempted = true;
  const auto [success, changed] = invoke(result, sync, std::pair{false, false});
  result.git_succeeded = success;
  result.worktree_changed = changed;
  if(success && changed)
  {
    invoke_void(result, reload);
    result.reloaded = !result.exception_caught;
  }
  return result;
}
} // namespace notepp::sync_coordinator
