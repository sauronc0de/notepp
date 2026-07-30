#include "process.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

namespace process
{
namespace
{
void append_bounded(std::string &destination, const char *data, std::size_t size,
                    std::size_t maximum, bool &truncated)
{
  const std::size_t available = destination.size() < maximum ? maximum - destination.size() : 0U;
  const std::size_t count = std::min(size, available);
  destination.append(data, count);
  if(count != size) truncated = true;
}

#ifdef _WIN32
std::wstring widen(std::string_view value)
{
  if(value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if(size <= 0) return {};
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                         static_cast<int>(value.size()), result.data(), size) != size)
    return {};
  return result;
}

std::string windows_error(DWORD code)
{
  LPWSTR message = nullptr;
  const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                        FORMAT_MESSAGE_IGNORE_INSERTS,
                                    nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
  std::string result = "Windows error " + std::to_string(code);
  if(size != 0 && message != nullptr)
  {
    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, message, static_cast<int>(size),
                                              nullptr, 0, nullptr, nullptr);
    if(utf8_size > 0)
    {
      result.resize(static_cast<std::size_t>(utf8_size));
      WideCharToMultiByte(CP_UTF8, 0, message, static_cast<int>(size), result.data(),
                          utf8_size, nullptr, nullptr);
      while(!result.empty() && (result.back() == '\r' || result.back() == '\n')) result.pop_back();
    }
  }
  if(message != nullptr) LocalFree(message);
  return result;
}

class Handle
{
public:
  Handle() = default;
  explicit Handle(HANDLE value) : value_(value) {}
  ~Handle() { reset(); }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  Handle(Handle &&other) noexcept : value_(other.release()) {}
  Handle &operator=(Handle &&other) noexcept
  {
    if(this != &other) reset(other.release());
    return *this;
  }
  [[nodiscard]] HANDLE get() const noexcept { return value_; }
  [[nodiscard]] HANDLE release() noexcept
  {
    HANDLE value = value_;
    value_ = nullptr;
    return value;
  }
  void reset(HANDLE value = nullptr) noexcept
  {
    if(value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    value_ = value;
  }

private:
  HANDLE value_ = nullptr;
};

std::wstring quote_windows_argument(std::wstring_view argument)
{
  if(argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos && !argument.empty())
    return std::wstring(argument);

  std::wstring output = L"\"";
  std::size_t backslashes = 0;
  for(const wchar_t character : argument)
  {
    if(character == L'\\')
    {
      ++backslashes;
      continue;
    }
    if(character == L'\"')
    {
      output.append(backslashes * 2U + 1U, L'\\');
      output.push_back(L'\"');
      backslashes = 0;
      continue;
    }
    output.append(backslashes, L'\\');
    backslashes = 0;
    output.push_back(character);
  }
  output.append(backslashes * 2U, L'\\');
  output.push_back(L'\"');
  return output;
}

std::vector<wchar_t> make_environment(const RunOptions &options)
{
  std::map<std::wstring, std::wstring, std::less<>> values;
  LPWCH environment = GetEnvironmentStringsW();
  if(environment != nullptr)
  {
    for(const wchar_t *entry = environment; *entry != L'\0'; entry += std::wcslen(entry) + 1U)
    {
      const std::wstring_view item(entry);
      const std::size_t equals = item.find(L'=', item.starts_with(L'=') ? 1U : 0U);
      if(equals != std::wstring_view::npos)
        values.emplace(std::wstring(item.substr(0, equals)), std::wstring(item.substr(equals + 1U)));
    }
    FreeEnvironmentStringsW(environment);
  }
  for(const auto &[key, value] : options.environment_overrides) values[widen(key)] = widen(value);

  std::vector<wchar_t> block;
  for(const auto &[key, value] : values)
  {
    block.insert(block.end(), key.begin(), key.end());
    block.push_back(L'=');
    block.insert(block.end(), value.begin(), value.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

void drain_handle(HANDLE handle, std::string &output, std::size_t maximum, bool &truncated)
{
  std::array<char, 4096> buffer{};
  for(;;)
  {
    DWORD read = 0;
    if(!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0)
      break;
    append_bounded(output, buffer.data(), static_cast<std::size_t>(read), maximum, truncated);
  }
}
#else
class FileDescriptor
{
public:
  FileDescriptor() = default;
  explicit FileDescriptor(int value) : value_(value) {}
  ~FileDescriptor() { reset(); }
  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;
  FileDescriptor(FileDescriptor &&other) noexcept : value_(other.release()) {}
  FileDescriptor &operator=(FileDescriptor &&other) noexcept
  {
    if(this != &other) reset(other.release());
    return *this;
  }
  [[nodiscard]] int get() const noexcept { return value_; }
  [[nodiscard]] int release() noexcept
  {
    const int value = value_;
    value_ = -1;
    return value;
  }
  void reset(int value = -1) noexcept
  {
    if(value_ >= 0) close(value_);
    value_ = value;
  }

private:
  int value_ = -1;
};

std::vector<std::string> make_environment(const RunOptions &options)
{
  std::map<std::string, std::string, std::less<>> values;
  for(char **entry = environ; entry != nullptr && *entry != nullptr; ++entry)
  {
    const std::string_view item(*entry);
    const std::size_t equals = item.find('=');
    if(equals != std::string_view::npos)
      values.emplace(std::string(item.substr(0, equals)), std::string(item.substr(equals + 1U)));
  }
  for(const auto &[key, value] : options.environment_overrides) values[key] = value;

  std::vector<std::string> result;
  result.reserve(values.size());
  for(const auto &[key, value] : values) result.push_back(key + "=" + value);
  return result;
}

void drain_descriptor(int descriptor, std::string &output, std::size_t maximum, bool &truncated)
{
  std::array<char, 4096> buffer{};
  for(;;)
  {
    const ssize_t count = read(descriptor, buffer.data(), buffer.size());
    if(count > 0)
    {
      append_bounded(output, buffer.data(), static_cast<std::size_t>(count), maximum, truncated);
      continue;
    }
    if(count < 0 && errno == EINTR) continue;
    break;
  }
}
#endif
} // namespace

Result SystemRunner::run(const std::filesystem::path &executable,
                         std::span<const std::string> arguments,
                         const RunOptions &options) const
{
  Result result;
  if(executable.empty())
  {
    result.error = "Executable path is empty";
    return result;
  }

#ifdef _WIN32
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE stdout_read_raw = nullptr;
  HANDLE stdout_write_raw = nullptr;
  HANDLE stderr_read_raw = nullptr;
  HANDLE stderr_write_raw = nullptr;
  if(!CreatePipe(&stdout_read_raw, &stdout_write_raw, &security, 0) ||
     !CreatePipe(&stderr_read_raw, &stderr_write_raw, &security, 0))
  {
    if(stdout_read_raw != nullptr) CloseHandle(stdout_read_raw);
    if(stdout_write_raw != nullptr) CloseHandle(stdout_write_raw);
    if(stderr_read_raw != nullptr) CloseHandle(stderr_read_raw);
    if(stderr_write_raw != nullptr) CloseHandle(stderr_write_raw);
    result.error = windows_error(GetLastError());
    return result;
  }
  Handle stdout_read(stdout_read_raw);
  Handle stdout_write(stdout_write_raw);
  Handle stderr_read(stderr_read_raw);
  Handle stderr_write(stderr_write_raw);
  if(!SetHandleInformation(stdout_read.get(), HANDLE_FLAG_INHERIT, 0) ||
     !SetHandleInformation(stderr_read.get(), HANDLE_FLAG_INHERIT, 0))
  {
    result.error = windows_error(GetLastError());
    return result;
  }

  SIZE_T attribute_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
  std::vector<std::byte> attribute_storage(attribute_size);
  auto *attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
  if(!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size))
  {
    result.error = windows_error(GetLastError());
    return result;
  }
  struct AttributeCleanup
  {
    PPROC_THREAD_ATTRIBUTE_LIST value;
    ~AttributeCleanup() { DeleteProcThreadAttributeList(value); }
  } attribute_cleanup{attributes};
  std::array<HANDLE, 2> inherited{stdout_write.get(), stderr_write.get()};
  if(!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                inherited.data(), sizeof(inherited), nullptr, nullptr))
  {
    result.error = windows_error(GetLastError());
    return result;
  }

  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = nullptr;
  startup.StartupInfo.hStdOutput = stdout_write.get();
  startup.StartupInfo.hStdError = stderr_write.get();
  startup.lpAttributeList = attributes;

  std::wstring command = quote_windows_argument(executable.wstring());
  for(const std::string &argument : arguments)
  {
    command.push_back(L' ');
    command += quote_windows_argument(widen(argument));
  }
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  std::vector<wchar_t> environment = make_environment(options);
  const std::wstring working_directory = options.working_directory.empty()
                                             ? std::wstring{}
                                             : options.working_directory.wstring();

  PROCESS_INFORMATION process_info{};
  const DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
  if(!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, flags,
                     environment.data(), working_directory.empty() ? nullptr : working_directory.c_str(),
                     &startup.StartupInfo, &process_info))
  {
    result.error = windows_error(GetLastError());
    return result;
  }
  Handle process_handle(process_info.hProcess);
  Handle thread_handle(process_info.hThread);
  stdout_write.reset();
  stderr_write.reset();

  Handle job(CreateJobObjectW(nullptr, nullptr));
  if(job.get() == nullptr)
  {
    TerminateProcess(process_handle.get(), 1);
    result.error = windows_error(GetLastError());
    return result;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
     !AssignProcessToJobObject(job.get(), process_handle.get()))
  {
    TerminateProcess(process_handle.get(), 1);
    result.error = windows_error(GetLastError());
    return result;
  }

  bool stdout_truncated = false;
  bool stderr_truncated = false;
  std::thread stdout_thread(drain_handle, stdout_read.get(), std::ref(result.stdout_text),
                            options.max_output_bytes, std::ref(stdout_truncated));
  std::thread stderr_thread(drain_handle, stderr_read.get(), std::ref(result.stderr_text),
                            options.max_output_bytes, std::ref(stderr_truncated));

  const auto deadline = std::chrono::steady_clock::now() + options.timeout;
  bool terminated = false;
  for(;;)
  {
    const DWORD wait = WaitForSingleObject(process_handle.get(), 25);
    if(wait == WAIT_OBJECT_0) break;
    if(options.stop_token.stop_requested())
    {
      result.termination = Termination::cancelled;
      terminated = true;
      break;
    }
    if(std::chrono::steady_clock::now() >= deadline)
    {
      result.termination = Termination::timed_out;
      terminated = true;
      break;
    }
    if(wait == WAIT_FAILED)
    {
      result.error = windows_error(GetLastError());
      terminated = true;
      break;
    }
  }
  if(terminated)
  {
    TerminateJobObject(job.get(), 1);
    WaitForSingleObject(process_handle.get(), 5000);
  }
  else
  {
    DWORD exit_code = 0;
    if(GetExitCodeProcess(process_handle.get(), &exit_code))
    {
      result.termination = Termination::exited;
      result.exit_code = static_cast<std::int64_t>(exit_code);
    }
    else
      result.error = windows_error(GetLastError());
  }

  stdout_thread.join();
  stderr_thread.join();
  result.output_truncated = stdout_truncated || stderr_truncated;
  return result;
#else
  int stdout_pipe[2]{};
  int stderr_pipe[2]{};
  if(pipe2(stdout_pipe, O_CLOEXEC) != 0 || pipe2(stderr_pipe, O_CLOEXEC) != 0)
  {
    if(stdout_pipe[0] != 0) close(stdout_pipe[0]);
    if(stdout_pipe[1] != 0) close(stdout_pipe[1]);
    result.error = std::strerror(errno);
    return result;
  }
  FileDescriptor stdout_read(stdout_pipe[0]);
  FileDescriptor stdout_write(stdout_pipe[1]);
  FileDescriptor stderr_read(stderr_pipe[0]);
  FileDescriptor stderr_write(stderr_pipe[1]);

  posix_spawn_file_actions_t actions;
  const int actions_init_result = posix_spawn_file_actions_init(&actions);
  if(actions_init_result != 0)
  {
    result.error = std::strerror(actions_init_result);
    return result;
  }
  struct ActionsCleanup
  {
    posix_spawn_file_actions_t *value;
    ~ActionsCleanup() { posix_spawn_file_actions_destroy(value); }
  } actions_cleanup{&actions};
  int actions_result = posix_spawn_file_actions_adddup2(&actions, stdout_write.get(), STDOUT_FILENO);
  if(actions_result == 0)
    actions_result = posix_spawn_file_actions_adddup2(&actions, stderr_write.get(), STDERR_FILENO);
  if(actions_result == 0)
    actions_result = posix_spawn_file_actions_addclose(&actions, stdout_read.get());
  if(actions_result == 0)
    actions_result = posix_spawn_file_actions_addclose(&actions, stderr_read.get());
  if(actions_result == 0)
    actions_result = posix_spawn_file_actions_addclose(&actions, stdout_write.get());
  if(actions_result == 0)
    actions_result = posix_spawn_file_actions_addclose(&actions, stderr_write.get());
  if(actions_result != 0)
  {
    result.error = std::strerror(actions_result);
    return result;
  }
  if(!options.working_directory.empty())
  {
#if defined(__GLIBC__) || defined(__APPLE__)
    const int add_chdir_result = posix_spawn_file_actions_addchdir_np(&actions, options.working_directory.c_str());
    if(add_chdir_result != 0)
    {
      result.error = std::strerror(add_chdir_result);
      return result;
    }
#else
    result.error = "Working directories are not supported by this POSIX spawn implementation";
    return result;
#endif
  }

  posix_spawnattr_t attributes;
  const int attributes_init_result = posix_spawnattr_init(&attributes);
  if(attributes_init_result != 0)
  {
    result.error = std::strerror(attributes_init_result);
    return result;
  }
  struct AttributesCleanup
  {
    posix_spawnattr_t *value;
    ~AttributesCleanup() { posix_spawnattr_destroy(value); }
  } attributes_cleanup{&attributes};
  int attributes_result = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  if(attributes_result == 0) attributes_result = posix_spawnattr_setpgroup(&attributes, 0);
  if(attributes_result != 0)
  {
    result.error = std::strerror(attributes_result);
    return result;
  }

  std::vector<std::string> argument_storage;
  argument_storage.reserve(arguments.size() + 1U);
  argument_storage.push_back(executable.string());
  argument_storage.insert(argument_storage.end(), arguments.begin(), arguments.end());
  std::vector<char *> argv;
  argv.reserve(argument_storage.size() + 1U);
  for(std::string &argument : argument_storage) argv.push_back(argument.data());
  argv.push_back(nullptr);

  std::vector<std::string> environment_storage = make_environment(options);
  std::vector<char *> environment;
  environment.reserve(environment_storage.size() + 1U);
  for(std::string &entry : environment_storage) environment.push_back(entry.data());
  environment.push_back(nullptr);

  pid_t pid = -1;
  const int spawn_result = posix_spawnp(&pid, executable.c_str(), &actions, &attributes,
                                        argv.data(), environment.data());
  if(spawn_result != 0)
  {
    result.error = std::strerror(spawn_result);
    return result;
  }
  stdout_write.reset();
  stderr_write.reset();

  const int stdout_flags = fcntl(stdout_read.get(), F_GETFL, 0);
  const int stderr_flags = fcntl(stderr_read.get(), F_GETFL, 0);
  fcntl(stdout_read.get(), F_SETFL, stdout_flags | O_NONBLOCK);
  fcntl(stderr_read.get(), F_SETFL, stderr_flags | O_NONBLOCK);

  const auto deadline = std::chrono::steady_clock::now() + options.timeout;
  int wait_status = 0;
  bool child_exited = false;
  bool sent_termination = false;
  for(;;)
  {
    std::array<pollfd, 2> descriptors{{{stdout_read.get(), POLLIN, 0}, {stderr_read.get(), POLLIN, 0}}};
    (void)poll(descriptors.data(), descriptors.size(), 25);
    drain_descriptor(stdout_read.get(), result.stdout_text, options.max_output_bytes, result.output_truncated);
    drain_descriptor(stderr_read.get(), result.stderr_text, options.max_output_bytes, result.output_truncated);

    const pid_t waited = waitpid(pid, &wait_status, WNOHANG);
    if(waited == pid)
    {
      child_exited = true;
      break;
    }
    if(waited < 0 && errno != EINTR)
    {
      result.error = std::strerror(errno);
      break;
    }
    if(options.stop_token.stop_requested())
    {
      result.termination = Termination::cancelled;
      sent_termination = true;
      break;
    }
    if(std::chrono::steady_clock::now() >= deadline)
    {
      result.termination = Termination::timed_out;
      sent_termination = true;
      break;
    }
  }

  if(sent_termination)
  {
    (void)kill(-pid, SIGTERM);
    const auto grace_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while(std::chrono::steady_clock::now() < grace_deadline)
    {
      if(waitpid(pid, &wait_status, WNOHANG) == pid)
      {
        child_exited = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(!child_exited)
    {
      (void)kill(-pid, SIGKILL);
      while(waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {}
    }
  }

  // Descendants may inherit the pipes even after the direct child exits. Keep
  // draining briefly without allowing such a descendant to defeat the caller's
  // timeout by holding a pipe open indefinitely.
  const auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  while(std::chrono::steady_clock::now() < drain_deadline)
  {
    std::array<pollfd, 2> descriptors{{{stdout_read.get(), POLLIN | POLLHUP, 0},
                                       {stderr_read.get(), POLLIN | POLLHUP, 0}}};
    const int ready = poll(descriptors.data(), descriptors.size(), 10);
    drain_descriptor(stdout_read.get(), result.stdout_text, options.max_output_bytes, result.output_truncated);
    drain_descriptor(stderr_read.get(), result.stderr_text, options.max_output_bytes, result.output_truncated);
    if(ready > 0 && (descriptors[0].revents & POLLHUP) != 0 &&
       (descriptors[1].revents & POLLHUP) != 0)
      break;
  }

  if(child_exited && !sent_termination && result.error.empty())
  {
    result.termination = Termination::exited;
    if(WIFEXITED(wait_status))
      result.exit_code = WEXITSTATUS(wait_status);
    else if(WIFSIGNALED(wait_status))
      result.exit_code = 128 + WTERMSIG(wait_status);
  }
  return result;
#endif
}

Result run(const std::filesystem::path &executable,
           std::span<const std::string> arguments,
           const RunOptions &options)
{
  return SystemRunner{}.run(executable, arguments, options);
}
} // namespace process
