#pragma once

#include <string>
#include <vector>

namespace GitSync
{
struct CommandResult
{
  bool ok = false;
  int exit_code = -1;
  std::string output;
};

struct SyncResult
{
  bool ok = false;
  bool conflict = false;
  std::string message;
};

struct RepoStatus
{
  bool git_available = false;
  bool repo_exists = false;
  bool remote_configured = false;
  bool has_commits = false;
  bool clean = true;
  bool remote_branch_exists = false;
  std::string current_branch;
  std::string remote_url;
  std::string head_commit;
  std::string remote_commit;
  std::vector<std::string> local_branches;
};

class Client
{
public:
  explicit Client(std::string repo_path);

  const std::string &repo_path() const { return repo_path_; }

  CommandResult git(std::vector<std::string> args) const;
  bool git_available(std::string *message = nullptr) const;
  bool ensure_repo(std::string *message = nullptr) const;
  RepoStatus query_status(std::string_view branch = {}) const;

  bool set_remote_origin(const std::string &remote_url, std::string *message = nullptr) const;
  bool clear_remote_origin(std::string *message = nullptr) const;
  bool has_remote_branch(const std::string &branch) const;
  SyncResult ensure_initial_commit(const std::string &message) const;
  SyncResult fetch() const;
  SyncResult pull_latest(const std::string &branch) const;
  SyncResult commit_all(const std::string &message) const;
  SyncResult checkout_branch(const std::string &branch, bool create_if_missing) const;
  SyncResult push_branch(const std::string &branch, bool force_with_lease) const;
  SyncResult create_tag(const std::string &tag_name, bool push_to_remote) const;

private:
  std::string repo_path_;
};
} // namespace GitSync
