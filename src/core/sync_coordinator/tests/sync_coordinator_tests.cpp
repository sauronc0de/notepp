#include "sync_coordinator.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace sc = notepp::sync_coordinator;
namespace
{
int failures = 0;
void expect(bool condition, const char *message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

void test_project_write_policy()
{
  expect(sc::project_writes_allowed(false), "project writes are allowed while idle");
  expect(!sc::project_writes_allowed(true), "every UI branch blocks project writes during Git");
}

void test_disabled_open_makes_no_git_call()
{
  int git_calls = 0;
  int loads = 0;
  const auto result = sc::open(false, [&] { ++git_calls; return std::pair{true, false}; }, [&] { ++loads; });
  expect(git_calls == 0, "disabled open makes zero Git calls");
  expect(loads == 1 && result.continued, "disabled open still loads");
}

void test_disabled_lifecycle_makes_zero_git_calls()
{
  int git_calls = 0;
  const auto git = [&] {
    ++git_calls;
    return std::pair{true, false};
  };
  (void)sc::close(false, [] { return true; }, git);
  (void)sc::switch_project(false, [] { return true; }, git, git, [] {});
  (void)sc::manual(false, [] { return true; }, git, [] {});
  expect(git_calls == 0, "disabled lifecycle makes zero Git calls");
}

void test_pull_precedes_load_and_failures_still_load()
{
  std::vector<std::string> order;
  const auto result = sc::open(true, [&] { order.emplace_back("pull"); return std::pair{false, false}; }, [&] { order.emplace_back("load"); });
  expect(order == std::vector<std::string>({"pull", "load"}), "pull precedes load");
  expect(!result.git_succeeded && result.continued, "pull failure still continues");
}

void test_close_save_precedes_git_and_failure_still_exits()
{
  std::vector<std::string> order;
  const auto result = sc::close(true, [&] { order.emplace_back("save"); return true; }, [&] { order.emplace_back("push"); return std::pair{false, false}; });
  expect(order == std::vector<std::string>({"save", "push"}), "save precedes push");
  expect(!result.git_succeeded && result.continued, "push failure still exits");

  int pushes = 0;
  const auto failed_save = sc::close(true, [] { return false; }, [&] { ++pushes; return std::pair{true, false}; });
  expect(pushes == 0 && !failed_save.save_succeeded, "failed save skips Git");
}

void test_switch_ordering()
{
  std::vector<std::string> order;
  const auto result = sc::switch_project(
      true, [&] { order.emplace_back("save-old"); return true; },
      [&] { order.emplace_back("push-old"); return std::pair{true, false}; },
      [&] { order.emplace_back("pull-new"); return std::pair{false, false}; },
      [&] { order.emplace_back("load-new"); });
  expect(order == std::vector<std::string>({"save-old", "push-old", "pull-new", "load-new"}),
         "switch ordering");
  expect(result.continued && !result.git_succeeded, "switch loads despite Git failure");
}

void test_callback_exceptions_are_contained()
{
  const auto opened = sc::open(
      true, []() -> std::pair<bool, bool> { throw 1; }, [] { throw 2; });
  expect(opened.exception_caught && !opened.git_succeeded,
         "open converts callback exceptions to failure state");

  const auto closed = sc::close(
      true, []() -> bool { throw 1; }, [] { return std::pair{true, false}; });
  expect(closed.exception_caught && !closed.save_succeeded,
         "close contains save exceptions and skips Git");

  const auto switched = sc::switch_project(
      true, [] { return true; }, []() -> std::pair<bool, bool> { throw 1; },
      [] { return std::pair{true, false}; }, [] {});
  expect(switched.exception_caught && switched.continued,
         "switch remains nonblocking after Git exceptions");
}

void test_manual_changed_worktree_reloads_once()
{
  int reloads = 0;
  const auto result = sc::manual(true, [] { return true; }, [] { return std::pair{true, true}; }, [&] { ++reloads; });
  expect(result.reloaded && reloads == 1, "manual pull reloads exactly once");
}
} // namespace

int main()
{
  test_project_write_policy();
  test_disabled_open_makes_no_git_call();
  test_disabled_lifecycle_makes_zero_git_calls();
  test_pull_precedes_load_and_failures_still_load();
  test_close_save_precedes_git_and_failure_still_exits();
  test_switch_ordering();
  test_callback_exceptions_are_contained();
  test_manual_changed_worktree_reloads_once();
  if(failures != 0) return EXIT_FAILURE;
  std::cout << "sync_coordinator tests passed\n";
  return EXIT_SUCCESS;
}
