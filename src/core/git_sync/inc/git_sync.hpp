#pragma once

#include "process.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace notepp::git_sync
{
enum class SyncState
{
  unavailable,
  not_repository,
  no_upstream,
  clean,
  dirty,
  ahead,
  behind,
  diverged,
  syncing,
  offline,
  conflict,
  error
};

struct Status
{
  SyncState state = SyncState::error;
  std::string branch;
  int ahead = 0;
  int behind = 0;
  bool has_uncommitted_changes = false;
  std::string summary;
  std::string detail;
  std::chrono::system_clock::time_point checked_at{};
};

struct OperationResult
{
  bool success = false;
  bool changed_worktree = false;
  Status status;
};

struct HeadContentResult
{
  bool success = false;
  bool missing = false;
  std::string content;
  std::string commit_id;
  std::string subject;
  std::string detail;
};

class Client
{
public:
  explicit Client(const process::Runner &runner);

  [[nodiscard]] Status inspect(const std::filesystem::path &project_root) const;
  [[nodiscard]] HeadContentResult read_head(const std::filesystem::path &project_root,
                                            const std::filesystem::path &note_path) const;
  [[nodiscard]] OperationResult pull_on_open(const std::filesystem::path &project_root) const;
  [[nodiscard]] OperationResult commit_and_push(const std::filesystem::path &project_root,
                                                std::string_view message) const;
  [[nodiscard]] OperationResult manual_sync(const std::filesystem::path &project_root,
                                            std::string_view message) const;

private:
  const process::Runner &runner_;
};

[[nodiscard]] OperationResult exception_result(std::string_view action,
                                               std::string_view detail = {});
[[nodiscard]] std::string_view state_name(SyncState state) noexcept;
[[nodiscard]] SyncState state_from_name(std::string_view name) noexcept;
} // namespace notepp::git_sync
