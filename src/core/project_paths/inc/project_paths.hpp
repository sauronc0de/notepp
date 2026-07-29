#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace notepp::project_paths
{
enum class PathError
{
  empty,
  absolute,
  traversal,
  outside_project,
  invalid_top_level,
  ambiguous_legacy,
  missing_legacy_target
};

struct LegacyPathResult
{
  std::filesystem::path absolute_path;
  std::string stored_path;
};

class ProjectPaths
{
public:
  explicit ProjectPaths(std::filesystem::path project_root);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] std::expected<std::string, PathError> encode(
      const std::filesystem::path &absolute_path) const;
  [[nodiscard]] std::expected<std::filesystem::path, PathError> decode(
      std::string_view stored_path) const;
  [[nodiscard]] std::expected<LegacyPathResult, PathError> migrate_legacy(
      std::string_view stored_path,
      std::string_view expected_top_level) const;
  [[nodiscard]] std::expected<std::string, PathError> stable_key(
      const std::filesystem::path &absolute_path) const;

private:
  std::filesystem::path root_;
};

[[nodiscard]] std::string_view describe(PathError error) noexcept;
} // namespace notepp::project_paths
