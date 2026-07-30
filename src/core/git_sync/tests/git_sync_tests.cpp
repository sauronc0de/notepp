#include "git_sync.hpp"

#include <algorithm>
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
    expect(options.environment_overrides.at("GIT_TERMINAL_PROMPT") == "0",
           "Git terminal prompts are disabled");
    expect(options.environment_overrides.at("GCM_INTERACTIVE") == "Never",
           "system credential manager lookup is noninteractive");
    expect(options.environment_overrides.at("GIT_ASKPASS") == "git" &&
               options.environment_overrides.at("SSH_ASKPASS") == "git",
           "askpass fallbacks fail immediately through non-helper Git invocation");
    expect(options.environment_overrides.at("SSH_ASKPASS_REQUIRE") == "never",
           "SSH askpass is disabled");
    expect(options.environment_overrides.at("GIT_SSH_COMMAND").find("BatchMode=yes") !=
               std::string::npos,
           "SSH password prompts are disabled");
    calls.emplace_back(arguments.begin(), arguments.end());
    const auto &call = calls.back();
    const auto has_config = [&](std::string_view setting) {
      for(std::size_t index = 0; index + 1U < call.size(); ++index)
        if(call[index] == "-c" && call[index + 1U] == setting) return true;
      return false;
    };
    expect(has_config("credential.interactive=never"),
           "credential helpers receive noninteractive configuration");
    expect(has_config("core.askPass="), "Git askpass helpers cannot prompt");
    expect(has_config("commit.gpgSign=false"), "commit signing cannot invoke pinentry");
    return handler_(call, options);
  }

  mutable std::vector<std::vector<std::string>> calls;

private:
  Handler handler_;
};

bool is_status_call(const std::vector<std::string> &arguments)
{
  return std::find(arguments.begin(), arguments.end(), "status") != arguments.end() &&
         std::find(arguments.begin(), arguments.end(), "--porcelain=v2") != arguments.end();
}

bool ends_with(const std::vector<std::string> &arguments, std::initializer_list<std::string_view> suffix)
{
  if(arguments.size() < suffix.size()) return false;
  if(!suffix.size()) return true;
  if(*suffix.begin() == "status") return is_status_call(arguments);
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
    if(ends_with(arguments, {"config", "--get", "branch.main.remote"})) return exited(0, "origin\n");
    if(ends_with(arguments, {"config", "--get", "branch.main.merge"})) return exited(0, "refs/heads/main\n");
    if(ends_with(arguments, {"fetch", "--no-tags", "--", "origin"})) return exited();
    return exited(2, {}, "unexpected fake command");
  });

  const gs::OperationResult result = gs::Client(runner).manual_sync(root, "Notepp sync test");
  expect(!result.success && result.status.state == gs::SyncState::diverged,
         "manual sync preserves and reports divergent histories");
  for(const auto &call : runner.calls)
    expect(std::find(call.begin(), call.end(), "push") == call.end() &&
               std::find(call.begin(), call.end(), "pull") == call.end(),
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
    if(ends_with(arguments, {"config", "--get", "branch.main.remote"})) return exited(0, "origin\n");
    if(ends_with(arguments, {"config", "--get", "branch.main.merge"})) return exited(0, "refs/heads/main\n");
    if(ends_with(arguments, {"rev-parse", "HEAD"})) return exited(0, head + "\n");
    if(ends_with(arguments, {"pull", "--ff-only", "--", "origin", "refs/heads/main"}))
      return exited(1, {}, "fatal: unable to access 'https://host/private/secret?access_token=query-secret': Could not resolve host");
    return exited(2, {}, "unexpected fake command");
  });
  const gs::OperationResult result = gs::Client(runner).pull_on_open(root);
  expect(!result.success && result.status.state == gs::SyncState::offline, "network failure is reported as offline");
  expect(result.status.detail.find("query-secret") == std::string::npos, "URL query credentials are redacted");
  bool saw_safe_pull = false;
  for(const auto &call : runner.calls)
  {
    saw_safe_pull = saw_safe_pull ||
                    ends_with(call, {"pull", "--ff-only", "--", "origin", "refs/heads/main"});
    for(const std::string &argument : call)
      expect(argument != "reset" && argument != "clean" && argument != "rebase" &&
                 argument != "stash" && argument != "checkout" && argument != "--force" &&
                 argument != "--force-with-lease",
             "forbidden destructive arguments are absent");
  }
  expect(saw_safe_pull, "pull always uses fast-forward-only mode");
  fs::remove_all(root);
}

void test_behind_dirty_and_failed_final_status()
{
  const fs::path root = fs::temp_directory_path() / "notepp_git_guard_root";
  fs::create_directories(root);

  FakeRunner behind_runner([&](const std::vector<std::string> &arguments, const process::RunOptions &) {
    if(ends_with(arguments, {"--version"})) return exited(0, "git version test\n");
    if(ends_with(arguments, {"rev-parse", "--show-toplevel"})) return exited(0, root.string() + "\n");
    if(ends_with(arguments, {"status", "--porcelain=v2", "--branch"}))
      return exited(0, "# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -1\n? local.md\n");
    return exited(2, {}, "unexpected fake command");
  });
  const gs::OperationResult behind = gs::Client(behind_runner).commit_and_push(root, "sync");
  expect(!behind.success && behind.status.behind == 1,
         "dirty-and-behind repository is rejected before committing");
  for(const auto &call : behind_runner.calls)
    expect(std::find(call.begin(), call.end(), "add") == call.end() &&
               std::find(call.begin(), call.end(), "commit") == call.end() &&
               std::find(call.begin(), call.end(), "push") == call.end(),
           "behind repository is not staged, committed, or pushed");

  int status_calls = 0;
  FakeRunner final_error([&](const std::vector<std::string> &arguments, const process::RunOptions &) {
    if(ends_with(arguments, {"--version"})) return exited(0, "git version test\n");
    if(ends_with(arguments, {"rev-parse", "--show-toplevel"})) return exited(0, root.string() + "\n");
    if(ends_with(arguments, {"status", "--porcelain=v2", "--branch"}))
    {
      ++status_calls;
      if(status_calls == 1)
        return exited(0, "# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -0\n");
      return exited(1, {}, "fatal: status failed");
    }
    if(ends_with(arguments, {"config", "--get", "branch.main.remote"})) return exited(0, "origin\n");
    if(ends_with(arguments, {"config", "--get", "branch.main.merge"})) return exited(0, "refs/heads/main\n");
    if(ends_with(arguments, {"rev-parse", "HEAD"})) return exited(0, "abc\n");
    if(ends_with(arguments, {"pull", "--ff-only", "--", "origin", "refs/heads/main"})) return exited();
    return exited(2, {}, "unexpected fake command");
  });
  const gs::OperationResult inspected = gs::Client(final_error).pull_on_open(root);
  expect(!inspected.success && inspected.status.state == gs::SyncState::error,
         "failed final inspection cannot report sync success");
  fs::remove_all(root);
}

void test_authentication_and_tls_are_not_offline()
{
  const fs::path root = fs::temp_directory_path() / "notepp_git_error_classification";
  fs::create_directories(root);
  for(const std::string diagnostic : {
          "fatal: unable to access 'https://host/repo': The requested URL returned error: 403",
          "fatal: unable to access 'https://host/repo': SSL certificate problem: unable to get local issuer"})
  {
    FakeRunner runner([&](const std::vector<std::string> &arguments, const process::RunOptions &) {
      if(ends_with(arguments, {"--version"})) return exited(0, "git version test\n");
      if(ends_with(arguments, {"rev-parse", "--show-toplevel"})) return exited(0, root.string() + "\n");
      if(ends_with(arguments, {"status", "--porcelain=v2", "--branch"}))
        return exited(0, "# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -0\n");
      if(ends_with(arguments, {"config", "--get", "branch.main.remote"})) return exited(0, "origin\n");
      if(ends_with(arguments, {"config", "--get", "branch.main.merge"})) return exited(0, "refs/heads/main\n");
      if(ends_with(arguments, {"rev-parse", "HEAD"})) return exited(0, "abc\n");
      if(ends_with(arguments, {"pull", "--ff-only", "--", "origin", "refs/heads/main"}))
        return exited(1, {}, diagnostic);
      return exited(2, {}, "unexpected fake command");
    });
    const gs::OperationResult result = gs::Client(runner).pull_on_open(root);
    expect(!result.success && result.status.state == gs::SyncState::error,
           "authentication and TLS failures are configuration errors, not offline");
    expect(result.status.detail.find("https://") == std::string::npos,
           "authentication and TLS diagnostics do not expose remote URLs");
  }
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

void test_offline_close_keeps_local_commit()
{
  const fs::path root = fs::temp_directory_path() / "notepp_git_offline_commit_root";
  fs::create_directories(root);
  std::vector<std::string> actions;
  FakeRunner runner([&](const std::vector<std::string> &arguments, const process::RunOptions &) {
    if(ends_with(arguments, {"--version"})) return exited(0, "git version test\n");
    if(ends_with(arguments, {"rev-parse", "--show-toplevel"})) return exited(0, root.string() + "\n");
    if(ends_with(arguments, {"status", "--porcelain=v2", "--branch"}))
      return exited(0, "# branch.head main\n# branch.upstream origin/main\n# branch.ab +0 -0\n? local.md\n");
    if(ends_with(arguments, {"config", "--get", "branch.main.remote"})) return exited(0, "origin\n");
    if(ends_with(arguments, {"config", "--get", "branch.main.merge"})) return exited(0, "refs/heads/main\n");
    if(std::find(arguments.begin(), arguments.end(), "add") != arguments.end())
    {
      expect(std::find(arguments.begin(), arguments.end(), ":(exclude,glob)**/*.bak") !=
                 arguments.end(),
             "backup files are excluded from staging");
      expect(std::find(arguments.begin(), arguments.end(),
                       ":(exclude,glob)**/*.~npp-t-*") != arguments.end(),
             "atomic temporary files are excluded from staging");
      expect(std::find(arguments.begin(), arguments.end(), ":(exclude,glob)**/*.tmp") !=
                 arguments.end(),
             "legacy temporary files are excluded from staging");
      actions.emplace_back("add");
      return exited();
    }
    if(std::find(arguments.begin(), arguments.end(), "--name-only") != arguments.end())
      return exited(0, std::string("notes/local.md\0", 15));
    if(std::find(arguments.begin(), arguments.end(), "commit") != arguments.end())
    {
      expect(std::find(arguments.begin(), arguments.end(), "--no-gpg-sign") != arguments.end(),
             "sync commits explicitly disable signing prompts");
      actions.emplace_back("commit");
      return exited();
    }
    if(ends_with(arguments, {"fetch", "--no-tags", "--", "origin"}))
    {
      actions.emplace_back("fetch");
      return exited(1, {}, "Could not resolve host");
    }
    return exited(2, {}, "unexpected fake command");
  });
  const auto result = gs::Client(runner).commit_and_push(root, "Notepp sync test");
  expect(!result.success && result.status.state == gs::SyncState::offline,
         "offline close reports an offline failure");
  expect(actions == std::vector<std::string>({"add", "commit", "fetch"}),
         "local commit is created before the offline fetch");
  fs::remove_all(root);
}

void test_exception_conversion()
{
  const auto result = gs::exception_result(
      "Manual Git Sync", "unexpected failure at https://token@host/repo?access_token=secret");
  expect(!result.success && result.status.state == gs::SyncState::error,
         "exceptions become ordinary Git error results");
  expect(result.status.detail.find("token") == std::string::npos &&
             result.status.detail.find("secret") == std::string::npos,
         "exception details are credential-redacted");
}

void test_state_name_round_trip()
{
  for(const gs::SyncState state : {gs::SyncState::unavailable, gs::SyncState::not_repository,
                                   gs::SyncState::no_upstream, gs::SyncState::clean,
                                   gs::SyncState::dirty, gs::SyncState::ahead,
                                   gs::SyncState::behind, gs::SyncState::diverged,
                                   gs::SyncState::syncing, gs::SyncState::offline,
                                   gs::SyncState::conflict, gs::SyncState::error})
    expect(gs::state_from_name(gs::state_name(state)) == state, "Git state name round trips");
  expect(gs::state_from_name("unknown") == gs::SyncState::error, "unknown state becomes error");
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
  const fs::path decoy = base / "decoy.git";
  std::error_code cleanup_error;
  fs::remove_all(base, cleanup_error);
  fs::create_directories(base);

  require_git(git({"init", "--bare", remote.string()}), "initialize bare remote");
  require_git(git({"init", "--bare", decoy.string()}), "initialize decoy remote");
  require_git(git({"clone", remote.string(), project.string()}), "clone project");
  require_git(git({"config", "user.name", "Notepp Tests"}, project), "configure project name");
  require_git(git({"config", "user.email", "notepp@example.test"}, project), "configure project email");
  fs::create_directories(project / "notes");
  fs::create_directories(project / "assets");
  fs::create_directories(project / "config");
  std::ofstream(project / "notepp.project.json") << "{}\n";
  require_git(git({"add", "."}, project), "stage initial project");
  require_git(git({"commit", "-m", "initial"}, project), "commit initial project");
  require_git(git({"push", "-u", "origin", "HEAD"}, project), "push initial project");
  require_git(git({"remote", "add", "decoy", decoy.string()}, project), "add decoy remote");
  require_git(git({"config", "remote.pushDefault", "decoy"}, project), "configure misleading push default");
  require_git(git({"config", "commit.gpgSign", "true"}, project),
              "configure signing that sync must override");
  require_git(git({"config", "user.signingkey", "notepp-test-missing-key"}, project),
              "configure unavailable signing key");

  process::SystemRunner runner;
  gs::Client client(runner);
  expect(client.inspect(project).state == gs::SyncState::clean, "real repository reports clean");

  std::ofstream(project / "notes" / "local.md") << "local note\n";
  std::ofstream(project / "notes" / "deleted.md.bak") << "undo backup\n";
  std::ofstream(project / "config" / "index.~npp-t-test.json") << "partial temporary\n";
  const gs::OperationResult pushed = client.commit_and_push(project, "Notepp sync test");
  expect(pushed.success && pushed.status.state == gs::SyncState::clean,
         "local files are committed and pushed normally");
  expect(!git({"show-ref"}, decoy).succeeded(),
         "explicit upstream push ignores a different remote.pushDefault");
  expect(git({"ls-files", "--error-unmatch", "notes/deleted.md.bak"}, project).exit_code != 0,
         "soft-delete backups are not committed");
  expect(git({"ls-files", "--error-unmatch", "config/index.~npp-t-test.json"}, project).exit_code != 0,
         "atomic temporary files are not committed");

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
  test_behind_dirty_and_failed_final_status();
  test_authentication_and_tls_are_not_offline();
  test_offline_close_keeps_local_commit();
  test_exception_conversion();
  test_state_name_round_trip();
  test_real_repository_integration();
  if(failures != 0)
  {
    std::cerr << failures << " git_sync test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "git_sync tests passed\n";
  return EXIT_SUCCESS;
}
