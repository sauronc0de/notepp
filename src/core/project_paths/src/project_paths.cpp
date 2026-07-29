#include "project_paths.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace fs = std::filesystem;

namespace notepp::project_paths
{
namespace
{
bool is_parent_reference(const fs::path &path)
{
  return std::ranges::any_of(path, [](const fs::path &component) {
    return component == "..";
  });
}

bool is_contained_relative(const fs::path &relative)
{
  return !relative.empty() && relative != "." && !relative.is_absolute() &&
         !relative.has_root_name() && !is_parent_reference(relative);
}

fs::path containment_normalized(const fs::path &path)
{
  std::error_code error;
  const fs::path canonical = fs::weakly_canonical(path, error);
  return error ? fs::absolute(path).lexically_normal() : canonical.lexically_normal();
}

std::string slash_normalized(std::string_view value)
{
  std::string normalized(value);
  std::ranges::replace(normalized, '\\', '/');
  return normalized;
}

std::vector<std::string> components(std::string_view value)
{
  std::vector<std::string> result;
  const std::string normalized = slash_normalized(value);
  std::size_t begin = 0;
  while(begin <= normalized.size())
  {
    const std::size_t end = normalized.find('/', begin);
    const std::size_t count = end == std::string::npos ? normalized.size() - begin : end - begin;
    if(count != 0) result.emplace_back(normalized.substr(begin, count));
    if(end == std::string::npos) break;
    begin = end + 1;
  }
  return result;
}

bool valid_top_level(std::string_view value)
{
  if(value.empty() || value == "." || value == "..") return false;
  return value.find('/') == std::string_view::npos &&
         value.find('\\') == std::string_view::npos &&
         fs::path(std::string(value)).is_relative();
}
} // namespace

ProjectPaths::ProjectPaths(fs::path project_root)
    : root_(containment_normalized(std::move(project_root)))
{
}

const fs::path &ProjectPaths::root() const noexcept
{
  return root_;
}

std::expected<std::string, PathError> ProjectPaths::encode(
    const fs::path &absolute_path) const
{
  if(absolute_path.empty()) return std::unexpected(PathError::empty);

  const fs::path normalized = containment_normalized(absolute_path);
  const fs::path relative = normalized.lexically_relative(root_);
  if(!is_contained_relative(relative)) return std::unexpected(PathError::outside_project);

  const std::string result = relative.generic_string();
  if(result.empty()) return std::unexpected(PathError::empty);
  return result;
}

std::expected<fs::path, PathError> ProjectPaths::decode(
    std::string_view stored_path) const
{
  if(stored_path.empty()) return std::unexpected(PathError::empty);
  if(stored_path.contains('\\')) return std::unexpected(PathError::traversal);

  const fs::path relative{std::string(stored_path)};
  if(relative.is_absolute() || relative.has_root_name())
    return std::unexpected(PathError::absolute);

  const fs::path normalized = relative.lexically_normal();
  if(!is_contained_relative(normalized)) return std::unexpected(PathError::traversal);

  const fs::path result = containment_normalized(root_ / normalized);
  const fs::path containment = result.lexically_relative(root_);
  if(!is_contained_relative(containment)) return std::unexpected(PathError::outside_project);
  return result;
}

std::expected<LegacyPathResult, PathError> ProjectPaths::migrate_legacy(
    std::string_view stored_path,
    std::string_view expected_top_level) const
{
  if(stored_path.empty()) return std::unexpected(PathError::empty);
  if(!valid_top_level(expected_top_level))
    return std::unexpected(PathError::invalid_top_level);

  const fs::path native_path{std::string(stored_path)};
  if(native_path.is_absolute())
  {
    if(auto encoded = encode(native_path))
    {
      const auto encoded_parts = components(*encoded);
      if(!encoded_parts.empty() && encoded_parts.front() == expected_top_level)
        return LegacyPathResult{native_path.lexically_normal(), *encoded};
    }
  }
  else if(stored_path.find('\\') == std::string_view::npos)
  {
    if(auto decoded = decode(stored_path))
    {
      auto encoded = encode(*decoded);
      if(encoded && components(*encoded).front() == expected_top_level)
        return LegacyPathResult{*decoded, *encoded};
    }
  }

  const std::vector<std::string> parts = components(stored_path);
  std::vector<std::size_t> matches;
  for(std::size_t index = 0; index < parts.size(); ++index)
  {
    if(parts[index] == expected_top_level) matches.push_back(index);
    if(parts[index] == "..") return std::unexpected(PathError::traversal);
  }
  if(matches.empty()) return std::unexpected(PathError::outside_project);
  if(matches.size() != 1) return std::unexpected(PathError::ambiguous_legacy);

  fs::path relative;
  for(std::size_t index = matches.front(); index < parts.size(); ++index)
    relative /= parts[index];

  auto decoded = decode(relative.generic_string());
  if(!decoded) return std::unexpected(decoded.error());
  std::error_code error;
  if(!fs::exists(*decoded, error) || error)
    return std::unexpected(PathError::missing_legacy_target);

  return LegacyPathResult{*decoded, relative.generic_string()};
}

std::expected<std::string, PathError> ProjectPaths::stable_key(
    const fs::path &absolute_path) const
{
  return encode(absolute_path);
}

std::string_view describe(PathError error) noexcept
{
  switch(error)
  {
  case PathError::empty:
    return "path is empty";
  case PathError::absolute:
    return "portable path must be relative";
  case PathError::traversal:
    return "path traversal is not allowed";
  case PathError::outside_project:
    return "path is outside the project";
  case PathError::invalid_top_level:
    return "invalid project top-level directory";
  case PathError::ambiguous_legacy:
    return "legacy path contains an ambiguous project suffix";
  case PathError::missing_legacy_target:
    return "legacy path target does not exist in this project";
  }
  return "unknown project path error";
}
} // namespace notepp::project_paths
