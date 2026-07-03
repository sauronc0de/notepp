#include "lang.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lang
{

namespace
{

std::filesystem::path g_lang_path;
std::vector<LanguageInfo> g_languages;
std::unordered_map<std::string, std::string> g_strings;
std::string g_current_code;
const LanguageInfo *g_current_info = nullptr;

void load_strings_from_file(const std::filesystem::path &path,
                            LanguageInfo &out_info,
                            std::unordered_map<std::string, std::string> &out_strings)
{
  std::ifstream f(path);
  if(!f) return;

  auto j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
  if(j.is_discarded()) return;

  auto get_str = [&](const char *key) -> std::string {
    auto it = j.find(key);
    if(it != j.end() && it->is_string()) return it->get<std::string>();
    return {};
  };

  out_info.code = get_str("code");
  out_info.name = get_str("name");
  out_info.short_code = get_str("short");
  out_info.flag_icon = get_str("flag_icon");

  auto sit = j.find("strings");
  if(sit != j.end() && sit->is_object())
  {
    for(auto &[key, val] : sit->items())
    {
      if(val.is_string())
        out_strings[key] = val.get<std::string>();
    }
  }
}

} // namespace

void init(const std::filesystem::path &languages_path)
{
  g_lang_path = languages_path;
  g_languages.clear();
  g_strings.clear();
  g_current_code.clear();
  g_current_info = nullptr;

  std::error_code ec;
  if(!std::filesystem::exists(languages_path, ec)) return;

  for(const auto &entry : std::filesystem::directory_iterator(languages_path, ec))
  {
    if(!entry.is_regular_file(ec)) continue;
    if(entry.path().extension() != ".json") continue;

    LanguageInfo info;
    std::unordered_map<std::string, std::string> dummy;
    load_strings_from_file(entry.path(), info, dummy);
    if(!info.code.empty())
      g_languages.push_back(std::move(info));
  }

  std::sort(g_languages.begin(), g_languages.end(),
            [](const LanguageInfo &a, const LanguageInfo &b) { return a.name < b.name; });

  // Default to English; fall back to first available language.
  if(!set_language("en") && !g_languages.empty())
    set_language(g_languages.front().code);
}

bool set_language(const std::string &code)
{
  if(code == g_current_code) return true;

  const LanguageInfo *found = nullptr;
  for(const auto &lang : g_languages)
  {
    if(lang.code == code)
    {
      found = &lang;
      break;
    }
  }
  if(!found) return false;

  LanguageInfo tmp;
  std::unordered_map<std::string, std::string> strings;
  load_strings_from_file(g_lang_path / (code + ".json"), tmp, strings);

  g_strings = std::move(strings);
  g_current_code = code;
  g_current_info = found;
  return true;
}

const std::vector<LanguageInfo> &languages()
{
  return g_languages;
}

const std::string &current_language_code()
{
  return g_current_code;
}

const LanguageInfo *current_language()
{
  return g_current_info;
}

const char *t(const char *key)
{
  auto it = g_strings.find(key);
  if(it != g_strings.end()) return it->second.c_str();
  return key;
}

} // namespace Lang
