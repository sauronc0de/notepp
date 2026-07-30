#include "note_project.hpp"

#include "app_settings.hpp"
#include "atomic_file.hpp"

#include <array>
#include <cstdio>
#include <random>
#include <system_error>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include "log.hpp"
#include "nfd.hpp"

namespace fs = std::filesystem;

namespace notepp::project
{
namespace
{
constexpr int kProjectSchemaVersion = 2;

std::string generate_project_id()
{
  std::random_device random;
  std::uniform_int_distribution<unsigned int> distribution(0, 255);
  std::array<unsigned char, 16> bytes{};
  for(auto &byte : bytes)
    byte = static_cast<unsigned char>(distribution(random));
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0FU) | 0x40U);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3FU) | 0x80U);

  char value[37]{};
  std::snprintf(value, sizeof(value),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                bytes[12], bytes[13], bytes[14], bytes[15]);
  return value;
}

void load_or_create_manifest(ProjectInfo &project, bool layout_ok)
{
  if(!layout_ok) return;
  using Json = nlohmann::json;
  Json manifest = Json::object();
  const auto loaded_manifest = atomic_file::read_text(project.projectFile);
  if(!loaded_manifest)
  {
    LOG_ERROR(loaded_manifest.message);
    return;
  }
  bool should_write = !loaded_manifest.snapshot.existed;

  if(!should_write)
  {
    try
    {
      manifest = Json::parse(loaded_manifest.snapshot.content);
    }
    catch(const std::exception &error)
    {
      LOG_ERROR("Cannot parse notepp project manifest '", project.projectFile.generic_string(), "': ", error.what());
      return;
    }
    if(!manifest.is_object())
    {
      LOG_ERROR("Notepp project manifest is not an object: ", project.projectFile.generic_string());
      return;
    }
  }

  if(manifest.contains("projectId") && manifest["projectId"].is_string())
    project.projectId = manifest["projectId"].get<std::string>();
  if(project.projectId.empty())
  {
    project.projectId = generate_project_id();
    manifest["projectId"] = project.projectId;
    should_write = true;
  }

  if(manifest.contains("schemaVersion") && manifest["schemaVersion"].is_number_integer())
  {
    try
    {
      project.schemaVersion = manifest["schemaVersion"].get<int>();
    }
    catch(const std::exception &)
    {
      project.schemaVersion = 0;
    }
  }
  if(project.schemaVersion < kProjectSchemaVersion)
  {
    project.schemaVersion = kProjectSchemaVersion;
    manifest["schemaVersion"] = project.schemaVersion;
    should_write = true;
  }

  if(!manifest.contains("name"))
  {
    manifest["name"] = project.root.filename().generic_string();
    should_write = true;
  }
  if(!manifest.contains("notesPath"))
  {
    manifest["notesPath"] = "notes";
    should_write = true;
  }
  if(!manifest.contains("assetsPath"))
  {
    manifest["assetsPath"] = "assets";
    should_write = true;
  }

  if(!should_write) return;

  std::string serialized = manifest.dump(2);
  serialized.push_back('\n');
  const auto saved = atomic_file::save_text(
      project.projectFile, serialized, &loaded_manifest.snapshot);
  if(!saved)
  {
    LOG_ERROR("Cannot write project file: ", project.projectFile.generic_string(),
              ": ", saved.message);
    project.projectId.clear();
    project.schemaVersion = 0;
  }
}
} // namespace

fs::path get_appdata_dir()
{
#ifdef _WIN32
  PWSTR path = nullptr;

  if(FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)))
  {
    return fs::current_path() / "Notepp";
  }

  fs::path result = path;
  CoTaskMemFree(path);

  return result / "Notepp";
#else
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if(xdg && *xdg)
    return fs::path(xdg) / "Notepp";

  const char *home = std::getenv("HOME");
  if(home)
    return fs::path(home) / ".config" / "Notepp";

  return fs::current_path() / "Notepp";
#endif
}

fs::path get_config_file()
{
  return get_appdata_dir() / "config.json";
}

static fs::path get_recent_projects_file()
{
  return get_appdata_dir() / "recent_projects.txt";
}

void save_last_project_path(const fs::path &path)
{
  notepp::app_settings::Store store(get_config_file(), get_recent_projects_file());
  const auto updated = store.record_project(path);
  if(!updated)
  {
    LOG_ERROR("Cannot update Notepp app settings: ", updated.message);
  }
}

std::vector<fs::path> load_recent_projects()
{
  notepp::app_settings::Store store(get_config_file(), get_recent_projects_file());
  const auto loaded = store.load();
  if(!loaded)
  {
    LOG_ERROR("Cannot load Notepp app settings: ", loaded.message);
    return {};
  }

  std::vector<fs::path> result;
  for(const auto &path : loaded.settings.recent_projects)
    if(fs::exists(path)) result.push_back(path);
  return result;
}

std::optional<fs::path> load_last_project_path()
{
  notepp::app_settings::Store store(get_config_file(), get_recent_projects_file());
  const auto loaded = store.load();
  if(!loaded)
  {
    LOG_ERROR("Cannot load Notepp app settings: ", loaded.message);
    return std::nullopt;
  }
  if(!loaded.settings.last_project_path || !fs::exists(*loaded.settings.last_project_path))
    return std::nullopt;
  return loaded.settings.last_project_path;
}

std::optional<fs::path> select_project_folder()
{
  NFD::Guard guard;

  NFD::UniquePath outPath;
  const nfdresult_t result = NFD::PickFolder(outPath);

  if(result == NFD_OKAY)
    return fs::path(outPath.get());

  return std::nullopt;
}

ProjectInfo create_or_open_project(const fs::path &root)
{
  ProjectInfo project;
  project.root = root;
  project.notes = root / "notes";
  project.assets = root / "assets";
  project.config = root / "config";
  project.projectFile = root / "notepp.project.json";

  bool layoutOk = true;
  for(const auto &subdir : {project.notes, project.assets, project.config})
  {
    std::error_code ec;
    fs::create_directories(subdir, ec);
    if(ec)
    {
      LOG_ERROR("Cannot create project subdir '", subdir.generic_string(), "': ", ec.message());
      layoutOk = false;
    }
  }

  load_or_create_manifest(project, layoutOk);

  save_last_project_path(root);

  return project;
}

std::optional<ProjectInfo> initialize_project()
{
  if(auto lastPath = load_last_project_path())
  {
    return create_or_open_project(*lastPath);
  }

  auto selectedPath = select_project_folder();

  if(!selectedPath)
    return std::nullopt;

  return create_or_open_project(*selectedPath);
}
} // namespace notepp::project