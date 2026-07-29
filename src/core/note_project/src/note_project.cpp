#include "note_project.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include "log.hpp"
#include "nfd.hpp"
#include "tiny_json.hpp"

using TinyJson::json_escape;

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
  bool should_write = !fs::exists(project.projectFile);

  if(!should_write)
  {
    std::ifstream input(project.projectFile);
    try
    {
      input >> manifest;
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

  std::ofstream output(project.projectFile, std::ios::trunc);
  if(!output)
  {
    LOG_ERROR("Cannot open project file for writing: ", project.projectFile.generic_string());
    project.projectId.clear();
    project.schemaVersion = 0;
    return;
  }
  output << manifest.dump(2) << '\n';
  if(!output)
  {
    LOG_ERROR("Cannot write project file: ", project.projectFile.generic_string());
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
  const auto configDir = get_appdata_dir();
  std::error_code ec;
  fs::create_directories(configDir, ec);
  if(ec)
  {
    LOG_ERROR("Cannot create notepp config dir '", configDir.generic_string(), "': ", ec.message());
    return;
  }

  std::ofstream file(get_config_file(), std::ios::trunc);

  if(!file)
  {
    LOG_ERROR("Cannot open notepp config file for writing");
    return;
  }

  file << "{\n";
  file << "  \"lastProjectPath\": \"" << json_escape(path.generic_string()) << "\"\n";
  file << "}\n";

  // Maintain the recent projects list (most recent first, max 10)
  const auto recentFile = get_recent_projects_file();
  const std::string pathStr = path.generic_string();
  std::vector<std::string> lines;
  {
    std::ifstream rf(recentFile);
    std::string line;
    while(std::getline(rf, line))
      if(!line.empty() && line != pathStr)
        lines.push_back(line);
  }
  lines.insert(lines.begin(), pathStr);
  if(lines.size() > 10) lines.resize(10);

  std::ofstream wf(recentFile, std::ios::trunc);
  for(const auto &l : lines)
    wf << l << "\n";
}

std::vector<fs::path> load_recent_projects()
{
  std::ifstream file(get_recent_projects_file());
  if(!file) return {};

  std::vector<fs::path> result;
  std::string line;
  while(std::getline(file, line))
  {
    if(line.empty()) continue;
    fs::path p(line);
    if(fs::exists(p))
      result.push_back(p);
  }
  return result;
}

std::optional<fs::path> load_last_project_path()
{
  const auto configFile = get_config_file();

  if(!fs::exists(configFile))
    return std::nullopt;

  std::ifstream file(configFile);

  if(!file)
    return std::nullopt;

  std::stringstream buffer;
  buffer << file.rdbuf();

  const std::string content = buffer.str();
  const std::string key = "\"lastProjectPath\": \"";

  auto start = content.find(key);

  if(start == std::string::npos)
    return std::nullopt;

  start += key.size();

  auto end = content.find("\"", start);

  if(end == std::string::npos)
    return std::nullopt;

  fs::path result = content.substr(start, end - start);

  if(!fs::exists(result))
    return std::nullopt;

  return result;
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