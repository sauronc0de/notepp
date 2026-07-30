#include "git_sync.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace gs = notepp::git_sync;

namespace
{
int failures = 0;

void expect(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

process::Result exited(int code = 0, std::string output = {}, std::string error = {})
{
  return {.termination = process::Termination::exited,
          .exit_code = code,
          .stdout_text = std::move(output),
          .stderr_text = std::move(error),
          .error = {},
          .output_truncated = false};
}

class FakeRunner final : public process::Runner
{
public:
  using Handler = std::function<process::Result(const std::vector<std::string> &, const process::RunOptions &)>;
  explicit FakeRunner(Handler handler) : handler_(std::move(handler)) {}

  process::Result run(const fs::path &executable, std::span<const std::string> arguments,
                      const process::RunOptions &options) const override
  {
    expect(executable == "git", "git client invokes only the system git executable");
    calls.emplace_back(arguments.begin(), arguments.end());
    return handler_(calls.back(), options);
  }

  mutable std::vector<std::vector<std::string>> calls;

private:
  Handler handler_;
};

bool ends_with(const std::vector<std::string> &arguments, std::initializer_list<std::string_view> suffix)
{
  if(arguments.size() < suffix.size()) return false;
  auto argument = arguments.end() - static_cast<std::ptrdiff_t>(suffix.size());
  for(const std::string_view expected : suffix)
  {
    if(*argument != expected) return false;
    ++argument;
  }
  return true;
}

FakeRunner make_status_runner(const fs::path &root, std::string porcelain)
{
  return FakeRunner([root, porcelain = std::move(porcelain)](const std::vector<std::string> &arguments,
                                                             const process::RunOptions &options) {
    expect(options.environment_overrides.at("GIT_TERMINAL_PROMPT") == "0", "git is noninteractive");
    if(ends_with(arguments, {"--version"})) return exited(0, "git version test\n");
    if(ends_with(arguments, {"rev-parse", "--show-toplevel"})) return exited(0, root.string() + "\n");
    if(ends_with(arguments, {"status", "--porcelain=v2", "--branch"})) return exited(0, porcelain);
    return exited(2, {}, "unexpected fake command");
  });
}

void test_status_parsing_and_repository_root()
{
  const fs::path root = fs::temp_directory_path() / "notepp_git_status_root";
  fs::create_directories(root);
  struct Case
  {
    std::string porcelain;
    gs::SyncState expected;
  };
  const std::vector<Case> cases{
      {"# branch.oid abc\n# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -0\n", gs::SyncState::clean},
      {"# branch.head main\n# branch.upstream origin/main\n# branch.ab +2 -0\n", gs::SyncState::ahead},
      {"# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -3\n", gs::SyncState::behind},
      {"# branch.head main\n# branch.upstream origin/main\n# branch.ab +1 -1\n", gs::SyncState::diverged},
      {"# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -0\n? notes/new.md\n", gs::SyncState::dirty},
      {"# branch.head main\n# branch.upstream origin/main\nu UU N... 100644 100644 100644 100644 a b c file\n", gs::SyncState::conflict},
      {"# branch.head main\n# branch.ab +0 -0\n", gs::SyncState::no_upstream}};
  for(const Case &test : cases)
  {
    FakeRunner runner = make_status_runner(root, test.porcelain);
    const gs::Status status = gs::Client(runner).inspect(root);
    expect(status.state == test.expected, "porcelain v2 state is parsed");
  }

  FakeRunner ancestor = make_status_runner(root.parent_path(),
                                           "# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -0\n");
  expect(gs::Client(ancestor).inspect(root).state == gs::SyncState::not_repository,
         "an ancestor repository is rejected");
  fs::remove_all(root);
}

void test_manual_sync_refuses_divergence()
{
  const fs::path root = fs::temp_directory_path() / "notepp_git_diverged_root";
  fs::create_directories(root);
  int status_calls = 0;
  FakeRunner runner([&](const std::vector<std::string> &arguments, const process::RunOptions &) {
    if(ends_with(arguments, {"--version"})) return exited(0, "git version test\n");
    if(ends_with(arguments, {"rev-parse", "--show-toplevel"})) return exited(0, root.string() + "\n");
    if(ends_with(arguments, {"status", "--porcelain=v2", "--branch"}))
    {
      ++status_calls;
      if(status_calls == 1)
        return exited(0, "# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -0\n? local.md\n");
      return exited(0, "# branch.head main\n# branch.upstream origin/main\n# branch.ab +1 -1\n");
    }
    if(ends_with(arguments, {"add", "--all", "--", "."})) return exited();
    if(ends_with(arguments, {"diff", "--cached", "--quiet", "--exit-code"})) return exited(1);
    if(arguments.size() >= 2 && arguments[arguments.size() - 2] == "-m" &&
       arguments[arguments.size() - 3] == "commit")
      return exited();
    if(ends_with(arguments, {"fetch"})) return exited();
    return exited(2, {}, "unexpected fake command");
  });

  const gs::OperationResult result = gs::Client(runner).manual_sync(root, "Notepp sync test");
  expect(!result.success && result.status.state == gs::SyncState::diverged,
         "manual sync preserves and reports divergent histories");
  for(const auto &call : runner.calls)
    expect(!ends_with(call, {"push"}) && !ends_with(call, {"pull", "--ff-only"}),
           "divergence does not push or pull automatically");
  fs::remove_all(root);
}

void test_safe_commands_offline_and_redaction()
{
  const fs::path root = fs::temp_directory_path() / "notepp_git_command_root";
  fs::create_directories(root);
  std::string head = "before";
  FakeRunner runner([&](const std::vector<std::string> &arguments, const process::RunOptions &) {
    if(ends_with(arguments, {"--version"})) return exited(0, "git version test\n");
    if(ends_with(arguments, {"rev-parse", "--show-toplevel"})) return exited(0, root.string() + "\n");
    if(ends_with(arguments, {"status", "--porcelain=v2", "--branch"}))
      return exited(0, "# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -0\n");
    if(ends_with(arguments, {"rev-parse", "HEAD"})) return exited(0, head + "\n");
    if(ends_with(arguments, {"pull", "--ff-only"}))
      return exited(1, {}, "fatal: unable to access 'https://secret-token@example.test/repo': Could not resolve host");
    return exited(2, {}, "unexpected fake command");
  });
  const gs::OperationResult result = gs::Client(runner).pull_on_open(root);
  expect(!result.success && result.status.state == gs::SyncState::offline, "network failure is reported as offline");
  expect(result.status.detail.find("secret-token") == std::string::npos, "URL credentials are redacted");
  bool saw_safe_pull = false;
  for(const auto &call : runner.calls)
  {
    saw_safe_pull = saw_safe_pull || ends_with(call, {"pull", "--ff-only"});
    for(const std::string &argument : call)
      expect(argument != "reset" && argument != "clean" && argument != "rebase" &&
                 argument != "stash" && argument != "checkout" && argument != "--force" &&
                 argument != "--force-with-lease",
             "forbidden destructive arguments are absent");
  }
  expect(saw_safe_pull, "pull always uses fast-forward-only mode");
  fs::remove_all(root);
}

process::Result git(std::initializer_list<std::string> arguments, const fs::path &working = {})
{
  process::RunOptions options;
  options.working_directory = working;
  options.timeout = std::chrono::seconds(20);
  const std::vector<std::string> values(arguments);
  return process::run("git", values, options);
}

void require_git(const process::Result &result, std::string_view action)
{
  if(result.succeeded()) return;
  ++failures;
  std::cerr << "FAIL: " << action << ": " << result.stderr_text << result.error << '\n';
}

void test_real_repository_integration()
{
  if(!git({"--version"}).succeeded())
  {
    std::cout << "git_sync integration skipped: system Git unavailable\n";
    return;
  }

  const fs::path base = fs::temp_directory_path() / "notepp_git_sync_integration";
  const fs::path remote = base / "remote.git";
  const fs::path project = base / "project with spaces";
  const fs::path peer = base / "peer";
  std::error_code cleanup_error;
  fs::remove_all(base, cleanup_error);
  fs::create_directories(base);

  require_git(git({"init", "--bare", remote.string()}), "initialize bare remote");
  require_git(git({"clone", remote.string(), project.string()}), "clone project");
  require_git(git({"config", "user.name", "Notepp Tests"}, project), "configure project name");
  require_git(git({"config", "user.email", "notepp@example.test"}, project), "configure project email");
  std::ofstream(project / "notepp.project.json") << "{}\n";
  require_git(git({"add", "."}, project), "stage initial project");
  require_git(git({"commit", "-m", "initial"}, project), "commit initial project");
  require_git(git({"push", "-u", "origin", "HEAD"}, project), "push initial project");

  process::SystemRunner runner;
  gs::Client client(runner);
  expect(client.inspect(project).state == gs::SyncState::clean, "real repository reports clean");

  std::ofstream(project / "notes.md") << "local note\n";
  const gs::OperationResult pushed = client.commit_and_push(project, "Notepp sync test");
  expect(pushed.success && pushed.status.state == gs::SyncState::clean,
         "local files are committed and pushed normally");

  require_git(git({"clone", remote.string(), peer.string()}), "clone peer");
  require_git(git({"config", "user.name", "Notepp Tests"}, peer), "configure peer name");
  require_git(git({"config", "user.email", "notepp@example.test"}, peer), "configure peer email");
  std::ofstream(peer / "remote.md") << "remote note\n";
  require_git(git({"add", "."}, peer), "stage peer change");
  require_git(git({"commit", "-m", "peer"}, peer), "commit peer change");
  require_git(git({"push"}, peer), "push peer change");

  const gs::OperationResult pulled = client.pull_on_open(project);
  expect(pulled.success && pulled.changed_worktree && fs::exists(project / "remote.md"),
         "open pull fast-forwards and reports changed worktree");

  fs::remove_all(base, cleanup_error);
}
} // namespace

int main()
{
  test_status_parsing_and_repository_root();
  test_manual_sync_refuses_divergence();
  test_safe_commands_offline_and_redaction();
  test_real_repository_integration();
  if(failures != 0)
  {
    std::cerr << failures << " git_sync test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "git_sync tests passed\n";
  return EXIT_SUCCESS;
}
