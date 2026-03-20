#include "git_sync.hpp"

#include <array>
#include <cstdio>
#include <sstream>
#include <string_view>

namespace GitSync
{
namespace
{
std::string trim_copy(std::string_view text)
{
  size_t start = 0;
  while(start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n')) ++start;
  size_t end = text.size();
  while(end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) --end;
  return std::string(text.substr(start, end - start));
}

std::string shell_escape(std::string_view value)
{
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('\'');
  for(char c : value)
  {
    if(c == '\'')
      out += "'\"'\"'";
    else
      out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

CommandResult run_command(const std::string &command)
{
  CommandResult result;
  const std::string full = command + " 2>&1";
  FILE *pipe = popen(full.c_str(), "r");
  if(pipe == nullptr)
  {
    result.output = "Failed to start command.";
    return result;
  }

  std::array<char, 4096> buffer{};
  while(fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
  {
    result.output += buffer.data();
  }

  const int status = pclose(pipe);
  result.exit_code = status;
  result.ok = (status == 0);
  result.output = trim_copy(result.output);
  return result;
}

std::vector<std::string> split_lines(const std::string &text)
{
  std::vector<std::string> lines;
  std::istringstream in(text);
  std::string line;
  while(std::getline(in, line))
  {
    line = trim_copy(line);
    if(!line.empty()) lines.push_back(std::move(line));
  }
  return lines;
}
} // namespace

Client::Client(std::string repo_path)
    : repo_path_(std::move(repo_path))
{
}

CommandResult Client::git(std::vector<std::string> args) const
{
  std::string command = "git -C " + shell_escape(repo_path_);
  for(const std::string &arg : args)
  {
    command.push_back(' ');
    command += shell_escape(arg);
  }
  return run_command(command);
}

bool Client::git_available(std::string *message) const
{
  const CommandResult result = run_command("git --version");
  if(message != nullptr) *message = result.output;
  return result.ok;
}

bool Client::ensure_repo(std::string *message) const
{
  if(!git_available(message)) return false;

  const CommandResult has_repo = git({"rev-parse", "--is-inside-work-tree"});
  if(has_repo.ok)
  {
    if(message != nullptr) *message = "Git repository ready.";
    return true;
  }

  const CommandResult init = git({"init", "-b", "main"});
  if(!init.ok)
  {
    const CommandResult fallback = git({"init"});
    if(!fallback.ok)
    {
      if(message != nullptr) *message = fallback.output.empty() ? init.output : fallback.output;
      return false;
    }
  }

  if(message != nullptr) *message = "Initialized local Git repository.";
  return true;
}

RepoStatus Client::query_status(std::string_view branch) const
{
  RepoStatus status;
  status.git_available = git_available();
  if(!status.git_available) return status;

  const CommandResult repo = git({"rev-parse", "--is-inside-work-tree"});
  status.repo_exists = repo.ok;
  if(!status.repo_exists) return status;

  status.remote_url = trim_copy(git({"remote", "get-url", "origin"}).output);
  status.remote_configured = !status.remote_url.empty();
  status.has_commits = git({"rev-parse", "--verify", "HEAD"}).ok;
  status.current_branch = trim_copy(git({"branch", "--show-current"}).output);

  const CommandResult branches = git({"branch", "--format=%(refname:short)"});
  if(branches.ok) status.local_branches = split_lines(branches.output);

  const CommandResult porcelain = git({"status", "--porcelain"});
  status.clean = porcelain.ok && trim_copy(porcelain.output).empty();

  if(status.has_commits)
  {
    status.head_commit = trim_copy(git({"rev-parse", "HEAD"}).output);
  }

  const std::string target_branch = trim_copy(branch);
  if(status.remote_configured && !target_branch.empty())
  {
    const CommandResult remote = git({"rev-parse", "--verify", ("refs/remotes/origin/" + target_branch)});
    status.remote_branch_exists = remote.ok;
    if(remote.ok) status.remote_commit = trim_copy(remote.output);
  }

  return status;
}

bool Client::set_remote_origin(const std::string &remote_url, std::string *message) const
{
  if(!ensure_repo(message)) return false;

  const CommandResult has_origin = git({"remote", "get-url", "origin"});
  CommandResult result;
  if(has_origin.ok)
    result = git({"remote", "set-url", "origin", remote_url});
  else
    result = git({"remote", "add", "origin", remote_url});

  if(message != nullptr) *message = result.output.empty() ? (result.ok ? "Remote repository configured." : "Failed to configure remote repository.") : result.output;
  return result.ok;
}

bool Client::clear_remote_origin(std::string *message) const
{
  const CommandResult has_origin = git({"remote", "get-url", "origin"});
  if(!has_origin.ok)
  {
    if(message != nullptr) *message = "Remote repository already disconnected.";
    return true;
  }

  const CommandResult result = git({"remote", "remove", "origin"});
  if(message != nullptr) *message = result.output.empty() ? (result.ok ? "Remote repository disconnected." : "Failed to disconnect remote repository.") : result.output;
  return result.ok;
}

bool Client::has_remote_branch(const std::string &branch) const
{
  if(branch.empty()) return false;
  const CommandResult result = git({"ls-remote", "--heads", "origin", branch});
  return result.ok && !trim_copy(result.output).empty();
}

SyncResult Client::ensure_initial_commit(const std::string &message) const
{
  SyncResult result;
  if(!ensure_repo(&result.message)) return result;

  const RepoStatus status = query_status();
  if(status.has_commits)
  {
    result.ok = true;
    result.message = "Repository already has commits.";
    return result;
  }

  const CommandResult add = git({"add", "-A"});
  if(!add.ok)
  {
    result.message = add.output;
    return result;
  }

  const CommandResult staged = git({"status", "--porcelain"});
  if(!staged.ok)
  {
    result.message = staged.output;
    return result;
  }
  if(trim_copy(staged.output).empty())
  {
    result.ok = true;
    result.message = "Nothing to commit yet.";
    return result;
  }

  const CommandResult commit = git({"commit", "-m", message});
  result.ok = commit.ok;
  result.message = commit.output.empty() ? (commit.ok ? "Created initial commit." : "Failed to create initial commit.") : commit.output;
  return result;
}

SyncResult Client::fetch() const
{
  SyncResult result;
  const RepoStatus status = query_status();
  if(!status.repo_exists)
  {
    result.message = "Local repository is not initialized.";
    return result;
  }
  if(!status.remote_configured)
  {
    result.ok = true;
    result.message = "Working in local-only mode.";
    return result;
  }

  const CommandResult fetch_result = git({"fetch", "origin", "--prune"});
  result.ok = fetch_result.ok;
  result.message = fetch_result.output.empty() ? (fetch_result.ok ? "Fetched latest remote state." : "Fetch failed.") : fetch_result.output;
  return result;
}

SyncResult Client::pull_latest(const std::string &branch) const
{
  SyncResult result;
  if(branch.empty())
  {
    result.message = "Choose a branch before syncing.";
    return result;
  }

  SyncResult fetched = fetch();
  if(!fetched.ok)
  {
    result.message = fetched.message;
    return result;
  }

  const RepoStatus status = query_status(branch);
  if(!status.remote_configured || !status.remote_branch_exists)
  {
    result.ok = true;
    result.message = status.remote_configured ? "Remote branch does not exist yet." : "Working in local-only mode.";
    return result;
  }

  SyncResult initial_commit = ensure_initial_commit("Initial local notes snapshot");
  if(!initial_commit.ok)
  {
    result.message = initial_commit.message;
    return result;
  }

  const CommandResult merge_base = git({"merge-base", "HEAD", ("origin/" + branch)});
  const std::string base = trim_copy(merge_base.output);
  const std::string local = trim_copy(git({"rev-parse", "HEAD"}).output);
  const std::string remote = trim_copy(git({"rev-parse", ("origin/" + branch)}).output);

  if(!merge_base.ok || base.empty() || local.empty() || remote.empty())
  {
    result.message = "Unable to compare local and remote history.";
    return result;
  }

  const bool ahead = local != base;
  const bool behind = remote != base;
  if(ahead && behind)
  {
    result.conflict = true;
    result.message = "Local notes and remote branch both changed. Choose keep-local or push to a new branch.";
    return result;
  }

  if(!ahead && behind)
  {
    const CommandResult checkout = git({"checkout", branch});
    if(!checkout.ok)
    {
      result.message = checkout.output;
      return result;
    }

    const CommandResult pull = git({"pull", "--ff-only", "origin", branch});
    result.ok = pull.ok;
    result.message = pull.output.empty() ? (pull.ok ? "Pulled the latest notes." : "Pull failed.") : pull.output;
    return result;
  }

  result.ok = true;
  result.message = ahead ? "Local branch is ahead of remote; no pull needed." : "Already up to date.";
  return result;
}

SyncResult Client::commit_all(const std::string &message) const
{
  SyncResult result;
  if(!ensure_repo(&result.message)) return result;

  const CommandResult add = git({"add", "-A"});
  if(!add.ok)
  {
    result.message = add.output;
    return result;
  }

  const CommandResult staged = git({"status", "--porcelain"});
  if(!staged.ok)
  {
    result.message = staged.output;
    return result;
  }
  if(trim_copy(staged.output).empty())
  {
    result.ok = true;
    result.message = "No local note changes to commit.";
    return result;
  }

  const CommandResult commit = git({"commit", "-m", message});
  result.ok = commit.ok;
  result.message = commit.output.empty() ? (commit.ok ? "Committed local note changes." : "Commit failed.") : commit.output;
  return result;
}

SyncResult Client::checkout_branch(const std::string &branch, bool create_if_missing) const
{
  SyncResult result;
  if(branch.empty())
  {
    result.message = "Branch name cannot be empty.";
    return result;
  }
  if(!ensure_repo(&result.message)) return result;

  SyncResult initial_commit = ensure_initial_commit("Initial local notes snapshot");
  if(!initial_commit.ok)
  {
    result.message = initial_commit.message;
    return result;
  }

  SyncResult fetched = fetch();
  if(!fetched.ok && fetched.message != "Working in local-only mode.")
  {
    result.message = fetched.message;
    return result;
  }

  CommandResult command;
  if(has_remote_branch(branch))
    command = git({"checkout", "-B", branch, ("origin/" + branch)});
  else if(create_if_missing)
    command = git({"checkout", "-B", branch});
  else
    command = git({"checkout", branch});

  result.ok = command.ok;
  result.message = command.output.empty() ? (command.ok ? ("Switched to branch '" + branch + "'.") : "Branch checkout failed.") : command.output;
  return result;
}

SyncResult Client::push_branch(const std::string &branch, bool force_with_lease) const
{
  SyncResult result;
  if(branch.empty())
  {
    result.message = "Branch name cannot be empty.";
    return result;
  }

  const RepoStatus status = query_status();
  if(!status.remote_configured)
  {
    result.ok = true;
    result.message = "Working in local-only mode.";
    return result;
  }

  std::vector<std::string> args = {"push", "-u", "origin", branch};
  if(force_with_lease) args.insert(args.begin() + 1, "--force-with-lease");
  const CommandResult push = git(args);
  result.ok = push.ok;
  result.message = push.output.empty() ? (push.ok ? "Pushed notes to remote." : "Push failed.") : push.output;
  if(!push.ok && result.message.find("[rejected]") != std::string::npos) result.conflict = true;
  return result;
}

SyncResult Client::create_tag(const std::string &tag_name, bool push_to_remote) const
{
  SyncResult result;
  if(tag_name.empty())
  {
    result.message = "Tag name cannot be empty.";
    return result;
  }
  if(!ensure_repo(&result.message)) return result;

  SyncResult initial_commit = ensure_initial_commit("Initial local notes snapshot");
  if(!initial_commit.ok)
  {
    result.message = initial_commit.message;
    return result;
  }

  const CommandResult tag = git({"tag", tag_name});
  if(!tag.ok)
  {
    result.message = tag.output;
    return result;
  }

  if(push_to_remote)
  {
    const RepoStatus status = query_status();
    if(status.remote_configured)
    {
      const CommandResult push = git({"push", "origin", tag_name});
      result.ok = push.ok;
      result.message = push.output.empty() ? (push.ok ? ("Created and pushed tag '" + tag_name + "'.") : "Tag push failed.") : push.output;
      return result;
    }
  }

  result.ok = true;
  result.message = "Created local tag '" + tag_name + "'.";
  return result;
}
} // namespace GitSync
