#pragma once

#include <filesystem>
#include <string_view>

namespace NoteppPaths
{
void initialize(const char *argv0);

const std::filesystem::path &executable_dir();
const std::filesystem::path &assets_dir();
const std::filesystem::path &data_dir();

std::filesystem::path asset(std::string_view relative_path);
std::filesystem::path data(std::string_view relative_path);
} // namespace NoteppPaths
