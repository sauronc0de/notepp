#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace notepp::command_ipc
{
using Handler = std::function<std::string(std::string_view)>;

// A frame-boundary server: poll() is called by the GUI thread and executes the
// handler there. Requests are newline-delimited UTF-8 JSON documents.
class Server
{
public:
  Server() = default;
  ~Server();
  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;

  bool open(std::string endpoint, std::string *error = nullptr);
  void close() noexcept;
  // Processes at most max_requests pending connections.
  std::size_t poll(const Handler &handler, std::size_t max_requests = 8);
  bool is_open() const noexcept;

private:
  std::string endpoint_;
#ifdef _WIN32
  void *handle_ = nullptr;
  void *connect_event_ = nullptr;
  void *connect_overlapped_ = nullptr;
  bool connect_pending_ = false;
#else
  int descriptor_ = -1;
#endif
};

class Client
{
public:
  static std::string request(std::string_view endpoint, std::string_view request,
                             std::string *error = nullptr);
};
} // namespace notepp::command_ipc
