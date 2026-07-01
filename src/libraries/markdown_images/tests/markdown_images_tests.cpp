#include "markdown_images.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
namespace mi = MarkdownImages;

namespace
{
int failures = 0;

void expect_true(bool cond, std::string_view msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void expect_false(bool cond, std::string_view msg)
{
  expect_true(!cond, msg);
}

void expect_eq_str(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void write_file(const fs::path &p, std::string_view content)
{
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary).write(content.data(), static_cast<std::streamsize>(content.size()));
}

class ScopedTmp
{
public:
  ScopedTmp()
  {
    static int counter = 0;
    base_ = fs::temp_directory_path() /
        ("notepp_md_images_" + std::to_string(static_cast<long long>(++counter)));
    fs::create_directories(base_);
  }
  ~ScopedTmp() { std::error_code ec; fs::remove_all(base_, ec); }
  const fs::path &base() const { return base_; }

private:
  fs::path base_;
};

void test_is_external_link()
{
  expect_true(mi::is_external_link("http://example.com"), "http is external");
  expect_true(mi::is_external_link("https://example.com/img.png"), "https is external");
  expect_false(mi::is_external_link("assets/x.png"), "relative path is not external");
  expect_false(mi::is_external_link("/abs/path/img.png"), "absolute path is not external");
}

void test_decode_link_component_basic()
{
  expect_eq_str(mi::decode_link_component("hello%20world"), "hello world", "percent decode space");
  expect_eq_str(mi::decode_link_component("a+b"), "a b", "plus -> space");
  expect_eq_str(mi::decode_link_component("plain"), "plain", "no decoding needed");
}

void test_decode_link_component_invalid_hex()
{
  expect_eq_str(mi::decode_link_component("%ZZ"), "%ZZ", "invalid hex kept as-is");
  expect_eq_str(mi::decode_link_component("%2"), "%2", "incomplete escape kept");
}

void test_decode_link_component_mixed()
{
  expect_eq_str(mi::decode_link_component("path%2Fto%2Ffile"), "path/to/file", "encoded slashes");
}

void test_resolve_absolute_existing()
{
  ScopedTmp tmp;
  const fs::path img = tmp.base() / "abs.png";
  write_file(img, "x");

  fs::path result = mi::resolve_image_path(img.string(), tmp.base(), fs::path{});
  expect_eq_str(result.string(), img.string(), "absolute existing path returned");
}

void test_resolve_relative_to_document()
{
  ScopedTmp tmp;
  const fs::path note_dir = tmp.base() / "notes";
  const fs::path img = note_dir / "img.png";
  write_file(img, "x");

  fs::path result = mi::resolve_image_path("img.png", tmp.base(), note_dir);
  expect_eq_str(result.string(), img.string(), "relative to document dir");
}

void test_resolve_relative_to_assets_root()
{
  ScopedTmp tmp;
  const fs::path img = tmp.base() / "assets" / "x.png";
  write_file(img, "x");

  fs::path result = mi::resolve_image_path("x.png", tmp.base() / "assets", fs::path{});
  expect_eq_str(result.string(), img.string(), "relative to assets root");
}

void test_resolve_relative_to_repo_root()
{
  ScopedTmp tmp;
  const fs::path img = tmp.base() / "x.png";
  write_file(img, "x");

  // assets_root is tmp.base()/assets; parent is tmp.base()/ (the repo root)
  fs::path result = mi::resolve_image_path("x.png", tmp.base() / "assets", fs::path{});
  expect_eq_str(result.string(), img.string(), "relative to repo root (assets parent)");
}

void test_resolve_strips_assets_prefix()
{
  ScopedTmp tmp;
  const fs::path assets = tmp.base() / "assets";
  const fs::path img = assets / "stripped.png";
  write_file(img, "x");

  fs::path result = mi::resolve_image_path("assets/stripped.png", assets, fs::path{});
  expect_eq_str(result.string(), img.string(), "href with assets/ prefix is resolved by stripping");
}

void test_resolve_missing_returns_empty()
{
  ScopedTmp tmp;
  fs::path result = mi::resolve_image_path("nonexistent.png", tmp.base(), fs::path{});
  expect_true(result.empty(), "missing file returns empty path");
}
} // namespace

int main()
{
  test_is_external_link();
  test_decode_link_component_basic();
  test_decode_link_component_invalid_hex();
  test_decode_link_component_mixed();
  test_resolve_absolute_existing();
  test_resolve_relative_to_document();
  test_resolve_relative_to_assets_root();
  test_resolve_relative_to_repo_root();
  test_resolve_strips_assets_prefix();
  test_resolve_missing_returns_empty();
  if(failures != 0)
  {
    std::cerr << failures << " markdown_images test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "markdown_images tests passed\n";
  return EXIT_SUCCESS;
}