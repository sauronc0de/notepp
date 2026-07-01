#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace notepp::note_storage
{
/**
 * @brief Build a sanitized note path under a data root.
 *
 * @param data_root Root directory where notes live.
 * @param folder_name Folder segment (may contain '/' to allow nested folders).
 * @param note_title Note title (will be sanitized).
 * @return Absolute filesystem path ending in ".md".
 */
std::filesystem::path make_note_path(const std::filesystem::path &data_root,
                                     std::string_view folder_name,
                                     std::string_view note_title);

/**
 * @brief Make a unique title in a folder by suffixing " 2", " 3", ...
 *
 * @param existing_titles Titles already used in the folder.
 * @param base_title Desired base title; empty becomes "Note".
 * @return Unique sanitized title.
 */
std::string make_unique_note_title(const std::vector<std::string> &existing_titles,
                                   std::string_view base_title);
} // namespace notepp::note_storage