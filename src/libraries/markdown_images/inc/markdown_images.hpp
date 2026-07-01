#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace MarkdownImages
{
/**
 * @brief True if the URL is an http:// or https:// external link.
 */
bool is_external_link(std::string_view href);

/**
 * @brief URL-decode a single link component (percent + plus).
 */
std::string decode_link_component(std::string_view s);

/**
 * @brief Resolve an image href into an absolute filesystem path.
 *
 * @param href          The original href from the markdown source.
 * @param assets_root   The configured assets directory.
 * @param document_dir  The parent directory of the current note (may be empty).
 *
 * Lookup order:
 *   1. The href itself, if absolute and existing.
 *   2. href resolved relative to document_dir.
 *   3. href resolved relative to the assets parent (repo root).
 *   4. href resolved relative to the assets root.
 *   5. If href starts with "assets/", href stripped of that prefix relative
 *      to the assets root.
 *
 * @return Resolved existing file path, or empty path if none match.
 */
std::filesystem::path resolve_image_path(std::string_view href,
                                         const std::filesystem::path &assets_root,
                                         const std::filesystem::path &document_dir);
} // namespace MarkdownImages