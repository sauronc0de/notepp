#include "runtime_paths.hpp"

#include <cstdlib>
#include <mutex>
#include <string>

namespace NoteppPaths
{
namespace
{
std::once_flag g_init_once;
std::filesystem::path g_executable_dir;

std::filesystem::path normalize_existing_or_fallback(const std::filesystem::path &preferred, const std::filesystem::path &fallback)
{
  std::error_code ec;
  if(std::filesystem::exists(preferred, ec)) return preferred;
  return fallback;
}

void initialize_impl(const char *argv0)
{
  std::error_code ec;

  if(argv0 != nullptr && argv0[0] != '\0')
  {
    const std::filesystem::path raw(argv0);
    const std::filesystem::path absolute = raw.is_absolute() ? raw : std::filesystem::absolute(raw, ec);
    if(!absolute.empty())
    {
      g_executable_dir = absolute.parent_path();
      return;
    }
  }

  g_executable_dir = std::filesystem::current_path(ec);
}

std::filesystem::path default_assets_dir()
{
  return std::filesystem::path(NOTEPP_DEFAULT_ASSETS_PATH);
}

std::filesystem::path default_data_dir()
{
  return std::filesystem::path(NOTEPP_DEFAULT_DATA_PATH);
}
} // namespace

void initialize(const char *argv0)
{
  std::call_once(g_init_once, initialize_impl, argv0);
}

const std::filesystem::path &executable_dir()
{
  std::call_once(g_init_once, initialize_impl, nullptr);
  return g_executable_dir;
}

const std::filesystem::path &assets_dir()
{
  static const std::filesystem::path dir =
      normalize_existing_or_fallback(executable_dir() / "assets", default_assets_dir());
  return dir;
}

const std::filesystem::path &data_dir()
{
  static const std::filesystem::path dir =
      normalize_existing_or_fallback(executable_dir() / "data", default_data_dir());
  return dir;
}

std::filesystem::path asset(std::string_view relative_path)
{
  return assets_dir() / std::filesystem::path(relative_path);
}

std::filesystem::path data(std::string_view relative_path)
{
  return data_dir() / std::filesystem::path(relative_path);
}
} // namespace NoteppPaths
