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

  explicit operator bool() const noexcept
  {
    return disposition == SaveDisposition::saved || disposition == SaveDisposition::unchanged;
  }
};

// A missing file is a successful read with Snapshot::existed == false.
ReadResult read_text(const std::filesystem::path &path) noexcept;

// Saves through a unique sibling temporary file and atomically publishes it.
// When expected_previous is supplied and the canonical bytes have changed, the
// desired bytes are preserved in a uniquely named sibling conflict file and the
// canonical file is left untouched. The comparison and replacement are not a
// distributed lock; an uncooperative writer can still race the narrow interval
// between them.
SaveResult save_text(const std::filesystem::path &path, std::string_view desired,
                     const Snapshot *expected_previous = nullptr) noexcept;
} // namespace atomic_file
