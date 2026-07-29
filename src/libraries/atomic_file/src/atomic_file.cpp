#include "atomic_file.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
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
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto counter = g_unique_counter.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
  const auto process_id = static_cast<unsigned long long>(GetCurrentProcessId());
#else
  const auto process_id = static_cast<unsigned long long>(getpid());
#endif
  std::ostringstream out;
  out << process_id << '-' << static_cast<unsigned long long>(now) << '-' << counter;
  return out.str();
}

std::filesystem::path sibling_with_suffix(const std::filesystem::path &path,
                                          std::string_view suffix, bool preserve_extension)
{
  using NativeString = std::filesystem::path::string_type;
  const std::filesystem::path suffix_path{std::string(suffix)};
  const NativeString native_suffix = suffix_path.native();
  const std::filesystem::path filename = path.filename();
  NativeString name;
  if(preserve_extension)
  {
    name = filename.stem().native();
    name += native_suffix;
    name += filename.extension().native();
  }
  else
  {
    name = filename.native();
    name += native_suffix;
  }
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
    const auto temp = sibling_with_suffix(destination, ".notepp-tmp-" + unique_token(), false);
    const int raw_fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if(raw_fd < 0)
    {
      if(errno == EEXIST)
        continue;
      return {false, {}, posix_error("cannot create temporary file", temp, errno)};
    }

    FileDescriptor fd(raw_fd);
    struct stat destination_status
    {
    };
    if(::stat(destination.c_str(), &destination_status) == 0)
    {
      if(::fchmod(fd.get(), destination_status.st_mode & 07777) != 0)
      {
        const int saved_errno = errno;
        fd.reset();
        (void)::unlink(temp.c_str());
        return {false, {}, posix_error("cannot set temporary file permissions", temp, saved_errno)};
      }
    }

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
    return {PublishDisposition::error, posix_error("cannot remove published temporary file", temp, errno)};
  return {PublishDisposition::published, {}};
}

void flush_parent_directory(const std::filesystem::path &destination) noexcept
{
  const std::filesystem::path parent = destination.parent_path().empty()
                                           ? std::filesystem::path(".")
                                           : destination.parent_path();
  FileDescriptor fd(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if(fd.get() >= 0)
    (void)::fsync(fd.get());
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
    const auto temp = sibling_with_suffix(destination, ".notepp-tmp-" + unique_token(), false);
    WinHandle handle(CreateFileW(temp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
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
  if(!ReplaceFileW(destination.c_str(), temp.c_str(), nullptr, 0, nullptr, nullptr))
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

void flush_parent_directory(const std::filesystem::path &) noexcept {}
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

SaveResult io_error(std::string message)
{
  SaveResult result;
  result.disposition = SaveDisposition::io_error;
  result.message = std::move(message);
  return result;
}

SaveResult preserve_stale(const std::filesystem::path &canonical, std::string_view desired)
{
  for(unsigned int attempt = 0; attempt < kUniqueAttempts; ++attempt)
  {
    const auto recovery = sibling_with_suffix(
        canonical, ".notepp-local-conflict-" + unique_token(), true);
    const TempResult temp = write_unique_temp(recovery, desired);
    if(!temp.success)
      return io_error(temp.message);

    const PublishResult publish = publish_missing(temp.path, recovery);
    if(publish.disposition == PublishDisposition::published)
    {
      flush_parent_directory(recovery);
      SaveResult result;
      result.disposition = SaveDisposition::stale_preserved;
      result.recovery_path = recovery;
      result.message = "canonical file changed; local content preserved in '" + recovery.generic_string() + "'";
      return result;
    }
    remove_temp(temp.path);
    if(publish.disposition == PublishDisposition::error)
      return io_error(publish.message);
  }
  return io_error("cannot allocate a unique conflict file beside '" + canonical.generic_string() + "'");
}
} // namespace

ReadResult read_text(const std::filesystem::path &path) noexcept
{
  try
  {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if(error)
    {
      if(error == std::errc::no_such_file_or_directory)
        return {true, {false, {}}, {}};
      return {false, {}, error_message("cannot inspect file", path, error)};
    }
    if(!std::filesystem::exists(status))
      return {true, {false, {}}, {}};
    if(!std::filesystem::is_regular_file(status))
      return {false, {}, "cannot read '" + path.generic_string() + "': not a regular file"};

    std::ifstream input(path, std::ios::binary);
    if(!input)
      return {false, {}, "cannot open file for reading '" + path.generic_string() + "'"};
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if(input.bad())
      return {false, {}, "cannot read file '" + path.generic_string() + "'"};
    return {true, {true, std::move(content)}, {}};
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
      return result;
    }

    const TempResult temp = write_unique_temp(path, desired);
    if(!temp.success)
      return io_error(temp.message);

    const PublishResult publish = current.snapshot.existed
                                      ? publish_existing(temp.path, path)
                                      : publish_missing(temp.path, path);
    if(publish.disposition != PublishDisposition::published)
    {
      remove_temp(temp.path);
      if(publish.disposition == PublishDisposition::collision)
        return preserve_stale(path, desired);
      return io_error(publish.message);
    }

    flush_parent_directory(path);
    SaveResult result;
    result.disposition = SaveDisposition::saved;
    result.new_snapshot = {true, std::string(desired)};
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
} // namespace atomic_file
