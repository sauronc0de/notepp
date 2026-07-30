#include "git_sync.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <sstream>
#include <system_error>
#include <variant>
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
  auto sensitive_line = [](std::string_view line) {
    for(const std::string_view key : {"authorization", "extraheader", "access_token", "password=",
                                      "credential="})
      if(std::search(line.begin(), line.end(), key.begin(), key.end(), [](char left, char right) {
           return std::tolower(static_cast<unsigned char>(left)) ==
                  std::tolower(static_cast<unsigned char>(right));
         }) != line.end())
        return true;
    return false;
  };
  std::size_t line_start = 0;
  while(line_start < value.size())
  {
    const std::size_t line_end = value.find('\n', line_start);
    const std::size_t length = (line_end == std::string::npos ? value.size() : line_end) - line_start;
    if(sensitive_line(std::string_view(value).substr(line_start, length)))
    {
      value.replace(line_start, length, "[redacted diagnostic]");
      line_start += 21U;
    }
    else
      line_start += length;
    if(line_start < value.size() && value[line_start] == '\n') ++line_start;
  }

  // Remote URLs can carry credentials in the authority, path, or query.
  // Diagnostics do not need the URL itself, so redact each URL wholesale.
  std::size_t search = 0;
  while((search = value.find("://", search)) != std::string::npos)
  {
    std::size_t start = search;
    while(start > 0 && !std::isspace(static_cast<unsigned char>(value[start - 1])) &&
          value[start - 1] != '\'' && value[start - 1] != '"')
      --start;
    std::size_t end = search + 3U;
    while(end < value.size() && !std::isspace(static_cast<unsigned char>(value[end])) &&
          value[end] != '\'' && value[end] != '"')
      ++end;
    value.replace(start, end - start, "[redacted-url]");
    search = start + 14U;
  }

  // Redact common non-URL secret assignments without trying to preserve the
  // secret-bearing diagnostic verbatim.
  for(const std::string_view key : {"access_token=", "token=", "password=", "authorization:"})
  {
    std::size_t position = 0;
    while((position = std::search(value.begin() + static_cast<std::ptrdiff_t>(position), value.end(),
                                  key.begin(), key.end(), [](char left, char right) {
                                    return std::tolower(static_cast<unsigned char>(left)) ==
                                           std::tolower(static_cast<unsigned char>(right));
                                  }) -
                      value.begin()) < value.size())
    {
      const std::size_t secret_start = position + key.size();
      const std::size_t secret_end = value.find_first_of(" \t\r\n&", secret_start);
      value.replace(secret_start, (secret_end == std::string::npos ? value.size() : secret_end) - secret_start,
                    "***");
      position = secret_start + 3U;
    }
  }

  search = 0;
  while((search = value.find('@', search)) != std::string::npos)
  {
    const std::size_t colon = value.find(':', search + 1U);
    const std::size_t token_end = value.find_first_of(" \t\r\n'\"", search);
    if(colon != std::string::npos && (token_end == std::string::npos || colon < token_end))
    {
      const std::size_t token_start_marker = value.find_last_of(" \t\r\n'\"", search);
      const std::size_t token_start = token_start_marker == std::string::npos ? 0U : token_start_marker + 1U;
      const std::size_t end = token_end == std::string::npos ? value.size() : token_end;
      value.replace(token_start, end - token_start, "[redacted-remote]");
      search = token_start + 17U;
    }
    else
      ++search;
  }

  constexpr std::size_t kMaximumDetail = 1024;
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

bool is_authentication_error(std::string_view detail)
{
  for(const std::string_view token : {"authentication failed", "permission denied", "http 401", "http 403",
                                      "requested url returned error: 401", "requested url returned error: 403",
                                      "could not read username"})
    if(contains_case_insensitive(detail, token)) return true;
  return false;
}

bool is_tls_error(std::string_view detail)
{
  for(const std::string_view token : {"ssl certificate", "certificate verify", "tls certificate",
                                      "server certificate verification failed", "schannel:"})
    if(contains_case_insensitive(detail, token)) return true;
  return false;
}

bool is_offline_error(std::string_view detail)
{
  for(const std::string_view token : {"could not resolve", "connection timed out", "connection refused",
                                      "network is unreachable", "no route to host", "ssh: connect",
                                      "failed to connect"})
    if(contains_case_insensitive(detail, token)) return true;
  return false;
}

process::Result run_git(const process::Runner &runner, const std::filesystem::path &root,
                        std::vector<std::string> arguments, bool network)
{
  std::vector<std::string> full_arguments{
      "-c", "credential.interactive=never",
      "-c", "core.askPass=",
      "-c", "commit.gpgSign=false",
      "-c", "tag.gpgSign=false"};
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
  options.environment_overrides["GIT_ASKPASS"] = "git";
  options.environment_overrides["SSH_ASKPASS"] = "git";
  options.environment_overrides["SSH_ASKPASS_REQUIRE"] = "never";
  options.environment_overrides["GIT_SSH_COMMAND"] =
      "ssh -oBatchMode=yes -oNumberOfPasswordPrompts=0";
  return runner.run("git", full_arguments, options);
}

Status command_failure(const process::Result &result, std::string_view action, bool network)
{
  Status status;
  status.checked_at = std::chrono::system_clock::now();
  std::string detail = trim(!result.stderr_text.empty() ? result.stderr_text : result.error);
  if(result.termination == process::Termination::spawn_failed)
  {
    status.state = SyncState::unavailable;
    status.summary = "System Git is unavailable";
  }
  else if(network && (result.termination == process::Termination::timed_out || is_offline_error(detail)))
  {
    status.state = SyncState::offline;
    status.summary = "Git sync could not reach the remote";
    detail = "The remote could not be reached. Check the network connection and try again.";
  }
  else
  {
    status.state = SyncState::error;
    status.summary = std::string(action) + " failed";
    if(is_authentication_error(detail))
      detail = "Git authentication was rejected. Check the system Git credentials.";
    else if(is_tls_error(detail))
      detail = "Git TLS or certificate validation failed. Check the system Git configuration.";
  }
  status.detail = redact(std::move(detail));
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

struct Upstream
{
  std::string remote;
  std::string merge_ref;
};

bool valid_upstream_component(std::string_view value)
{
  if(value.empty() || value.front() == '-') return false;
  return std::none_of(value.begin(), value.end(), [](char character) {
    return std::iscntrl(static_cast<unsigned char>(character)) != 0;
  });
}

std::variant<Upstream, Status> resolve_upstream(const process::Runner &runner,
                                                const std::filesystem::path &root,
                                                std::string_view branch)
{
  process::Result remote = run_git(runner, root,
                                   {"config", "--get", "branch." + std::string(branch) + ".remote"}, false);
  if(!remote.succeeded()) return operation_error(remote, "Reading the upstream remote", false);
  process::Result merge = run_git(runner, root,
                                  {"config", "--get", "branch." + std::string(branch) + ".merge"}, false);
  if(!merge.succeeded()) return operation_error(merge, "Reading the upstream branch", false);

  Upstream upstream{trim(std::move(remote.stdout_text)), trim(std::move(merge.stdout_text))};
  if(upstream.remote == "." || !valid_upstream_component(upstream.remote) ||
     !upstream.merge_ref.starts_with("refs/heads/") || !valid_upstream_component(upstream.merge_ref))
  {
    Status status;
    status.state = SyncState::no_upstream;
    status.summary = "The current branch has no supported remote upstream";
    status.detail = "Configure the branch to track a branch on a named Git remote.";
    status.checked_at = std::chrono::system_clock::now();
    return status;
  }
  return upstream;
}

process::Result fetch_upstream(const process::Runner &runner, const std::filesystem::path &root,
                               const Upstream &upstream)
{
  return run_git(runner, root, {"fetch", "--no-tags", "--", upstream.remote}, true);
}

process::Result pull_upstream(const process::Runner &runner, const std::filesystem::path &root,
                              const Upstream &upstream)
{
  return run_git(runner, root,
                 {"pull", "--ff-only", "--", upstream.remote, upstream.merge_ref}, true);
}

process::Result push_upstream(const process::Runner &runner, const std::filesystem::path &root,
                              const Upstream &upstream)
{
  return run_git(runner, root,
                 {"push", "--porcelain", "--", upstream.remote, "HEAD:" + upstream.merge_ref}, true);
}

std::vector<std::string> project_pathspec()
{
  return {"notes", "assets", "config", "notepp.project.json",
          ":(exclude,glob)**/*.bak", ":(exclude,glob)**/*.~npp-t-*",
          ":(exclude,glob)**/*.tmp"};
}

std::vector<std::string> status_pathspec()
{
  return {".", ":(exclude,glob)**/*.bak", ":(exclude,glob)**/*.~npp-t-*",
          ":(exclude,glob)**/*.tmp"};
}

bool synchronized(const Status &status)
{
  return status.state == SyncState::clean;
}

std::optional<Status> require_syncable(const Status &status, bool allow_dirty)
{
  if(allow_dirty && status.behind > 0) return status;
  switch(status.state)
  {
  case SyncState::clean:
  case SyncState::ahead:
    return std::nullopt;
  case SyncState::behind:
    if(!allow_dirty) return std::nullopt;
    break;
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
  std::vector<std::string> add_arguments = {"add", "--all", "--"};
  const auto pathspec = project_pathspec();
  add_arguments.insert(add_arguments.end(), pathspec.begin(), pathspec.end());
  process::Result result = run_git(runner, root, std::move(add_arguments), false);
  if(!result.succeeded()) return operation_error(result, "Staging project files", false);

  std::vector<std::string> list_arguments = {"diff", "--cached", "--name-only", "-z", "--"};
  list_arguments.insert(list_arguments.end(), pathspec.begin(), pathspec.end());
  result = run_git(runner, root, std::move(list_arguments), false);
  if(!result.succeeded()) return operation_error(result, "Inspecting staged project files", false);

  std::vector<std::string> staged_paths;
  std::size_t position = 0;
  while(position < result.stdout_text.size())
  {
    const std::size_t end = result.stdout_text.find('\0', position);
    const std::size_t length = (end == std::string::npos ? result.stdout_text.size() : end) - position;
    if(length != 0) staged_paths.push_back(result.stdout_text.substr(position, length));
    if(end == std::string::npos) break;
    position = end + 1;
  }
  if(staged_paths.empty()) return std::nullopt;

  std::vector<std::string> commit_arguments = {"commit", "--no-gpg-sign", "--only", "-m",
                                               std::string(message), "--"};
  commit_arguments.insert(commit_arguments.end(), staged_paths.begin(), staged_paths.end());
  result = run_git(runner, root, std::move(commit_arguments), false);
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

  std::vector<std::string> status_arguments = {"status", "--porcelain=v2", "--branch", "--"};
  const auto pathspec = status_pathspec();
  status_arguments.insert(status_arguments.end(), pathspec.begin(), pathspec.end());
  result = run_git(runner_, project_root, std::move(status_arguments), false);
  if(!result.succeeded()) return command_failure(result, "Reading Git status", false);
  return parse_status(result.stdout_text);
}

OperationResult Client::pull_on_open(const std::filesystem::path &project_root) const
{
  const Status before = inspect(project_root);
  if(const auto rejected = require_syncable(before, false)) return failure(*rejected);
  const auto resolved = resolve_upstream(runner_, project_root, before.branch);
  if(std::holds_alternative<Status>(resolved)) return failure(std::get<Status>(resolved));
  const Upstream &upstream = std::get<Upstream>(resolved);

  const process::Result head_before = run_git(runner_, project_root, {"rev-parse", "HEAD"}, false);
  if(!head_before.succeeded()) return failure(operation_error(head_before, "Reading the current revision", false));

  const process::Result pulled = pull_upstream(runner_, project_root, upstream);
  if(!pulled.succeeded()) return failure(operation_error(pulled, "Fast-forward pull", true));

  const process::Result head_after = run_git(runner_, project_root, {"rev-parse", "HEAD"}, false);
  if(!head_after.succeeded()) return failure(operation_error(head_after, "Reading the updated revision", false));
  const bool changed = trim(head_before.stdout_text) != trim(head_after.stdout_text);
  Status final_status = inspect(project_root);
  if(!synchronized(final_status)) return failure(std::move(final_status));
  return success(std::move(final_status), changed);
}

OperationResult Client::commit_and_push(const std::filesystem::path &project_root,
                                        std::string_view message) const
{
  const Status before = inspect(project_root);
  if(const auto rejected = require_syncable(before, true)) return failure(*rejected);
  const auto resolved = resolve_upstream(runner_, project_root, before.branch);
  if(std::holds_alternative<Status>(resolved)) return failure(std::get<Status>(resolved));
  const Upstream &upstream = std::get<Upstream>(resolved);

  // Commit the checked local project first so an offline close retains a local
  // recovery point. Fetch may then reveal divergence, in which case both
  // histories are preserved and no push is attempted.
  if(const auto error = stage_and_commit(runner_, project_root, message)) return failure(*error);

  const process::Result fetched = fetch_upstream(runner_, project_root, upstream);
  if(!fetched.succeeded()) return failure(operation_error(fetched, "Git fetch", true));
  const Status after_fetch = inspect(project_root);
  if(const auto rejected = require_syncable(after_fetch, true)) return failure(*rejected);

  const process::Result pushed = push_upstream(runner_, project_root, upstream);
  if(!pushed.succeeded()) return failure(operation_error(pushed, "Git push", true));
  Status final_status = inspect(project_root);
  if(!synchronized(final_status)) return failure(std::move(final_status));
  return success(std::move(final_status));
}

OperationResult Client::manual_sync(const std::filesystem::path &project_root,
                                    std::string_view message) const
{
  const Status before = inspect(project_root);
  if(const auto rejected = require_syncable(before, true)) return failure(*rejected);
  const auto resolved = resolve_upstream(runner_, project_root, before.branch);
  if(std::holds_alternative<Status>(resolved)) return failure(std::get<Status>(resolved));
  const Upstream &upstream = std::get<Upstream>(resolved);

  const process::Result fetched = fetch_upstream(runner_, project_root, upstream);
  if(!fetched.succeeded()) return failure(operation_error(fetched, "Git fetch", true));
  Status status = inspect(project_root);
  if(status.behind > 0 && status.has_uncommitted_changes) return failure(std::move(status));
  if(status.state == SyncState::behind)
  {
    const process::Result head_before = run_git(runner_, project_root, {"rev-parse", "HEAD"}, false);
    if(!head_before.succeeded()) return failure(operation_error(head_before, "Reading the current revision", false));
    const process::Result pulled = pull_upstream(runner_, project_root, upstream);
    if(!pulled.succeeded()) return failure(operation_error(pulled, "Fast-forward pull", true));
    const process::Result head_after = run_git(runner_, project_root, {"rev-parse", "HEAD"}, false);
    if(!head_after.succeeded()) return failure(operation_error(head_after, "Reading the updated revision", false));
    Status final_status = inspect(project_root);
    if(!synchronized(final_status)) return failure(std::move(final_status));
    return success(std::move(final_status), trim(head_before.stdout_text) != trim(head_after.stdout_text));
  }
  if(status.state == SyncState::dirty)
  {
    if(const auto error = stage_and_commit(runner_, project_root, message)) return failure(*error);
    status = inspect(project_root);
  }
  if(status.state == SyncState::ahead)
  {
    const process::Result pushed = push_upstream(runner_, project_root, upstream);
    if(!pushed.succeeded()) return failure(operation_error(pushed, "Git push", true));
    Status final_status = inspect(project_root);
    if(!synchronized(final_status)) return failure(std::move(final_status));
    return success(std::move(final_status));
  }
  if(status.state == SyncState::clean) return success(std::move(status));
  return failure(std::move(status));
}

OperationResult exception_result(std::string_view action, std::string_view detail)
{
  OperationResult result;
  result.status.state = SyncState::error;
  result.status.summary = std::string(action) + " failed unexpectedly";
  result.status.detail = detail.empty() ? "The operation was stopped; local files remain available."
                                        : redact(std::string(detail));
  result.status.checked_at = std::chrono::system_clock::now();
  return result;
}

SyncState state_from_name(std::string_view name) noexcept
{
  for(const SyncState state : {SyncState::unavailable, SyncState::not_repository,
                               SyncState::no_upstream, SyncState::clean, SyncState::dirty,
                               SyncState::ahead, SyncState::behind, SyncState::diverged,
                               SyncState::syncing, SyncState::offline, SyncState::conflict,
                               SyncState::error})
    if(state_name(state) == name) return state;
  return SyncState::error;
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
