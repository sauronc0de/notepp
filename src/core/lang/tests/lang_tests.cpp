#include "lang.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
int failures = 0;

void expect_true(bool cond, std::string_view msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void expect_eq_str(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void expect_eq_int(long long a, long long b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got " << a << ", expected " << b << ")\n";
}

void write_file(const fs::path &p, const std::string &content)
{
  std::ofstream out(p, std::ios::binary);
  out << content;
}

fs::path make_temp_lang_dir()
{
  static int counter = 0;
  const fs::path base = fs::temp_directory_path() /
                        ("notepp_lang_tests_" + std::to_string(static_cast<long long>(++counter)));
  fs::create_directories(base);
  return base;
}

void test_init_loads_languages()
{
  const fs::path dir = make_temp_lang_dir();
  write_file(dir / "en.json", R"({
    "code": "en",
    "name": "English",
    "short": "EN",
    "flag_icon": "english.png",
    "strings": { "hello": "Hello", "bye": "Goodbye" }
  })");
  write_file(dir / "ca.json", R"({
    "code": "ca",
    "name": "Català",
    "short": "CA",
    "flag_icon": "catala.png",
    "strings": { "hello": "Hola" }
  })");
  write_file(dir / "broken.json", "{ this is not valid json");

  Lang::init(dir);

  const auto &langs = Lang::languages();
  expect_eq_int(static_cast<long long>(langs.size()), 2, "loads two valid languages (ignores broken)");

  // Names are sorted alphabetically: "Català" comes before "English".
  expect_eq_str(langs.front().code, "ca", "first language code is ca");
  expect_eq_str(langs.front().name, "Català", "first language name is Català");
  expect_eq_str(langs.back().code, "en", "last language code is en");
  expect_eq_str(langs.back().name, "English", "last language name is English");

  // Default language is "en" because the file exists.
  expect_eq_str(Lang::current_language_code(), "en", "default language is en");
  const Lang::LanguageInfo *cur = Lang::current_language();
  expect_true(cur != nullptr, "current language info is set");
  expect_eq_str(cur->short_code, "EN", "current language short code");

  // Default active language strings available.
  expect_eq_str(Lang::t("hello"), "Hello", "translate hello in en");
  expect_eq_str(Lang::t("bye"), "Goodbye", "translate bye in en");

  // Missing key returns the key itself.
  expect_eq_str(Lang::t("missing"), "missing", "missing key returns key");

  fs::remove_all(dir);
}

void test_set_language_changes_strings()
{
  const fs::path dir = make_temp_lang_dir();
  write_file(dir / "en.json", R"({"code":"en","name":"English","short":"EN","flag_icon":"e.png","strings":{"hello":"Hello"}})");
  write_file(dir / "ca.json", R"({"code":"ca","name":"Català","short":"CA","flag_icon":"c.png","strings":{"hello":"Hola"}})");
  Lang::init(dir);

  expect_true(Lang::set_language("ca"), "set_language ca succeeds");
  expect_eq_str(Lang::current_language_code(), "ca", "current code is ca");
  expect_eq_str(Lang::t("hello"), "Hola", "translate hello in ca");

  expect_true(!Lang::set_language("zz"), "set_language unknown fails");
  expect_eq_str(Lang::current_language_code(), "ca", "current code still ca after failed set");

  expect_true(Lang::set_language("en"), "set_language back to en succeeds");
  expect_eq_str(Lang::t("hello"), "Hello", "translate hello in en again");

  fs::remove_all(dir);
}

void test_init_missing_path_is_safe()
{
  const fs::path dir = make_temp_lang_dir() / "does_not_exist_subdir";
  Lang::init(dir);
  expect_eq_int(static_cast<long long>(Lang::languages().size()), 0, "no languages loaded from missing path");
  expect_eq_str(Lang::current_language_code(), "", "no current language code");
  expect_true(Lang::current_language() == nullptr, "no current language info");
  expect_eq_str(Lang::t("anything"), "anything", "t returns key when no language loaded");
}
} // namespace

int main()
{
  test_init_loads_languages();
  test_set_language_changes_strings();
  test_init_missing_path_is_safe();
  if(failures != 0)
  {
    std::cerr << failures << " lang test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "lang tests passed\n";
  return EXIT_SUCCESS;
}
