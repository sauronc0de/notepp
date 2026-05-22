#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Lang {

struct LanguageInfo
{
  std::string code;       // "en", "ca"
  std::string name;       // "English", "Català"
  std::string short_code; // "EN", "CA"
  std::string flag_icon;  // filename in assets/icons/, e.g. "english.png"
};

// Scan assets/languages/ and load metadata for all *.json files.
void init(const std::filesystem::path &languages_path);

// All discovered languages (sorted by name).
const std::vector<LanguageInfo> &languages();

// Activate a language by code. Returns false if not found.
bool set_language(const std::string &code);

// Code of the currently active language ("en" by default).
const std::string &current_language_code();

// Pointer to the currently active LanguageInfo, or nullptr if none loaded.
const LanguageInfo *current_language();

// Return the translated string for key, or key itself if not found.
const char *t(const char *key);

} // namespace Lang
