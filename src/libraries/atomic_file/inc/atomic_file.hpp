#pragma once

#include <filesystem>
#include <string>
#include <string_view>

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
} // namespace atomic_file
