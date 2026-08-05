#include "command_ipc.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#endif

namespace notepp::command_ipc
{
namespace
{
void set_error(std::string *error, std::string message)
{
  if(error != nullptr) *error = std::move(message);
}
#ifndef _WIN32
bool write_all(int fd, std::string_view text)
{
  std::string framed(text);
  if(framed.empty() || framed.back() != '\n') framed.push_back('\n');
  std::size_t offset = 0;
  while(offset < framed.size())
  {
#ifdef MSG_NOSIGNAL
    constexpr int send_flags = MSG_NOSIGNAL;
#else
    constexpr int send_flags = 0;
#endif
    const ssize_t written = ::send(fd, framed.data() + offset, framed.size() - offset, send_flags);
    if(written <= 0) return false;
    offset += static_cast<std::size_t>(written);
  }
  return true;
}
std::string read_line(int fd, int timeout_ms)
{
  std::string result;
  char buffer[1024];
  while(result.size() < 1024U * 1024U)
  {
    pollfd wait{fd, POLLIN, 0};
    if(::poll(&wait, 1, timeout_ms) <= 0) break;
    const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
    if(count <= 0) break;
    result.append(buffer, static_cast<std::size_t>(count));
    const auto newline = result.find('\n');
    if(newline != std::string::npos) { result.resize(newline); break; }
  }
  return result;
}
#endif
} // namespace

Server::~Server() { close(); }

bool Server::open(std::string endpoint, std::string *error)
{
  close();
#ifdef _WIN32
  const HANDLE pipe = CreateNamedPipeA(endpoint.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1024 * 1024, 1024 * 1024,
      0, nullptr);
  if(pipe == INVALID_HANDLE_VALUE)
  {
    set_error(error, "cannot create named pipe");
    return false;
  }
  auto *overlapped = new OVERLAPPED{};
  overlapped->hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if(overlapped->hEvent == nullptr)
  {
    delete overlapped; CloseHandle(pipe); set_error(error, "cannot create named pipe event"); return false;
  }
  handle_ = pipe;
  connect_event_ = overlapped->hEvent;
  connect_overlapped_ = overlapped;
  endpoint_ = std::move(endpoint);
  return true;
#else
  (void)::signal(SIGPIPE, SIG_IGN);
  sockaddr_un address{};
  if(endpoint.empty() || endpoint.size() >= sizeof(address.sun_path))
  {
    set_error(error, "IPC endpoint is empty or too long");
    return false;
  }
  descriptor_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if(descriptor_ < 0) { set_error(error, std::strerror(errno)); return false; }
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, endpoint.c_str(), sizeof(address.sun_path) - 1U);
  ::unlink(endpoint.c_str());
  if(::bind(descriptor_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0 ||
     ::listen(descriptor_, 16) < 0)
  {
    set_error(error, std::strerror(errno));
    close();
    return false;
  }
  const int flags = ::fcntl(descriptor_, F_GETFL, 0);
  if(flags >= 0) (void)::fcntl(descriptor_, F_SETFL, flags | O_NONBLOCK);
  (void)::chmod(endpoint.c_str(), S_IRUSR | S_IWUSR);
  endpoint_ = std::move(endpoint);
  return true;
#endif
}

void Server::close() noexcept
{
#ifdef _WIN32
  if(connect_event_ != nullptr) CloseHandle(static_cast<HANDLE>(connect_event_));
  delete static_cast<OVERLAPPED *>(connect_overlapped_);
  connect_event_ = nullptr;
  connect_overlapped_ = nullptr;
  if(handle_ != nullptr) CloseHandle(static_cast<HANDLE>(handle_));
  handle_ = nullptr;
  connect_pending_ = false;
#else
  if(descriptor_ >= 0) ::close(descriptor_);
  if(!endpoint_.empty()) ::unlink(endpoint_.c_str());
  descriptor_ = -1;
#endif
  endpoint_.clear();
}

std::size_t Server::poll(const Handler &handler, std::size_t max_requests)
{
#ifdef _WIN32
  if(handle_ == nullptr || !handler || max_requests == 0U) return 0;
  HANDLE pipe = static_cast<HANDLE>(handle_);
  auto *connect = static_cast<OVERLAPPED *>(connect_overlapped_);
  if(!connect_pending_)
  {
    ResetEvent(static_cast<HANDLE>(connect_event_));
    const BOOL connected = ConnectNamedPipe(pipe, connect);
    const DWORD connect_error = connected ? ERROR_SUCCESS : GetLastError();
    if(connect_error == ERROR_IO_PENDING) { connect_pending_ = true; return 0; }
    if(connect_error != ERROR_SUCCESS && connect_error != ERROR_PIPE_CONNECTED) return 0;
  }
  if(connect_pending_)
  {
    if(WaitForSingleObject(static_cast<HANDLE>(connect_event_), 0) != WAIT_OBJECT_0) return 0;
    DWORD ignored = 0;
    if(!GetOverlappedResult(pipe, connect, &ignored, FALSE)) return 0;
    connect_pending_ = false;
  }
  char buffer[1024]; std::string request;
  while(request.size() < 1024U * 1024U)
  {
    OVERLAPPED read{}; read.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if(read.hEvent == nullptr) break;
    DWORD count = 0;
    const BOOL started = ReadFile(pipe, buffer, sizeof(buffer), &count, &read);
    if(!started && GetLastError() != ERROR_IO_PENDING) { CloseHandle(read.hEvent); break; }
    if(!started && WaitForSingleObject(read.hEvent, 250) != WAIT_OBJECT_0)
    { CancelIo(pipe); CloseHandle(read.hEvent); break; }
    if(!started && !GetOverlappedResult(pipe, &read, &count, FALSE)) { CloseHandle(read.hEvent); break; }
    CloseHandle(read.hEvent);
    if(count == 0U) break;
    request.append(buffer, count);
    if(request.find('\n') != std::string::npos) break;
  }
  const std::string response = handler(request);
  OVERLAPPED write{}; write.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  DWORD written = 0;
  if(write.hEvent != nullptr)
  {
    const BOOL started = WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &written, &write);
    if(!started && GetLastError() == ERROR_IO_PENDING) (void)WaitForSingleObject(write.hEvent, 250);
    CloseHandle(write.hEvent);
  }
  FlushFileBuffers(pipe);
  DisconnectNamedPipe(pipe);
  CloseHandle(pipe);
  handle_ = nullptr;
  connect_pending_ = false;
  const std::string endpoint = endpoint_;
  std::string ignored_error;
  (void)open(endpoint, &ignored_error);
  return 1;
#else
  if(descriptor_ < 0 || !handler) return 0;
  std::size_t processed = 0;
  while(processed < max_requests)
  {
    const int client = ::accept(descriptor_, nullptr, nullptr);
    if(client < 0)
    {
      if(errno == EAGAIN || errno == EWOULDBLOCK) break;
      continue;
    }
    const int client_flags = ::fcntl(client, F_GETFL, 0);
    if(client_flags >= 0) (void)::fcntl(client, F_SETFL, client_flags | O_NONBLOCK);
    const std::string request = read_line(client, 1000);
    const std::string response = handler(request);
    (void)write_all(client, response);
    ::shutdown(client, SHUT_RDWR);
    ::close(client);
    ++processed;
  }
  return processed;
#endif
}

bool Server::is_open() const noexcept
{
#ifdef _WIN32
  return handle_ != nullptr;
#else
  return descriptor_ >= 0;
#endif
}

std::string Client::request(std::string_view endpoint, std::string_view request, std::string *error)
{
#ifdef _WIN32
  HANDLE pipe = CreateFileA(std::string(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE,
                            0, nullptr, OPEN_EXISTING, 0, nullptr);
  if(pipe == INVALID_HANDLE_VALUE)
  {
    set_error(error, "cannot connect to named pipe");
    return {};
  }
  std::string framed(request);
  if(framed.empty() || framed.back() != '\n') framed.push_back('\n');
  DWORD written = 0;
  if(!WriteFile(pipe, framed.data(), static_cast<DWORD>(framed.size()), &written, nullptr))
  {
    set_error(error, "cannot write named pipe"); CloseHandle(pipe); return {};
  }
  char buffer[1024]; DWORD count = 0; std::string response;
  while(response.size() < 1024U * 1024U && ReadFile(pipe, buffer, sizeof(buffer), &count, nullptr) && count != 0U)
  {
    response.append(buffer, count);
    if(response.find('\n') != std::string::npos) break;
  }
  CloseHandle(pipe);
  if(response.empty()) set_error(error, "IPC server returned no response");
  return response;
#else
  const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if(descriptor < 0) { set_error(error, std::strerror(errno)); return {}; }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if(endpoint.empty() || endpoint.size() >= sizeof(address.sun_path))
  {
    set_error(error, "IPC endpoint is empty or too long"); ::close(descriptor); return {};
  }
  std::strncpy(address.sun_path, endpoint.data(), sizeof(address.sun_path) - 1U);
  if(::connect(descriptor, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0)
  {
    set_error(error, std::strerror(errno)); ::close(descriptor); return {};
  }
  if(!write_all(descriptor, request))
  {
    set_error(error, std::strerror(errno)); ::close(descriptor); return {};
  }
  const std::string response = read_line(descriptor, 30000);
  ::shutdown(descriptor, SHUT_RDWR);
  ::close(descriptor);
  if(response.empty()) set_error(error, "IPC server returned no response");
  return response;
#endif
}
} // namespace notepp::command_ipc
