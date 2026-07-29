#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace atomic_file
{
struct Snapshot
{
  bool existed = false;
  std::string content;

  friend bool operator==(const Snapshot &, const Snapshot &) = default;
};

struct ReadResult
{
  bool success = false;
  Snapshot snapshot;
  std::string message;

  explicit operator bool() const noexcept { return success; }
};

enum class SaveDisposition
{
  saved,
  unchanged,
  stale_preserved,
  io_error
};

struct SaveResult
{
  SaveDisposition disposition = SaveDisposition::io_error;
  Snapshot new_snapshot;
  std::filesystem::path recovery_path;
  std::string message;
  bool durability_confirmed = false;

  explicit operator bool() const noexcept
  {
    return disposition == SaveDisposition::saved || disposition == SaveDisposition::unchanged;
  }
};

// A missing file is a successful read with Snapshot::existed == false.
ReadResult read_text(const std::filesystem::path &path) noexcept;

// Saves through a unique sibling temporary file and atomically publishes it.
// When expected_previous is supplied, the canonical bytes are checked before
// and immediately after preparing the temporary file. If either check is stale,
// the desired bytes are preserved in a uniquely named sibling conflict file and
// the canonical file is left untouched. This is optimistic concurrency, not a
// distributed lock: app-managed Git operations must be serialized with saves,
// and an uncooperative external writer can still race the final check/replace
// interval. Final-component symlinks are rejected rather than replaced.
//
// A successful publication can carry a warning in message and set
// durability_confirmed=false when the platform cannot confirm persistence of
// the containing directory entry. The canonical bytes were still published.
SaveResult save_text(const std::filesystem::path &path, std::string_view desired,
                     const Snapshot *expected_previous = nullptr) noexcept;

struct MoveResult
{
  bool success = false;
  bool destination_exists = false;
  std::string message;

  explicit operator bool() const noexcept { return success; }
};

// Moves a regular file without replacing an existing destination.
MoveResult move_no_replace(const std::filesystem::path &from,
                           const std::filesystem::path &to) noexcept;

// Tracks exact loaded/successfully-saved bytes per file. Failed and stale saves
// deliberately do not advance the expected snapshot, so callers retain dirty
// state and retries cannot silently overwrite the canonical file.
class SnapshotStore
{
public:
  ReadResult load(const std::filesystem::path &path) noexcept;
  ReadResult ensure_loaded(const std::filesystem::path &path) noexcept;
  SaveResult save(const std::filesystem::path &path, std::string_view desired) noexcept;
  void expect_missing(const std::filesystem::path &path);
  void moved(const std::filesystem::path &from, const std::filesystem::path &to);
  void forget(const std::filesystem::path &path);
  void clear() noexcept;

private:
  std::unordered_map<std::string, Snapshot> snapshots_;
  std::unordered_map<std::string, std::string> read_errors_;
};

// Moves a normalized path-keyed value without retaining stale destination
// state. Used by persistence surfaces when a project file is renamed.
void move_path_value(std::unordered_map<std::string, std::string> &values,
                     const std::filesystem::path &from,
                     const std::filesystem::path &to);

// Tracks read failures and stale-save suppression independently of snapshots.
// Plain I/O failures are always retryable. Only a stale save whose desired
// bytes were successfully preserved may suppress an identical retry.
class PersistenceGuard
{
public:
  void record_read(const std::filesystem::path &path, const ReadResult &result);
  bool may_write(const std::filesystem::path &path) const;
  std::string read_error(const std::filesystem::path &path) const;

  bool has_preserved_stale(const std::filesystem::path &path) const;
  bool suppresses(const std::filesystem::path &path, std::string_view content) const;
  void record_save(const std::filesystem::path &path, std::string_view content,
                   const SaveResult &result);

  void moved(const std::filesystem::path &from, const std::filesystem::path &to);
  void forget(const std::filesystem::path &path);
  void clear() noexcept;

private:
  std::unordered_map<std::string, std::string> read_errors_;
  std::unordered_map<std::string, std::string> preserved_stale_content_;
};

// One registry is shared by all project-content writers in the process so a
// successful write through one UI surface updates every other writer's baseline.
SnapshotStore &shared_snapshot_store() noexcept;

struct SaveIssue
{
  std::filesystem::path path;
  SaveDisposition disposition = SaveDisposition::io_error;
  std::filesystem::path recovery_path;
  std::string message;
};

class SaveBatch
{
public:
  void record(const std::filesystem::path &path, const SaveResult &result);
  bool canonical_saves_succeeded() const noexcept { return issues_.empty(); }
  const std::vector<SaveIssue> &issues() const noexcept { return issues_; }
  void clear() noexcept { issues_.clear(); }

private:
  std::vector<SaveIssue> issues_;
};
} // namespace atomic_file
