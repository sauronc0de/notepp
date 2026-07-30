#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <stop_token>
#include <string>

namespace process
{
enum class Termination
{
  exited,
  timed_out,
  cancelled,
  spawn_failed
};

struct RunOptions
{
  std::filesystem::path working_directory;
  std::chrono::milliseconds timeout{30000};
  std::map<std::string, std::string, std::less<>> environment_overrides;
  std::size_t max_output_bytes = 64U * 1024U;
  std::stop_token stop_token;
};

struct Result
{
  Termination termination = Termination::spawn_failed;
  std::int64_t exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
  std::string error;
  bool output_truncated = false;

  [[nodiscard]] bool succeeded() const noexcept
  {
    return termination == Termination::exited && exit_code == 0;
  }
};

class Runner
{
public:
  virtual ~Runner() = default;
  [[nodiscard]] virtual Result run(const std::filesystem::path &executable,
                                   std::span<const std::string> arguments,
                                   const RunOptions &options) const = 0;
};

class SystemRunner final : public Runner
{
public:
  [[nodiscard]] Result run(const std::filesystem::path &executable,
                           std::span<const std::string> arguments,
                           const RunOptions &options) const override;
};

[[nodiscard]] Result run(const std::filesystem::path &executable,
                         std::span<const std::string> arguments,
                         const RunOptions &options = {});
} // namespace process
