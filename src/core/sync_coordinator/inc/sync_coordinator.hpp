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
};

using SaveAction = std::function<bool()>;
using GitAction = std::function<std::pair<bool, bool>()>;
using VoidAction = std::function<void()>;

[[nodiscard]] Result open(bool enabled, const GitAction &pull, const VoidAction &load);
[[nodiscard]] Result close(bool enabled, const SaveAction &save, const GitAction &push);
[[nodiscard]] Result switch_project(bool enabled, const SaveAction &save_old,
                                    const GitAction &push_old, const GitAction &pull_new,
                                    const VoidAction &load_new);
[[nodiscard]] Result manual(bool enabled, const SaveAction &save, const GitAction &sync,
                            const VoidAction &reload);
} // namespace notepp::sync_coordinator
