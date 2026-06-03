#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace TinyJson
{
std::string json_escape(std::string_view s);
std::string json_unescape(std::string_view s);
size_t find_matching(std::string_view s, size_t start, char open, char close);
std::string json_find_string(std::string_view obj, std::string_view key);
int json_find_int(std::string_view obj, std::string_view key, int defv);
float json_find_float(std::string_view obj, std::string_view key, float defv);
bool json_find_bool(std::string_view obj, std::string_view key, bool defv);
std::vector<std::string_view> json_array_objects(std::string_view arr);
} // namespace TinyJson

