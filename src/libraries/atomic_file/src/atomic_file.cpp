#include "atomic_file.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace atomic_file
{
namespace
{
constexpr unsigned int kUniqueAttempts = 64;
constexpr std::size_t kMaxSiblingComponent = 240;
std::atomic<std::uint64_t> g_unique_counter{0};

enum class PublishDisposition
{
  published,
  collision,
  error
};

struct IoResult
{
  bool success = false;
  std::string message;
};

struct TempResult
{
  bool success = false;
  std::filesystem::path path;
  std::string message;
};

struct PublishResult
{
  PublishDisposition disposition = PublishDisposition::error;
  std::string message;
};

std::string unique_token()
{
  const auto counter = g_unique_counter.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
  const auto process_id = static_cast<unsigned long long>(GetCurrentProcessId());
#else
  const auto process_id = static_cast<unsigned long long>(getpid());
#endif
  std::ostringstream out;
  out << std::hex << process_id << '-' << counter;
  return out.str();
}

std::filesystem::path sibling_with_suffix(const std::filesystem::path &path,
                                          std::string_view suffix, bool preserve_extension)
{
  using NativeString = std::filesystem::path::string_type;
  const NativeString native_suffix = std::filesystem::path(std::string(suffix)).native();
  const std::filesystem::path filename = path.filename();
  NativeString extension = preserve_extension ? filename.extension().native() : NativeString{};
  NativeString base = preserve_extension ? filename.stem().native() : filename.native();

  if(native_suffix.size() + extension.size() >= kMaxSiblingComponent)
  {
    constexpr std::size_t kMaxPreservedExtension = 16;
    if(extension.size() > kMaxPreservedExtension)
      extension.erase(0, extension.size() - kMaxPreservedExtension);
  }
  const std::size_t fixed_size = native_suffix.size() + extension.size();
  const std::size_t base_limit = fixed_size < kMaxSiblingComponent
                                     ? kMaxSiblingComponent - fixed_size
                                     : 1;
  if(base.size() > base_limit)
    base.resize(base_limit);

  NativeString name = std::move(base);
  name += native_suffix;
  name += extension;
  return path.parent_path() / std::filesystem::path(std::move(name));
}

std::string error_message(std::string_view operation, const std::filesystem::path &path,
                          const std::error_code &error)
{
  return std::string(operation) + " '" + path.generic_string() + "': " + error.message();
}

#ifndef _WIN32
class FileDescriptor
{
public:
  explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;
  FileDescriptor(FileDescriptor &&other) noexcept : value_(other.release()) {}
  FileDescriptor &operator=(FileDescriptor &&other) noexcept
  {
    if(this != &other)
    {
      reset();
      value_ = other.release();
    }
    return *this;
  }
  ~FileDescriptor() { reset(); }

  int get() const noexcept { return value_; }
  int release() noexcept
  {
    const int result = value_;
    value_ = -1;
    return result;
  }
  void reset() noexcept
  {
    if(value_ >= 0)
      (void)::close(value_);
    value_ = -1;
  }

private:
  int value_;
};

std::string posix_error(std::string_view operation, const std::filesystem::path &path,
                        int error_number)
{
  return error_message(operation, path, std::error_code(error_number, std::generic_category()));
}

TempResult write_unique_temp(const std::filesystem::path &destination, std::string_view content)
{
  for(unsigned int attempt = 0; attempt < kUniqueAttempts; ++attempt)
  {
    const auto temp = sibling_with_suffix(destination, ".~npp-t-" + unique_token(), false);
    const int raw_fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if(raw_fd < 0)
    {
      if(errno == EEXIST)
        continue;
      return {false, {}, posix_error("cannot create temporary file", temp, errno)};
    }

    FileDescriptor fd(raw_fd);
    std::size_t written = 0;
    while(written < content.size())
    {
      const ssize_t count = ::write(fd.get(), content.data() + written, content.size() - written);
      if(count < 0)
      {
        if(errno == EINTR)
          continue;
        const int saved_errno = errno;
        fd.reset();
        (void)::unlink(temp.c_str());
        return {false, {}, posix_error("cannot write temporary file", temp, saved_errno)};
      }
      if(count == 0)
      {
        fd.reset();
        (void)::unlink(temp.c_str());
        return {false, {}, "cannot write temporary file '" + temp.generic_string() + "': zero-byte write"};
      }
      written += static_cast<std::size_t>(count);
    }

    struct stat destination_status
    {
    };
    if(::lstat(destination.c_str(), &destination_status) == 0 &&
       !S_ISLNK(destination_status.st_mode) &&
       ::fchmod(fd.get(), destination_status.st_mode & 07777) != 0)
    {
      const int saved_errno = errno;
      fd.reset();
      (void)::unlink(temp.c_str());
      return {false, {}, posix_error("cannot set temporary file permissions", temp, saved_errno)};
    }

    if(::fsync(fd.get()) != 0)
    {
      const int saved_errno = errno;
      fd.reset();
      (void)::unlink(temp.c_str());
      return {false, {}, posix_error("cannot flush temporary file", temp, saved_errno)};
    }
    const int fd_value = fd.release();
    if(::close(fd_value) != 0)
    {
      const int saved_errno = errno;
      (void)::unlink(temp.c_str());
      return {false, {}, posix_error("cannot close temporary file", temp, saved_errno)};
    }
    return {true, temp, {}};
  }
  return {false, {}, "cannot allocate a unique temporary file beside '" + destination.generic_string() + "'"};
}

PublishResult publish_existing(const std::filesystem::path &temp,
                               const std::filesystem::path &destination)
{
  if(::rename(temp.c_str(), destination.c_str()) != 0)
    return {PublishDisposition::error, posix_error("cannot replace destination", destination, errno)};
  return {PublishDisposition::published, {}};
}

PublishResult publish_missing(const std::filesystem::path &temp,
                              const std::filesystem::path &destination)
{
  if(::link(temp.c_str(), destination.c_str()) != 0)
  {
    if(errno == EEXIST)
      return {PublishDisposition::collision, {}};
    return {PublishDisposition::error, posix_error("cannot publish destination", destination, errno)};
  }
  if(::unlink(temp.c_str()) != 0)
    return {PublishDisposition::published,
            posix_error("published destination but cannot remove temporary file", temp, errno)};
  return {PublishDisposition::published, {}};
}

IoResult flush_parent_directory(const std::filesystem::path &destination) noexcept
{
  const std::filesystem::path parent = destination.parent_path().empty()
                                           ? std::filesystem::path(".")
                                           : destination.parent_path();
  FileDescriptor fd(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if(fd.get() < 0)
    return {false, posix_error("published bytes but cannot open parent directory for flush", parent, errno)};
  if(::fsync(fd.get()) != 0)
    return {false, posix_error("published bytes but cannot flush parent directory", parent, errno)};
  return {true, {}};
}

ReadResult platform_read_text(const std::filesystem::path &path)
{
  FileDescriptor fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if(fd.get() < 0)
  {
    if(errno == ENOENT)
      return {true, {false, {}}, {}};
    if(errno == ELOOP)
      return {false, {}, "cannot read '" + path.generic_string() + "': final-component symlink rejected"};
    return {false, {}, posix_error("cannot open file for reading", path, errno)};
  }

  struct stat status
  {
  };
  if(::fstat(fd.get(), &status) != 0)
    return {false, {}, posix_error("cannot inspect open file", path, errno)};
  if(!S_ISREG(status.st_mode))
    return {false, {}, "cannot read '" + path.generic_string() + "': not a regular file"};

  std::string content;
  if(status.st_size > 0)
    content.reserve(static_cast<std::size_t>(status.st_size));
  std::array<char, 64U * 1024U> buffer{};
  for(;;)
  {
    const ssize_t count = ::read(fd.get(), buffer.data(), buffer.size());
    if(count < 0)
    {
      if(errno == EINTR) continue;
      return {false, {}, posix_error("cannot read file", path, errno)};
    }
    if(count == 0) break;
    content.append(buffer.data(), static_cast<std::size_t>(count));
  }
  return {true, {true, std::move(content)}, {}};
}
#else
class WinHandle
{
public:
  explicit WinHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
  WinHandle(const WinHandle &) = delete;
  WinHandle &operator=(const WinHandle &) = delete;
  WinHandle(WinHandle &&other) noexcept : value_(other.release()) {}
  WinHandle &operator=(WinHandle &&other) noexcept
  {
    if(this != &other)
    {
      reset();
      value_ = other.release();
    }
    return *this;
  }
  ~WinHandle() { reset(); }

  HANDLE get() const noexcept { return value_; }
  HANDLE release() noexcept
  {
    const HANDLE result = value_;
    value_ = INVALID_HANDLE_VALUE;
    return result;
  }
  void reset() noexcept
  {
    if(value_ != INVALID_HANDLE_VALUE && value_ != nullptr)
      (void)CloseHandle(value_);
    value_ = INVALID_HANDLE_VALUE;
  }

private:
  HANDLE value_;
};

std::string windows_error(std::string_view operation, const std::filesystem::path &path,
                          DWORD error_number)
{
  return error_message(operation, path,
                       std::error_code(static_cast<int>(error_number), std::system_category()));
}

TempResult write_unique_temp(const std::filesystem::path &destination, std::string_view content)
{
  for(unsigned int attempt = 0; attempt < kUniqueAttempts; ++attempt)
  {
    const auto temp = sibling_with_suffix(destination, ".~npp-t-" + unique_token(), false);
    WinHandle handle(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL, nullptr));
    if(handle.get() == INVALID_HANDLE_VALUE)
    {
      const DWORD error = GetLastError();
      if(error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
        continue;
      return {false, {}, windows_error("cannot create temporary file", temp, error)};
    }

    std::size_t written = 0;
    while(written < content.size())
    {
      const std::size_t remaining = content.size() - written;
      const DWORD chunk = static_cast<DWORD>(
          (std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
      DWORD count = 0;
      if(!WriteFile(handle.get(), content.data() + written, chunk, &count, nullptr))
      {
        const DWORD error = GetLastError();
        handle.reset();
        (void)DeleteFileW(temp.c_str());
        return {false, {}, windows_error("cannot write temporary file", temp, error)};
      }
      if(count == 0)
      {
        handle.reset();
        (void)DeleteFileW(temp.c_str());
        return {false, {}, "cannot write temporary file '" + temp.generic_string() + "': zero-byte write"};
      }
      written += static_cast<std::size_t>(count);
    }

    if(!FlushFileBuffers(handle.get()))
    {
      const DWORD error = GetLastError();
      handle.reset();
      (void)DeleteFileW(temp.c_str());
      return {false, {}, windows_error("cannot flush temporary file", temp, error)};
    }
    handle.reset();
    return {true, temp, {}};
  }
  return {false, {}, "cannot allocate a unique temporary file beside '" + destination.generic_string() + "'"};
}

PublishResult publish_existing(const std::filesystem::path &temp,
                               const std::filesystem::path &destination)
{
  if(!ReplaceFileW(destination.c_str(), temp.c_str(), nullptr,
                   REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
    return {PublishDisposition::error,
            windows_error("cannot replace destination", destination, GetLastError())};
  return {PublishDisposition::published, {}};
}

PublishResult publish_missing(const std::filesystem::path &temp,
                              const std::filesystem::path &destination)
{
  if(MoveFileExW(temp.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
    return {PublishDisposition::published, {}};
  const DWORD error = GetLastError();
  if(error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
    return {PublishDisposition::collision, {}};
  return {PublishDisposition::error, windows_error("cannot publish destination", destination, error)};
}

IoResult flush_parent_directory(const std::filesystem::path &destination) noexcept
{
  const std::filesystem::path parent = destination.parent_path().empty()
                                           ? std::filesystem::path(".")
                                           : destination.parent_path();
  WinHandle handle(CreateFileW(parent.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if(handle.get() == INVALID_HANDLE_VALUE)
    return {false, windows_error("published bytes but cannot open parent directory for flush",
                                 parent, GetLastError())};
  if(!FlushFileBuffers(handle.get()))
    return {false, windows_error("published bytes but cannot flush parent directory",
                                 parent, GetLastError())};
  return {true, {}};
}

ReadResult platform_read_text(const std::filesystem::path &path)
{
  WinHandle handle(CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
  if(handle.get() == INVALID_HANDLE_VALUE)
  {
    const DWORD error = GetLastError();
    if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
      return {true, {false, {}}, {}};
    return {false, {}, windows_error("cannot open file for reading", path, error)};
  }

  FILE_ATTRIBUTE_TAG_INFO attributes{};
  if(!GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo,
                                   &attributes, sizeof(attributes)))
    return {false, {}, windows_error("cannot inspect open file", path, GetLastError())};
  if((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    return {false, {}, "cannot read '" + path.generic_string() + "': final-component symlink rejected"};
  if((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    return {false, {}, "cannot read '" + path.generic_string() + "': not a regular file"};

  LARGE_INTEGER size{};
  if(!GetFileSizeEx(handle.get(), &size))
    return {false, {}, windows_error("cannot determine file size", path, GetLastError())};
  if(size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) >
                              static_cast<unsigned long long>((std::numeric_limits<std::size_t>::max)()))
    return {false, {}, "cannot read '" + path.generic_string() + "': file is too large"};

  std::string content(static_cast<std::size_t>(size.QuadPart), '\0');
  std::size_t read_total = 0;
  while(read_total < content.size())
  {
    const std::size_t remaining = content.size() - read_total;
    const DWORD chunk = static_cast<DWORD>(
        (std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
    DWORD count = 0;
    if(!ReadFile(handle.get(), content.data() + read_total, chunk, &count, nullptr))
      return {false, {}, windows_error("cannot read file", path, GetLastError())};
    if(count == 0)
      return {false, {}, "cannot read '" + path.generic_string() + "': file changed while reading"};
    read_total += static_cast<std::size_t>(count);
  }
  return {true, {true, std::move(content)}, {}};
}
#endif

void remove_temp(const std::filesystem::path &path) noexcept
{
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

IoResult ensure_parent(const std::filesystem::path &path)
{
  const auto parent = path.parent_path();
  if(parent.empty())
    return {true, {}};
  std::error_code error;
  std::filesystem::create_directories(parent, error);
  if(error)
    return {false, error_message("cannot create parent directory", parent, error)};
  return {true, {}};
}

void append_warning(std::string &message, std::string_view warning)
{
  if(warning.empty()) return;
  if(!message.empty()) message += "; ";
  message += warning;
}

SaveResult io_error(std::string message)
{
  SaveResult result;
  result.disposition = SaveDisposition::io_error;
  result.message = std::move(message);
  return result;
}

SaveResult preserve_temp_as_stale(const std::filesystem::path &canonical,
                                  const std::filesystem::path &temp)
{
  for(unsigned int attempt = 0; attempt < kUniqueAttempts; ++attempt)
  {
    const auto recovery = sibling_with_suffix(
        canonical, ".~notepp-conflict-" + unique_token(), true);
    const PublishResult publish = publish_missing(temp, recovery);
    if(publish.disposition == PublishDisposition::published)
    {
      const IoResult durability = flush_parent_directory(recovery);
      SaveResult result;
      result.disposition = SaveDisposition::stale_preserved;
      result.recovery_path = recovery;
      result.durability_confirmed = durability.success;
      result.message = "canonical file changed; local content preserved in '" + recovery.generic_string() + "'";
      append_warning(result.message, publish.message);
      if(!durability.success) append_warning(result.message, durability.message);
      return result;
    }
    if(publish.disposition == PublishDisposition::error)
    {
      remove_temp(temp);
      return io_error(publish.message);
    }
  }
  remove_temp(temp);
  return io_error("cannot allocate a unique conflict file beside '" + canonical.generic_string() + "'");
}

SaveResult preserve_stale(const std::filesystem::path &canonical, std::string_view desired)
{
  const TempResult temp = write_unique_temp(canonical, desired);
  if(!temp.success)
    return io_error(temp.message);
  return preserve_temp_as_stale(canonical, temp.path);
}
} // namespace

ReadResult read_text(const std::filesystem::path &path) noexcept
{
  try
  {
    return platform_read_text(path);
  }
  catch(const std::exception &error)
  {
    return {false, {}, "cannot read '" + path.generic_string() + "': " + error.what()};
  }
  catch(...)
  {
    return {false, {}, "cannot read '" + path.generic_string() + "': unknown error"};
  }
}

SaveResult preserve_recovery(const std::filesystem::path &path,
                             std::string_view desired) noexcept
{
  try
  {
    if(path.empty())
      return io_error("cannot preserve recovery content for an empty path");

    const IoResult parent = ensure_parent(path);
    if(!parent.success)
      return io_error(parent.message);

    SaveResult result = preserve_stale(path, desired);
    if(result.disposition == SaveDisposition::stale_preserved)
      result.message = "canonical file was unavailable; local content preserved in '" +
                       result.recovery_path.generic_string() + "'";
    return result;
  }
  catch(const std::exception &error)
  {
    return io_error("cannot preserve recovery content beside '" +
                    path.generic_string() + "': " + error.what());
  }
  catch(...)
  {
    return io_error("cannot preserve recovery content beside '" +
                    path.generic_string() + "': unknown error");
  }
}

SaveResult save_text(const std::filesystem::path &path, std::string_view desired,
                     const Snapshot *expected_previous) noexcept
{
  try
  {
    if(path.empty())
      return io_error("cannot save an empty path");

    const IoResult parent = ensure_parent(path);
    if(!parent.success)
      return io_error(parent.message);

    const ReadResult current = read_text(path);
    if(!current)
      return io_error(current.message);

    if(expected_previous != nullptr && current.snapshot != *expected_previous)
      return preserve_stale(path, desired);

    if(current.snapshot.existed && current.snapshot.content == desired)
    {
      SaveResult result;
      result.disposition = SaveDisposition::unchanged;
      result.new_snapshot = current.snapshot;
      result.durability_confirmed = true;
      return result;
    }

    const TempResult temp = write_unique_temp(path, desired);
    if(!temp.success)
      return io_error(temp.message);

    // Preparing and flushing the temporary file may take long enough for an
    // external writer to update the canonical path. Recheck immediately before
    // publication and preserve the prepared bytes if the snapshot moved.
    const ReadResult before_publish = read_text(path);
    if(!before_publish)
    {
      remove_temp(temp.path);
      return io_error(before_publish.message);
    }
    if(before_publish.snapshot != current.snapshot)
      return preserve_temp_as_stale(path, temp.path);

    const PublishResult publish = before_publish.snapshot.existed
                                      ? publish_existing(temp.path, path)
                                      : publish_missing(temp.path, path);
    if(publish.disposition != PublishDisposition::published)
    {
      if(publish.disposition == PublishDisposition::collision)
        return preserve_temp_as_stale(path, temp.path);
      remove_temp(temp.path);
      return io_error(publish.message);
    }

    const IoResult durability = flush_parent_directory(path);
    SaveResult result;
    result.disposition = SaveDisposition::saved;
    result.new_snapshot = {true, std::string(desired)};
    result.durability_confirmed = durability.success;
    result.message = publish.message;
    if(!durability.success) append_warning(result.message, durability.message);
    return result;
  }
  catch(const std::exception &error)
  {
    return io_error("cannot save '" + path.generic_string() + "': " + error.what());
  }
  catch(...)
  {
    return io_error("cannot save '" + path.generic_string() + "': unknown error");
  }
}

MoveResult move_no_replace(const std::filesystem::path &from,
                           const std::filesystem::path &to) noexcept
{
  try
  {
    std::error_code status_error;
    const auto source_status = std::filesystem::symlink_status(from, status_error);
    if(status_error)
      return {false, false, error_message("cannot inspect source", from, status_error)};
    if(!std::filesystem::is_regular_file(source_status))
      return {false, false, "source is not a regular file: '" + from.generic_string() + "'"};

#ifdef _WIN32
    if(MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0)
      return {true, false, {}};
    const DWORD error = GetLastError();
    const bool collision = error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS;
    return {false, collision,
            error_message("cannot move file", to,
                          std::error_code(static_cast<int>(error), std::system_category()))};
#else
    if(::link(from.c_str(), to.c_str()) != 0)
    {
      const int error = errno;
      return {false, error == EEXIST, posix_error("cannot move file", to, error)};
    }
    if(::unlink(from.c_str()) == 0)
      return {true, false, {}};

    const int unlink_error = errno;
    if(::unlink(to.c_str()) != 0)
    {
      return {false, false,
              posix_error("source unlink failed and destination rollback failed", from,
                          unlink_error)};
    }
    return {false, false, posix_error("cannot unlink moved source", from, unlink_error)};
#endif
  }
  catch(const std::exception &error)
  {
    return {false, false, "cannot move '" + from.generic_string() + "': " + error.what()};
  }
  catch(...)
  {
    return {false, false, "cannot move '" + from.generic_string() + "': unknown error"};
  }
}

ReadResult SnapshotStore::load(const std::filesystem::path &path) noexcept
{
  const std::string key = path.lexically_normal().generic_string();
  ReadResult result = read_text(path);
  if(result)
  {
    snapshots_[key] = result.snapshot;
    read_errors_.erase(key);
  }
  else
  {
    read_errors_[key] = result.message;
  }
  return result;
}

ReadResult SnapshotStore::ensure_loaded(const std::filesystem::path &path) noexcept
{
  const std::string key = path.lexically_normal().generic_string();
  if(read_errors_.contains(key)) return load(path);
  if(const auto found = snapshots_.find(key); found != snapshots_.end())
    return {true, found->second, {}};
  return load(path);
}

SaveResult SnapshotStore::save(const std::filesystem::path &path,
                               std::string_view desired) noexcept
{
  const std::string key = path.lexically_normal().generic_string();
  if(const auto read_error = read_errors_.find(key); read_error != read_errors_.end())
  {
    SaveResult failed;
    failed.disposition = SaveDisposition::io_error;
    failed.message = "canonical file must be reloaded after read failure: " +
                     read_error->second;
    return failed;
  }

  auto found = snapshots_.find(key);
  if(found == snapshots_.end())
  {
    ReadResult loaded = load(path);
    if(!loaded)
    {
      SaveResult failed;
      failed.disposition = SaveDisposition::io_error;
      failed.message = std::move(loaded.message);
      return failed;
    }
    found = snapshots_.find(key);
  }

  SaveResult result = save_text(path, desired, &found->second);
  if(result)
    found->second = result.new_snapshot;
  return result;
}

void SnapshotStore::expect_missing(const std::filesystem::path &path)
{
  const std::string key = path.lexically_normal().generic_string();
  snapshots_[key] = Snapshot{};
  read_errors_.erase(key);
}

void SnapshotStore::moved(const std::filesystem::path &from,
                          const std::filesystem::path &to)
{
  const std::string from_key = from.lexically_normal().generic_string();
  const std::string to_key = to.lexically_normal().generic_string();
  snapshots_.erase(to_key);
  read_errors_.erase(to_key);
  if(auto node = snapshots_.extract(from_key); !node.empty())
  {
    node.key() = to_key;
    snapshots_.insert(std::move(node));
  }
  if(auto error = read_errors_.extract(from_key); !error.empty())
  {
    error.key() = to_key;
    read_errors_.insert(std::move(error));
  }
}

void SnapshotStore::forget(const std::filesystem::path &path)
{
  const std::string key = path.lexically_normal().generic_string();
  snapshots_.erase(key);
  read_errors_.erase(key);
}

void SnapshotStore::clear() noexcept
{
  snapshots_.clear();
  read_errors_.clear();
}

namespace
{
std::string persistence_key(const std::filesystem::path &path)
{
  return path.lexically_normal().generic_string();
}

} // namespace

void move_path_value(std::unordered_map<std::string, std::string> &values,
                     const std::filesystem::path &from,
                     const std::filesystem::path &to)
{
  const std::string from_key = persistence_key(from);
  const std::string to_key = persistence_key(to);
  values.erase(to_key);
  if(auto node = values.extract(from_key); !node.empty())
  {
    node.key() = to_key;
    values.insert(std::move(node));
  }
}

void PersistenceGuard::record_read(const std::filesystem::path &path,
                                   const ReadResult &result)
{
  const std::string key = persistence_key(path);
  if(result)
  {
    read_errors_.erase(key);
  }
  else
  {
    read_errors_[key] = result.message;
  }
}

void PersistenceGuard::record_reload(const std::filesystem::path &path,
                                     const ReadResult &result)
{
  record_read(path, result);
  if(result)
    preserved_stale_content_.erase(persistence_key(path));
}

bool PersistenceGuard::may_write(const std::filesystem::path &path) const
{
  return !read_errors_.contains(persistence_key(path));
}

std::string PersistenceGuard::read_error(const std::filesystem::path &path) const
{
  if(const auto found = read_errors_.find(persistence_key(path)); found != read_errors_.end())
    return found->second;
  return {};
}

bool PersistenceGuard::has_preserved_stale(const std::filesystem::path &path) const
{
  return preserved_stale_content_.contains(persistence_key(path));
}

bool PersistenceGuard::suppresses(const std::filesystem::path &path,
                                  std::string_view content) const
{
  if(const auto found = preserved_stale_content_.find(persistence_key(path));
     found != preserved_stale_content_.end())
    return found->second == content;
  return false;
}

void PersistenceGuard::record_save(const std::filesystem::path &path,
                                   std::string_view content,
                                   const SaveResult &result)
{
  const std::string key = persistence_key(path);
  if(result)
  {
    read_errors_.erase(key);
    preserved_stale_content_.erase(key);
    return;
  }

  if(result.disposition == SaveDisposition::stale_preserved &&
     !result.recovery_path.empty())
    preserved_stale_content_[key] = std::string(content);
}

void PersistenceGuard::moved(const std::filesystem::path &from,
                             const std::filesystem::path &to)
{
  move_path_value(read_errors_, from, to);
  move_path_value(preserved_stale_content_, from, to);
}

void PersistenceGuard::forget(const std::filesystem::path &path)
{
  const std::string key = persistence_key(path);
  read_errors_.erase(key);
  preserved_stale_content_.erase(key);
}

void PersistenceGuard::clear() noexcept
{
  read_errors_.clear();
  preserved_stale_content_.clear();
}

SnapshotStore &shared_snapshot_store() noexcept
{
  static SnapshotStore store;
  return store;
}

void SaveBatch::record(const std::filesystem::path &path,
                       const SaveResult &result)
{
  if(result) return;
  issues_.push_back({path, result.disposition, result.recovery_path, result.message});
}
} // namespace atomic_file
