#include "note_content_cache.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace notepp::note_storage
{
NoteContentCache::NoteContentCache(std::size_t max_entries) : max_entries_(max_entries == 0 ? 1 : max_entries) {}

void NoteContentCache::clear()
{
  entries_.clear();
  clock_ = 0;
}

std::size_t NoteContentCache::size() const noexcept
{
  return entries_.size();
}

bool NoteContentCache::contains(const std::string &path) const noexcept
{
  auto it = entries_.find(path);
  return it != entries_.end() && it->second.valid;
}

std::pair<bool, std::filesystem::file_time_type> NoteContentCache::write_time(const std::string &path) const noexcept
{
  auto it = entries_.find(path);
  if(it == entries_.end() || !it->second.valid) return {false, {}};
  return {true, it->second.last_write_time};
}

void NoteContentCache::reset_disk_read_counter() noexcept
{
  disk_reads_ = 0;
}

unsigned long long NoteContentCache::disk_read_count() const noexcept
{
  return disk_reads_;
}

std::size_t NoteContentCache::evict_if_needed(const std::string &protected_path)
{
  if(entries_.size() <= max_entries_) return 0;

  auto victim = entries_.end();
  for(auto it = entries_.begin(); it != entries_.end(); ++it)
  {
    if(it->first == protected_path) continue;
    if(victim == entries_.end() || it->second.last_used < victim->second.last_used)
      victim = it;
  }
  if(victim == entries_.end()) return 0;
  entries_.erase(victim);
  return 1;
}

const std::string &NoteContentCache::get(const std::string &path)
{
  auto &entry = entries_[path];
  entry.last_used = ++clock_;

  std::error_code ec;
  const auto write_time = std::filesystem::last_write_time(path, ec);
  if(!entry.valid || (!ec && entry.last_write_time != write_time))
  {
    ++disk_reads_;
    std::ifstream in(path, std::ios::binary);
    if(in)
    {
      entry.text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      entry.last_write_time = ec ? std::filesystem::file_time_type{} : write_time;
    }
    else
    {
      entry.text.clear();
      entry.last_write_time = {};
    }
    entry.valid = true;
  }

  while(entries_.size() > max_entries_)
  {
    if(evict_if_needed(path) == 0) break;
  }

  return entries_.at(path).text;
}

void NoteContentCache::update(const std::string &path, std::string text)
{
  if(path.empty()) return;
  auto &entry = entries_[path];
  entry.text = std::move(text);
  entry.last_used = ++clock_;
  std::error_code ec;
  entry.last_write_time = std::filesystem::last_write_time(path, ec);
  if(ec) entry.last_write_time = {};
  entry.valid = true;
  while(entries_.size() > max_entries_)
  {
    if(evict_if_needed(path) == 0) break;
  }
}

void NoteContentCache::invalidate(const std::string &path)
{
  auto it = entries_.find(path);
  if(it != entries_.end()) it->second.valid = false;
}
} // namespace notepp::note_storage