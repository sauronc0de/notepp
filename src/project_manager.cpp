#include "project_manager.hpp"

#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include "nfd.hpp"

namespace fs = std::filesystem;

namespace notepp::project
{
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
  fs::create_directories(configDir);

  std::ofstream file(get_config_file(), std::ios::trunc);

  if(!file)
    return;

  file << "{\n";
  file << "  \"lastProjectPath\": \"" << path.generic_string() << "\"\n";
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
  project.projectFile = root / "notepp.project.json";

  fs::create_directories(project.notes);
  fs::create_directories(project.assets);

  if(!fs::exists(project.projectFile))
  {
    std::ofstream file(project.projectFile);

    file << "{\n";
    file << "  \"name\": \"" << root.filename().generic_string() << "\",\n";
    file << "  \"version\": 1,\n";
    file << "  \"notesPath\": \"notes\",\n";
    file << "  \"assetsPath\": \"assets\"\n";
    file << "}\n";
  }

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