#include "project_settings.hpp"

#include "atomic_file.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;
namespace settings = notepp::project_settings;
using Json = nlohmann::json;

namespace
{
int failures = 0;

void expect(bool condition, std::string_view message)
{
  if(condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

class TempDir
{
public:
  TempDir()
  {
    path_ = fs::temp_directory_path() / "notepp_project_settings_tests";
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  ~TempDir() { std::error_code error; fs::remove_all(path_, error); }
  const fs::path &path() const { return path_; }

private:
  fs::path path_;
};

Json read_json(const fs::path &path)
{
  std::ifstream input(path);
  return Json::parse(input);
}

void test_round_trip()
{
  TempDir temp;
  settings::Store store(temp.path() / "project_settings.json");
  expect(store.load().success, "missing settings load succeeds");
  expect(store.set_language("ca").success, "language saves");
  expect(store.set_git_sync_enabled(true).success, "Git Sync saves");
  const auto loaded = settings::Store(temp.path() / "project_settings.json").load();
  expect(loaded.success, "settings reload succeeds");
  expect(loaded.settings.language == "ca", "language round trips");
  expect(loaded.settings.git_sync_enabled, "Git Sync round trips");
}

void test_unknown_fields_and_future_schema()
{
  TempDir temp;
  const fs::path path = temp.path() / "project_settings.json";
  {
    std::ofstream output(path);
    output << R"({"schemaVersion":1,"language":"en","gitSyncEnabled":false,"future":7})";
  }
  settings::Store store(path);
  expect(store.load().success, "legacy project settings load");
  expect(read_json(path)["future"] == 7, "unknown project fields are preserved");

  {
    std::ofstream output(path);
    output << R"({"schemaVersion":99,"language":"en"})";
  }
  expect(!store.load().success, "future project schema is rejected");
}

void test_unsigned_future_schema_is_rejected()
{
  TempDir temp;
  const fs::path path = temp.path() / "project_settings.json";
  {
    std::ofstream output(path);
    output << R"({"schemaVersion":4294967296,"language":"en","gitSyncEnabled":false})";
  }
  settings::Store store(path);
  expect(!store.load().success, "unsigned future schema is rejected");
  const auto loaded = atomic_file::read_text(path);
  expect(loaded && loaded.snapshot.content.find("4294967296") != std::string::npos,
         "unsigned future schema is not rewritten");
}

void test_invalid_values_are_replaced_with_defaults()
{
  TempDir temp;
  const fs::path path = temp.path() / "project_settings.json";
  {
    std::ofstream output(path);
    output << R"({"schemaVersion":1,"language":7,"gitSyncEnabled":"yes"})";
  }
  settings::Store store(path);
  const auto loaded = store.load();
  expect(loaded.success, "invalid project values load with defaults");
  expect(loaded.settings.language.empty(), "invalid language defaults to empty");
  expect(!loaded.settings.git_sync_enabled, "invalid Git Sync value defaults disabled");
}
} // namespace

int main()
{
  test_round_trip();
  test_unknown_fields_and_future_schema();
  test_unsigned_future_schema_is_rejected();
  test_invalid_values_are_replaced_with_defaults();
  if(failures != 0)
  {
    std::cerr << failures << " project settings test expectation(s) failed\n";
    return 1;
  }
  std::cout << "project settings tests passed\n";
  return 0;
}
