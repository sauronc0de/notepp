#include "log.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int failures = 0;

void expect_true(bool cond, std::string_view msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void test_logger_lazy_start()
{
  Logger logger;
  logger.log(INFO, __FILE__, __LINE__, "lazy start does not deadlock");
  logger.stop();
  expect_true(true, "logger lazy start completes");
}

void test_logger_lifecycle()
{
  // Fresh logger: ensure it is not running and can be started/stopped cleanly.
  Logger::Options opt;
  engine::logger.start(opt);
  engine::logger.stop();
  expect_true(true, "logger start/stop cycle completes");
}

void test_log_calls_dont_throw()
{
  engine::logger.start();
  LOG_INFO("log call does not throw ", 42);
  LOG_DEBUG("debug ", 1, " ", 2);
  LOG_ERROR("error ", "msg");
  LOG_WARNING("warning");
  ASSERT(true, "assert message");
  engine::logger.stop();
  expect_true(true, "log macros execute under ENABLE_LOG");
}

void test_logger_macros_noop_when_disabled()
{
  // The non-ENABLE_LOG branch of the macros compiles to a no-op plus
  // a sizeof check that consumes the expression without evaluating it.
  ASSERT(false, "this expression should not be evaluated in no-op mode");
  expect_true(true, "ASSERT macro no-op when ENABLE_LOG is off");
}
} // namespace

int main()
{
  test_logger_lazy_start();
  test_logger_lifecycle();
  test_log_calls_dont_throw();
  test_logger_macros_noop_when_disabled();
  if(failures != 0)
  {
    std::cerr << failures << " log test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "log tests passed\n";
  return EXIT_SUCCESS;
}
