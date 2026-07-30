#include "git_sync.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

namespace notepp::git_sync
{
namespace
{
constexpr auto kLocalTimeout = std::chrono::seconds(5);
constexpr auto kNetworkTimeout = std::chrono::seconds(30);

std::string trim(std::string value)
{
  while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  std::size_t start = 0;
  while(start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
  value.erase(0, start);
  return value;
}

std::string redact(std::string value)
{
  std::size_t search = 0;
  while((search = value.find("://", search)) != std::string::npos)
  {
    const std::size_t credentials_start = search + 3U;
    const std::size_t end = value.find_first_of("/ \t\r\n", credentials_start);
    const std::size_t at = value.find('@', credentials_start);
    if(at != std::string::npos && (end == std::string::npos || at < end))
    {
      value.replace(credentials_start, at - credentials_start, "***");
      search = credentials_start + 4U;
    }
    else
      search = credentials_start;
  }
  constexpr std::size_t kMaximumDetail = 4096;
  if(value.size() > kMaximumDetail)
  {
    value.resize(kMaximumDetail);
    value += "\n[output truncated]";
  }
  return value;
}

bool contains_case_insensitive(std::string_view text, std::string_view needle)
{
  return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                     [](char left, char right) {
                       return std::tolower(static_cast<unsigned char>(left)) ==
                              std::tolower(static_cast<unsigned char>(right));
                     }) != text.end();
}

bool is_offline_error(std::string_view detail)
{
  for(const std::string_view token : {"could not resolve", "unable to access", "connection timed out",
                                      "connection refused", "network is unreachable", "ssh: connect",
                                      "remote end hung up", "failed to connect"})
    if(contains_case_insensitive(detail, token)) return true;
  return false;
}

process::Result run_git(const process::Runner &runner, const std::filesystem::path &root,
                        std::vector<std::string> arguments, bool network)
{
  std::vector<std::string> full_arguments;
  if(!root.empty())
  {
    full_arguments.emplace_back("-C");
    full_arguments.push_back(root.string());
  }
  full_arguments.insert(full_arguments.end(), arguments.begin(), arguments.end());

  process::RunOptions options;
  options.timeout = network ? kNetworkTimeout : kLocalTimeout;
  options.max_output_bytes = 64U * 1024U;
  options.environment_overrides["LC_ALL"] = "C";
  options.environment_overrides["LANG"] = "C";
  options.environment_overrides["GIT_TERMINAL_PROMPT"] = "0";
  options.environment_overrides["GCM_INTERACTIVE"] = "Never";
  return runner.run("git", full_arguments, options);
}

Status command_failure(const process::Result &result, std::string_view action, bool network)
{
  Status status;
  status.checked_at = std::chrono::system_clock::now();
  std::string detail = !result.stderr_text.empty() ? result.stderr_text : result.error;
  detail = redact(trim(std::move(detail)));
  if(result.termination == process::Termination::spawn_failed)
  {
    status.state = SyncState::unavailable;
    status.summary = "System Git is unavailable";
  }
  else if(network && (result.termination == process::Termination::timed_out || is_offline_error(detail)))
  {
    status.state = SyncState::offline;
    status.summary = "Git sync could not reach the remote";
  }
  else
  {
    status.state = SyncState::error;
    status.summary = std::string(action) + " failed";
  }
  status.detail = std::move(detail);
  return status;
}

std::optional<int> parse_nonnegative(std::string_view text)
{
  int value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if(parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < 0) return std::nullopt;
  return value;
}

Status parse_status(std::string_view output)
{
  Status status;
  status.checked_at = std::chrono::system_clock::now();
  bool has_upstream = false;
  bool detached = false;
  bool conflict = false;

  std::istringstream lines{std::string(output)};
  std::string line;
  while(std::getline(lines, line))
  {
    if(line.starts_with("# branch.head "))
    {
      status.branch = line.substr(14);
      detached = status.branch == "(detached)";
    }
    else if(line.starts_with("# branch.upstream "))
      has_upstream = true;
    else if(line.starts_with("# branch.ab "))
    {
      std::istringstream values(line.substr(12));
      std::string ahead;
      std::string behind;
      values >> ahead >> behind;
      if(ahead.starts_with('+'))
        if(const auto parsed = parse_nonnegative(std::string_view(ahead).substr(1))) status.ahead = *parsed;
      if(behind.starts_with('-'))
        if(const auto parsed = parse_nonnegative(std::string_view(behind).substr(1))) status.behind = *parsed;
    }
    else if(line.starts_with("u "))
    {
      conflict = true;
      status.has_uncommitted_changes = true;
    }
    else if(line.starts_with("1 ") || line.starts_with("2 ") || line.starts_with("? "))
      status.has_uncommitted_changes = true;
  }

  if(detached || status.branch.empty())
  {
    status.state = SyncState::no_upstream;
    status.summary = "Git requires a named branch";
  }
  else if(!has_upstream)
  {
    status.state = SyncState::no_upstream;
    status.summary = "The current branch has no upstream";
  }
  else if(conflict)
  {
    status.state = SyncState::conflict;
    status.summary = "The repository has unresolved conflicts";
  }
  else if(status.has_uncommitted_changes)
  {
    status.state = SyncState::dirty;
    status.summary = "The project has local changes";
  }
  else if(status.ahead > 0 && status.behind > 0)
  {
    status.state = SyncState::diverged;
    status.summary = "Local and remote histories have diverged";
  }
  else if(status.ahead > 0)
  {
    status.state = SyncState::ahead;
    status.summary = "Local commits are waiting to be pushed";
  }
  else if(status.behind > 0)
  {
    status.state = SyncState::behind;
    status.summary = "Remote commits are waiting to be pulled";
  }
  else
  {
    status.state = SyncState::clean;
    status.summary = "Git project is synchronized";
  }
  return status;
}

bool same_root(const std::filesystem::path &left, const std::filesystem::path &right)
{
  std::error_code left_error;
  std::error_code right_error;
  const auto canonical_left = std::filesystem::weakly_canonical(left, left_error);
  const auto canonical_right = std::filesystem::weakly_canonical(right, right_error);
  return !left_error && !right_error && canonical_left == canonical_right;
}

OperationResult failure(Status status)
{
  return {.success = false, .changed_worktree = false, .status = std::move(status)};
}

OperationResult success(Status status, bool changed = false)
{
  return {.success = true, .changed_worktree = changed, .status = std::move(status)};
}

Status operation_error(const process::Result &result, std::string_view action, bool network)
{
  return command_failure(result, action, network);
}

std::optional<Status> require_syncable(const Status &status, bool allow_dirty)
{
  switch(status.state)
  {
  case SyncState::clean:
  case SyncState::ahead:
  case SyncState::behind:
    return std::nullopt;
  case SyncState::dirty:
    if(allow_dirty) return std::nullopt;
    break;
  default:
    break;
  }
  return status;
}

std::optional<Status> stage_and_commit(const process::Runner &runner,
                                       const std::filesystem::path &root,
                                       std::string_view message)
{
  process::Result result = run_git(runner, root, {"add", "--all", "--", "."}, false);
  if(!result.succeeded()) return operation_error(result, "Staging project files", false);

  result = run_git(runner, root, {"diff", "--cached", "--quiet", "--exit-code"}, false);
  if(result.termination != process::Termination::exited)
    return operation_error(result, "Inspecting staged project files", false);
  if(result.exit_code == 0) return std::nullopt;
  if(result.exit_code != 1) return operation_error(result, "Inspecting staged project files", false);

  result = run_git(runner, root, {"commit", "-m", std::string(message)}, false);
  if(!result.succeeded()) return operation_error(result, "Creating the sync commit", false);
  return std::nullopt;
}
} // namespace

Client::Client(const process::Runner &runner) : runner_(runner) {}

Status Client::inspect(const std::filesystem::path &project_root) const
{
  process::Result result = run_git(runner_, {}, {"--version"}, false);
  if(!result.succeeded()) return command_failure(result, "Checking system Git", false);

  result = run_git(runner_, project_root, {"rev-parse", "--show-toplevel"}, false);
  if(!result.succeeded())
  {
    Status status = command_failure(result, "Inspecting the Git repository", false);
    if(result.termination == process::Termination::exited)
    {
      status.state = SyncState::not_repository;
      status.summary = "The selected project is not a Git repository";
    }
    return status;
  }

  const std::filesystem::path repository_root(trim(result.stdout_text));
  if(!same_root(project_root, repository_root))
  {
    Status status;
    status.state = SyncState::not_repository;
    status.summary = "The selected project must be the Git repository root";
    status.detail = redact(repository_root.string());
    status.checked_at = std::chrono::system_clock::now();
    return status;
  }

  result = run_git(runner_, project_root, {"status", "--porcelain=v2", "--branch"}, false);
  if(!result.succeeded()) return command_failure(result, "Reading Git status", false);
  return parse_status(result.stdout_text);
}

OperationResult Client::pull_on_open(const std::filesystem::path &project_root) const
{
  const Status before = inspect(project_root);
  if(const auto rejected = require_syncable(before, false)) return failure(*rejected);

  const process::Result head_before = run_git(runner_, project_root, {"rev-parse", "HEAD"}, false);
  if(!head_before.succeeded()) return failure(operation_error(head_before, "Reading the current revision", false));

  const process::Result pulled = run_git(runner_, project_root, {"pull", "--ff-only"}, true);
  if(!pulled.succeeded()) return failure(operation_error(pulled, "Fast-forward pull", true));

  const process::Result head_after = run_git(runner_, project_root, {"rev-parse", "HEAD"}, false);
  if(!head_after.succeeded()) return failure(operation_error(head_after, "Reading the updated revision", false));
  const bool changed = trim(head_before.stdout_text) != trim(head_after.stdout_text);
  return success(inspect(project_root), changed);
}

OperationResult Client::commit_and_push(const std::filesystem::path &project_root,
                                        std::string_view message) const
{
  const Status before = inspect(project_root);
  if(const auto rejected = require_syncable(before, true)) return failure(*rejected);
  if(const auto error = stage_and_commit(runner_, project_root, message)) return failure(*error);

  const process::Result pushed = run_git(runner_, project_root, {"push"}, true);
  if(!pushed.succeeded()) return failure(operation_error(pushed, "Git push", true));
  return success(inspect(project_root));
}

OperationResult Client::manual_sync(const std::filesystem::path &project_root,
                                    std::string_view message) const
{
  const Status before = inspect(project_root);
  if(const auto rejected = require_syncable(before, true)) return failure(*rejected);
  if(const auto error = stage_and_commit(runner_, project_root, message)) return failure(*error);

  const process::Result fetched = run_git(runner_, project_root, {"fetch"}, true);
  if(!fetched.succeeded()) return failure(operation_error(fetched, "Git fetch", true));

  Status status = inspect(project_root);
  if(status.state == SyncState::diverged || status.state == SyncState::conflict ||
     status.state == SyncState::dirty)
    return failure(std::move(status));
  if(status.state == SyncState::behind)
  {
    const process::Result pulled = run_git(runner_, project_root, {"pull", "--ff-only"}, true);
    if(!pulled.succeeded()) return failure(operation_error(pulled, "Fast-forward pull", true));
    return success(inspect(project_root), true);
  }
  if(status.state == SyncState::ahead)
  {
    const process::Result pushed = run_git(runner_, project_root, {"push"}, true);
    if(!pushed.succeeded()) return failure(operation_error(pushed, "Git push", true));
    return success(inspect(project_root));
  }
  return success(std::move(status));
}

std::string_view state_name(SyncState state) noexcept
{
  switch(state)
  {
  case SyncState::unavailable:
    return "Git unavailable";
  case SyncState::not_repository:
    return "Not a repository";
  case SyncState::no_upstream:
    return "No upstream";
  case SyncState::clean:
    return "Synced";
  case SyncState::dirty:
    return "Local changes";
  case SyncState::ahead:
    return "Ahead";
  case SyncState::behind:
    return "Behind";
  case SyncState::diverged:
    return "Diverged";
  case SyncState::syncing:
    return "Syncing";
  case SyncState::offline:
    return "Offline";
  case SyncState::conflict:
    return "Conflict";
  case SyncState::error:
    return "Error";
  }
  return "Error";
}
} // namespace notepp::git_sync
