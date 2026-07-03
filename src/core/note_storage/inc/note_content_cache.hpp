#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace notepp::note_storage
{
/**
 * @brief Simple LRU-ish cache for note text content keyed by path.
 *
 * The cache tracks each entry's last filesystem write time and reloads
 * automatically when the file changes. Callers can also explicitly
 * invalidate or update an entry.
 */
class NoteContentCache
{
public:
  static constexpr std::size_t kDefaultMaxEntries = 512;

  explicit NoteContentCache(std::size_t max_entries = kDefaultMaxEntries);

  /**
   * @brief Get the text for a path, loading from disk if necessary.
   *
   * @param path Absolute filesystem path.
   * @return Cached or freshly loaded text. Empty string if file is unreadable.
   */
  const std::string &get(const std::string &path);

  /**
   * @brief Replace the cached text for a path and refresh its write time.
   */
  void update(const std::string &path, std::string text);

  /**
   * @brief Mark a cached entry as stale so the next get() reloads from disk.
   */
  void invalidate(const std::string &path);

  /**
   * @brief Remove all entries from the cache.
   */
  void clear();

  /**
   * @brief Number of cached entries currently retained.
   */
  std::size_t size() const noexcept;

  /**
   * @brief True if there is a valid cached entry for the given path.
   */
  bool contains(const std::string &path) const noexcept;

  /**
   * @brief Return the cached file write time for the entry, if any.
   *
   * @return The stored file_time_type and a flag indicating whether a valid
   *         entry was found.
   */
  std::pair<bool, std::filesystem::file_time_type> write_time(const std::string &path) const noexcept;

  /**
   * @brief Reset the disk-read counter used for instrumentation.
   */
  void reset_disk_read_counter() noexcept;

  /**
   * @brief Get the number of disk reads triggered by previous get() calls
   *        since the last reset.
   */
  unsigned long long disk_read_count() const noexcept;

private:
  struct Entry
  {
    std::string text;
    std::filesystem::file_time_type last_write_time{};
    unsigned long long last_used = 0;
    bool valid = false;
  };

  std::size_t evict_if_needed(const std::string &protected_path);

  std::size_t max_entries_;
  std::unordered_map<std::string, Entry> entries_;
  unsigned long long clock_ = 0;
  unsigned long long disk_reads_ = 0;
};
} // namespace notepp::note_storage