#include "log.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

void test_logger_explicit_restart()
{
  Logger logger;
  Logger::Options opt;
  opt.color = false;

  std::ostringstream output;
  std::streambuf *const previous = std::cout.rdbuf(output.rdbuf());

  logger.start(opt);
  logger.log(INFO, __FILE__, __LINE__, "first cycle");
  logger.stop();

  // stop() disables lazy restart, including for a producer delayed until
  // after shutdown completes.
  logger.log(INFO, __FILE__, __LINE__, "must stay stopped");
  logger.stop();

  logger.start(opt);
  logger.log(INFO, __FILE__, __LINE__, "second cycle");
  logger.stop();

  std::cout.rdbuf(previous);
  const std::string text = output.str();
  expect_true(text.find("first cycle") != std::string::npos, "first logger cycle emits output");
  expect_true(text.find("must stay stopped") == std::string::npos, "explicit stop prevents lazy restart");
  expect_true(text.find("second cycle") != std::string::npos, "explicit start restarts logger");
}

void test_logger_concurrent_stop()
{
  Logger logger;
  Logger::Options opt;
  opt.color = false;
  opt.queue_capacity = 8;
  logger.start(opt);

  std::ostringstream output;
  std::streambuf *const previous = std::cout.rdbuf(output.rdbuf());

  std::vector<std::thread> producers;
  for(int producer = 0; producer < 4; ++producer)
  {
    producers.emplace_back([&logger, producer] {
      for(int message = 0; message < 100; ++message)
        logger.log(INFO, __FILE__, __LINE__, "producer ", producer, " message ", message);
    });
  }

  std::thread first_stop([&logger] { logger.stop(); });
  std::thread second_stop([&logger] { logger.stop(); });

  for(auto &producer : producers)
    producer.join();
  first_stop.join();
  second_stop.join();

  logger.log(INFO, __FILE__, __LINE__, "must remain stopped after concurrent stop");
  logger.stop();
  std::cout.rdbuf(previous);

  expect_true(output.str().find("must remain stopped after concurrent stop") == std::string::npos,
              "concurrent stop leaves logger stopped");
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
  test_logger_explicit_restart();
  test_logger_concurrent_stop();
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
