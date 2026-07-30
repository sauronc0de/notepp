#include "process.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace
{
int failures = 0;

void expect(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

std::string executable_path;

int child_main(int argc, char **argv)
{
  const std::string_view mode = argc > 2 ? argv[2] : "";
  if(mode == "echo")
  {
    std::cout << (argc > 3 ? argv[3] : "") << '\n';
    std::cerr << (argc > 4 ? argv[4] : "") << '\n';
    const char *value = std::getenv("NOTEPP_PROCESS_TEST");
    std::cout << (value != nullptr ? value : "missing") << '\n';
    std::cout << fs::current_path().filename().string() << '\n';
    return argc > 5 ? std::stoi(argv[5]) : 0;
  }
  if(mode == "large")
  {
    std::cout << std::string(8192, 'o');
    std::cerr << std::string(8192, 'e');
    return 0;
  }
  if(mode == "sleep")
  {
    std::this_thread::sleep_for(5s);
    return 0;
  }
  if(mode == "stdin")
  {
    char value = 0;
    std::cin.read(&value, 1);
    std::cout << (std::cin.eof() ? "eof" : "input") << '\n';
    return 0;
  }
  if(mode == "continuous")
  {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    const std::string output(4096, 'o');
    const std::string error(4096, 'e');
    for(;;)
    {
      std::cout << output;
      std::cerr << error;
    }
  }
#ifndef _WIN32
  if(mode == "tree")
  {
    if(argc < 4) return 2;
    const pid_t child = fork();
    if(child == 0)
    {
      std::signal(SIGTERM, SIG_IGN);
      std::this_thread::sleep_for(1500ms);
      std::ofstream(argv[3]) << "survived";
      _exit(0);
    }
    std::this_thread::sleep_for(5s);
    return 0;
  }
#endif
  return 3;
}

process::Result run_self(std::vector<std::string> arguments, process::RunOptions options = {})
{
  arguments.insert(arguments.begin(), "--child");
  return process::run(executable_path, arguments, options);
}

void test_arguments_environment_working_directory_and_exit()
{
  const fs::path directory = fs::temp_directory_path() / "notepp process tests cwd";
  fs::create_directories(directory);
  process::RunOptions options;
  options.working_directory = directory;
  options.environment_overrides["NOTEPP_PROCESS_TEST"] = "environment value";
  const process::Result result = run_self({"echo", "space quote\" slash\\ unicode-✓", "stderr value", "7"}, options);
  expect(result.termination == process::Termination::exited, "child exits normally");
  expect(result.exit_code == 7, "true child exit code is returned");
  expect(result.stdout_text.find("space quote\" slash\\ unicode-✓") != std::string::npos,
         "arguments are passed without shell interpretation");
  expect(result.stdout_text.find("environment value") != std::string::npos,
         "environment override is visible");
  expect(result.stdout_text.find(directory.filename().string()) != std::string::npos,
         "working directory is applied");
  expect(result.stderr_text.find("stderr value") != std::string::npos,
         "stderr is captured separately");
  fs::remove_all(directory);
}

void test_null_stdin()
{
  process::RunOptions options;
  options.timeout = 1s;
  const process::Result result = run_self({"stdin"}, options);
  expect(result.succeeded(), "child reading stdin exits normally");
  expect(result.stdout_text == "eof\n", "child stdin is connected to the null device");
}

void test_output_cap()
{
  process::RunOptions options;
  options.max_output_bytes = 100;
  const process::Result result = run_self({"large"}, options);
  expect(result.succeeded(), "large-output child succeeds");
  expect(result.stdout_text.size() == 100, "stdout is bounded");
  expect(result.stderr_text.size() == 100, "stderr is bounded");
  expect(result.output_truncated, "truncation is reported");
}

#ifdef _WIN32
void test_windows_environment_and_utf8_validation()
{
  _putenv_s("notepp_process_test", "inherited value");
  process::RunOptions options;
  options.environment_overrides["NOTEPP_PROCESS_TEST"] = "override value";
  const process::Result environment = run_self({"echo", "argument", "error", "0"}, options);
  expect(environment.succeeded() && environment.stdout_text.find("override value") != std::string::npos,
         "Windows environment overrides replace inherited keys case-insensitively");
  _putenv_s("notepp_process_test", "");

  const process::Result invalid = run_self({std::string(1, static_cast<char>(0xFF))});
  expect(invalid.termination == process::Termination::spawn_failed && !invalid.error.empty(),
         "invalid UTF-8 process arguments fail before spawning");
}
#endif

void test_missing_executable()
{
  const process::Result result = process::run("notepp-definitely-missing-executable", {});
  expect(result.termination == process::Termination::spawn_failed, "missing executable is a spawn failure");
  expect(!result.error.empty(), "spawn failure has details");
}

void test_timeout_and_cancellation()
{
  process::RunOptions timeout_options;
  timeout_options.timeout = 50ms;
  const process::Result timed_out = run_self({"sleep"}, timeout_options);
  expect(timed_out.termination == process::Termination::timed_out, "timeout terminates child");

  std::stop_source source;
  process::RunOptions cancel_options;
  cancel_options.timeout = 5s;
  cancel_options.stop_token = source.get_token();
  std::thread cancel([&source] {
    std::this_thread::sleep_for(50ms);
    source.request_stop();
  });
  const process::Result cancelled = run_self({"sleep"}, cancel_options);
  cancel.join();
  expect(cancelled.termination == process::Termination::cancelled, "stop token cancels child");

  process::RunOptions continuous_options;
  continuous_options.timeout = 100ms;
  continuous_options.max_output_bytes = 1024;
  const auto started = std::chrono::steady_clock::now();
  const process::Result continuous = run_self({"continuous"}, continuous_options);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  expect(continuous.termination == process::Termination::timed_out,
         "continuous dual-stream output cannot defeat timeout");
  expect(elapsed < 2s, "continuous output timeout remains bounded");
  expect(continuous.stdout_text.size() == 1024 && continuous.stderr_text.size() == 1024,
         "continuous dual streams respect output bounds");
}

#ifndef _WIN32
void test_timeout_terminates_process_group()
{
  const fs::path marker = fs::temp_directory_path() / "notepp_process_tree_marker";
  fs::remove(marker);
  process::RunOptions options;
  options.timeout = 50ms;
  const process::Result result = run_self({"tree", marker.string()}, options);
  expect(result.termination == process::Termination::timed_out, "tree parent times out");
  std::this_thread::sleep_for(1700ms);
  expect(!fs::exists(marker), "timeout kills descendants in the process group");
}
#endif
} // namespace

int main(int argc, char **argv)
{
  if(argc > 1 && std::string_view(argv[1]) == "--child") return child_main(argc, argv);
  executable_path = fs::absolute(argv[0]).string();
  test_arguments_environment_working_directory_and_exit();
  test_null_stdin();
  test_output_cap();
#ifdef _WIN32
  test_windows_environment_and_utf8_validation();
#endif
  test_missing_executable();
  test_timeout_and_cancellation();
#ifndef _WIN32
  test_timeout_terminates_process_group();
#endif
  if(failures != 0)
  {
    std::cerr << failures << " process test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "process tests passed\n";
  return EXIT_SUCCESS;
}
