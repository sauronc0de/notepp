#pragma once

#include <string>
#include <string_view>

namespace StringUtils
{
std::string_view ltrim(std::string_view s);
std::string_view rtrim(std::string_view s);
std::string_view trim(std::string_view s);
bool starts_with(std::string_view s, std::string_view prefix);
std::string sanitize_note_filename(std::string title);
std::string to_lower_copy(std::string_view s);
float clamp01f(float v);
} // namespace StringUtils

