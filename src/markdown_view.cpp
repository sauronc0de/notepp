#include "markdown_view.hpp"
#include "helpers.hpp"
#include "markdown_code_highlight.hpp"
#include "markdown_sections.hpp"
#include "string_utils.hpp"

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <SDL.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <SDL_image.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include "imgui_md.h"

namespace
{
struct Segment
{
  std::string text;
  bool colored = false;
  ImVec4 color{};
};

struct Chunk
{
  bool is_note = false;
  std::string text; // markdown to render
};

struct MdFonts
{
  ImFont *regular{};
  ImFont *italic{};
  ImFont *bold{};
};

static MdFonts g_fonts{};
std::filesystem::path g_assets_path;

struct TextureRecord
{
  GLuint texture_id = 0;
  ImVec2 size = ImVec2(0, 0);
  bool loaded = false;
};

static std::unordered_map<std::string, TextureRecord> g_image_cache{};
static float g_render_width = 0.0f;
static std::filesystem::path g_document_path;
static bool g_hover_preview_enabled = true;

enum class UrlFetchState
{
  pending,
  complete,
  failed
};

struct UrlFetchRecord
{
  UrlFetchState state = UrlFetchState::pending;
  std::filesystem::path local_path;
};

static std::unordered_map<std::string, UrlFetchRecord> g_url_fetches;
static std::mutex g_url_fetch_mutex;

struct HoverPreviewState
{
  bool active = false;
  int requested_frame = -1;
  ImVec2 mouse_pos = ImVec2(0, 0);
  std::string title;
  std::string body;
  std::string path;
};

static HoverPreviewState g_hover_preview;

struct ImageContextMenuState
{
  // Set when user right-clicks on a rendered image
  bool open_request = false;

  // Image source info captured at right-click time
  bool is_html = false;        // true if the image was <img ...>, false if ![](...)
  std::string src;             // raw src as md4c / HTML saw it (may be %XX encoded)
  std::string src_decoded;     // decoded version for searching the raw markdown text
  int orig_width = 0;          // width= from HTML; 0 when not specified / markdown
  int orig_height = 0;         // height= from HTML; 0 when not specified / markdown
  std::string html_tag;        // full original <img ...> tag text (only when is_html)
  ImVec2 natural_size;         // original pixel dimensions from the loaded texture

  // "Edit size..." dialog state
  bool edit_size_pending = false;  // set to open the modal on the next render pass
  int edit_width = 0;
  int edit_height = 0;
  bool proportional = true;
};
static ImageContextMenuState g_image_ctx;

static ImVec4 markdown_link_color()
{
  return ImVec4(0.56f, 0.82f, 1.0f, 1.0f);
}

static ImVec4 markdown_link_hover_color()
{
  return ImVec4(0.84f, 0.94f, 1.0f, 1.0f);
}

static std::filesystem::path resolve_image_path(std::string_view href)
{
  const std::filesystem::path p(href);
  if(p.is_absolute() && std::filesystem::exists(p)) return p;

  const std::filesystem::path asset_root = g_assets_path;
  const std::filesystem::path repo_root = asset_root.parent_path();

  std::vector<std::filesystem::path> candidates;
  candidates.push_back(p);

  // Relative to the note's own directory (highest priority for user images)
  if(!g_document_path.empty())
    candidates.push_back(g_document_path.parent_path() / p);

  candidates.push_back(repo_root / p);
  candidates.push_back(asset_root / p);

  if(starts_with(href, "assets/"))
  {
    std::filesystem::path rel = p.lexically_relative("assets");
    candidates.push_back(asset_root / rel);
  }

  for(const auto &c : candidates)
  {
    std::error_code ec;
    if(std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec))
      return c;
  }
  return {};
}

static bool is_external_link(std::string_view href)
{
  return starts_with(href, "http://") || starts_with(href, "https://");
}

static std::string decode_link_component(std::string_view s)
{
  std::string out;
  out.reserve(s.size());
  for(size_t i = 0; i < s.size(); ++i)
  {
    if(s[i] == '%' && i + 2 < s.size())
    {
      auto hex = [](char c) -> int {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
      };
      const int hi = hex(s[i + 1]);
      const int lo = hex(s[i + 2]);
      if(hi >= 0 && lo >= 0)
      {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    if(s[i] == '+')
      out.push_back(' ');
    else
      out.push_back(s[i]);
  }
  return out;
}

// ─── helpers used by the image context menu ──────────────────────────────────

static ImVec2 get_natural_size_for_src(const std::string &src)
{
  if(is_external_link(src))
  {
    auto it = g_image_cache.find(src);
    if(it != g_image_cache.end() && it->second.loaded) return it->second.size;
    return ImVec2(0, 0);
  }
  const auto resolved = resolve_image_path(src);
  if(!resolved.empty())
  {
    auto it = g_image_cache.find(resolved.string());
    if(it != g_image_cache.end() && it->second.loaded) return it->second.size;
  }
  auto it = g_image_cache.find(src);
  return (it != g_image_cache.end() && it->second.loaded) ? it->second.size : ImVec2(0, 0);
}

static bool find_image_in_text(const std::string &text,
                                bool is_html,
                                const std::string &html_tag,
                                const std::string &src,
                                size_t &out_start,
                                size_t &out_end)
{
  if(is_html)
  {
    if(html_tag.empty()) return false;
    const size_t pos = text.find(html_tag);
    if(pos == std::string::npos) return false;
    out_start = pos;
    out_end   = pos + html_tag.size();
    return true;
  }

  // Markdown: search for ](src) where the bracket pair is preceded by '!'
  const std::string needle = "](" + src;
  size_t sp = 0;
  while(sp < text.size())
  {
    const size_t pos = text.find(needle, sp);
    if(pos == std::string::npos) break;

    const size_t after_src = pos + needle.size();
    if(after_src < text.size() &&
       text[after_src] != ')' && text[after_src] != ' ' && text[after_src] != '"')
    {
      sp = pos + 1;
      continue;
    }

    size_t close = after_src;
    while(close < text.size() && text[close] != ')') ++close;
    if(close >= text.size()) { sp = pos + 1; continue; }

    if(pos == 0) { sp = pos + 1; continue; }

    int depth = 1;
    size_t br = pos - 1;
    bool found_bracket = false;
    while(true)
    {
      if(text[br] == ']')      ++depth;
      else if(text[br] == '[') { if(--depth == 0) { found_bracket = true; break; } }
      if(br == 0) break;
      --br;
    }

    if(found_bracket && br > 0 && text[br - 1] == '!')
    {
      out_start = br - 1;
      out_end   = close + 1;
      return true;
    }
    sp = pos + 1;
  }
  return false;
}

static std::string find_markdown_image_syntax(const std::string &markdown,
                                               const std::string &src,
                                               const std::string &src_decoded)
{
  size_t s, e;
  if(!src_decoded.empty() && src_decoded != src &&
     find_image_in_text(markdown, false, {}, src_decoded, s, e))
    return markdown.substr(s, e - s);
  if(find_image_in_text(markdown, false, {}, src, s, e))
    return markdown.substr(s, e - s);
  return "![](" + src + ")";
}

static bool replace_image_in_text(std::string &text,
                                   bool is_html,
                                   const std::string &html_tag,
                                   const std::string &src,
                                   const std::string &src_decoded,
                                   const std::string &new_text)
{
  size_t s, e;
  if(!is_html && !src_decoded.empty() && src_decoded != src &&
     find_image_in_text(text, false, {}, src_decoded, s, e))
  {
    text.replace(s, e - s, new_text);
    return true;
  }
  if(find_image_in_text(text, is_html, html_tag, src, s, e))
  {
    text.replace(s, e - s, new_text);
    return true;
  }
  return false;
}

static std::string slugify_heading(std::string_view s)
{
  std::string out;
  bool last_dash = false;
  for(char c : s)
  {
    const unsigned char uc = static_cast<unsigned char>(c);
    if(std::isalnum(uc))
    {
      out.push_back(static_cast<char>(std::tolower(uc)));
      last_dash = false;
    }
    else if(!out.empty() && !last_dash)
    {
      out.push_back('-');
      last_dash = true;
    }
  }
  while(!out.empty() && out.back() == '-') out.pop_back();
  return out;
}

struct InternalLinkTarget
{
  bool valid = false;
  std::filesystem::path note_path;
  std::string anchor;
};

static InternalLinkTarget resolve_internal_link(std::string_view href)
{
  InternalLinkTarget out;
  if(href.empty() || is_external_link(href)) return out;

  std::string path_part(href);
  const size_t hash = path_part.find('#');
  if(hash != std::string::npos)
  {
    out.anchor = slugify_heading(decode_link_component(path_part.substr(hash + 1)));
    path_part.resize(hash);
  }
  path_part = decode_link_component(path_part);

  std::filesystem::path resolved;
  if(!path_part.empty())
  {
    std::filesystem::path rel(path_part);
    if(rel.extension().empty()) rel += ".md";

    std::vector<std::filesystem::path> candidates;
    if(!g_document_path.empty())
      candidates.push_back(std::filesystem::path(g_document_path).parent_path() / rel);
    candidates.push_back(std::filesystem::path(DATA_PATH) / "notes" / rel);

    for(const auto &candidate : candidates)
    {
      std::error_code ec;
      if(std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
      {
        resolved = candidate;
        break;
      }
    }
  }
  else if(!g_document_path.empty())
  {
    resolved = std::filesystem::path(g_document_path);
  }

  if(resolved.empty()) return out;
  out.valid = true;
  out.note_path = std::move(resolved);
  return out;
}

static std::string read_text_file(const std::filesystem::path &path)
{
  std::ifstream in(path, std::ios::binary);
  if(!in) return {};
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::string extract_section_markdown(std::string_view markdown, std::string_view anchor)
{
  if(anchor.empty())
  {
    size_t cutoff = 0;
    int lines = 0;
    while(cutoff < markdown.size() && lines < 24)
    {
      const size_t next = markdown.find('\n', cutoff);
      cutoff = (next == std::string_view::npos) ? markdown.size() : next + 1;
      ++lines;
    }
    return std::string(markdown.substr(0, cutoff));
  }

  size_t pos = 0;
  size_t match_start = std::string_view::npos;
  int match_level = 0;
  while(pos < markdown.size())
  {
    const size_t line_start = pos;
    size_t line_end = markdown.find('\n', pos);
    if(line_end == std::string_view::npos) line_end = markdown.size();
    const std::string_view line = markdown.substr(line_start, line_end - line_start);

    int level = 0;
    std::string_view title;
    if(parse_heading_line(line, level, title))
    {
      const std::string slug = slugify_heading(title);
      if(match_start == std::string_view::npos)
      {
        if(slug == anchor)
        {
          match_start = line_start;
          match_level = level;
        }
      }
      else if(level <= match_level)
      {
        return std::string(markdown.substr(match_start, line_start - match_start));
      }
    }

    pos = (line_end < markdown.size()) ? line_end + 1 : line_end;
  }

  if(match_start != std::string_view::npos) return std::string(markdown.substr(match_start));
  return {};
}

static bool request_internal_link_preview(std::string_view href)
{
  if(!g_hover_preview_enabled) return false;

  const InternalLinkTarget target = resolve_internal_link(href);
  if(!target.valid) return false;

  const std::string markdown = read_text_file(target.note_path);
  if(markdown.empty()) return false;

  std::string body = extract_section_markdown(markdown, target.anchor);
  if(body.empty()) return false;

  g_hover_preview.active = true;
  g_hover_preview.requested_frame = ImGui::GetFrameCount();
  g_hover_preview.mouse_pos = ImGui::GetMousePos();
  g_hover_preview.path = target.note_path.string();
  g_hover_preview.title = std::string(href);
  g_hover_preview.body = std::move(body);
  return true;
}

static bool url_is_downloadable(std::string_view url)
{
  if(!starts_with(url, "http://") && !starts_with(url, "https://")) return false;
  for(unsigned char c : url)
  {
    if(!std::isalnum(c) && !::strchr("-._~:/?#[]@!$&()*+,;=%", (char)c))
      return false;
  }
  return true;
}

// Extract the image file extension from a URL path, e.g. ".svg", ".png".
// Returns empty string when none found or not a known image extension.
static std::string url_image_extension(std::string_view url)
{
  // Strip query string
  const size_t q = url.find('?');
  const std::string_view path = (q != std::string_view::npos) ? url.substr(0, q) : url;
  const size_t dot = path.rfind('.');
  const size_t slash = path.rfind('/');
  if(dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
    return {};
  std::string ext(path.substr(dot + 1));
  for(char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static const char *const known[] = {"svg", "png", "jpg", "jpeg", "gif", "bmp", "webp", nullptr};
  for(const char *const *p = known; *p; ++p)
    if(ext == *p) return "." + ext;
  return {};
}

// Peek at the first bytes of a downloaded file to detect SVG content.
static bool file_content_is_svg(const std::filesystem::path &p)
{
  std::ifstream f(p, std::ios::binary);
  char buf[256] = {};
  f.read(buf, sizeof(buf) - 1);
  return ::strstr(buf, "<svg") || ::strstr(buf, "<?xml");
}

static bool download_to_file_blocking(const std::string &url, const std::filesystem::path &dest)
{
#ifndef _WIN32
  const std::string dest_str = dest.string();
  const char *args[] = {"curl", "-s", "-L", "--max-time", "15",
                        "--output", dest_str.c_str(), url.c_str(), nullptr};
  const pid_t pid = ::fork();
  if(pid == 0)
  {
    ::execvp("curl", const_cast<char **>(args));
    ::_exit(1);
  }
  if(pid < 0) return false;
  int status;
  ::waitpid(pid, &status, 0);
  std::error_code ec;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
         std::filesystem::exists(dest, ec);
#else
  const std::string cmd = "curl -s -L --max-time 15 --output \"" +
                          dest.string() + "\" \"" + url + "\"";
  std::error_code ec;
  return ::system(cmd.c_str()) == 0 && std::filesystem::exists(dest, ec);
#endif
}

// Returns true when the SVG file uses <text> elements that nanosvg cannot render.
static bool file_svg_has_text(const std::filesystem::path &p)
{
  std::ifstream f(p, std::ios::binary);
  const std::string content(std::istreambuf_iterator<char>(f), {});
  return content.find("<text") != std::string::npos;
}

// Build a PNG variant of an SVG URL (strips .svg extension if present, appends .png).
static std::string svg_url_to_png(std::string_view url)
{
  const size_t q = url.find_first_of("?#");
  std::string path(q != std::string_view::npos ? url.substr(0, q) : url);
  const std::string_view tail = (q != std::string_view::npos) ? url.substr(q) : "";
  if(path.size() > 4 && path.substr(path.size() - 4) == ".svg")
    path.resize(path.size() - 4);
  return path + ".png" + std::string(tail);
}

static void start_url_fetch(const std::string &url)
{
  const std::string base_name = "notepp_img_" + std::to_string(std::hash<std::string>{}(url));
  const std::string url_ext = url_image_extension(url);
  const std::filesystem::path dest =
      std::filesystem::temp_directory_path() / (base_name + url_ext);

  std::thread([url, dest, base_name, url_ext]() {
    bool ok = download_to_file_blocking(url, dest);

    std::error_code ec;
    std::filesystem::path final_dest = dest;

    if(ok)
    {
      // No extension from URL: detect SVG by content and rename so SDL_image
      // picks the right decoder based on extension.
      if(url_ext.empty() && file_content_is_svg(dest))
      {
        const std::filesystem::path svg_dest =
            std::filesystem::temp_directory_path() / (base_name + ".svg");
        std::filesystem::rename(dest, svg_dest, ec);
        if(!ec) final_dest = svg_dest;
      }

      // nanosvg (SDL_image's SVG renderer) silently drops <text> elements.
      // If the SVG contains text, try fetching a PNG variant of the URL instead.
      if(final_dest.extension() == ".svg" && file_svg_has_text(final_dest))
      {
        const std::string png_url = svg_url_to_png(url);
        if(!png_url.empty() && png_url != url && url_is_downloadable(png_url))
        {
          const std::filesystem::path png_dest =
              std::filesystem::temp_directory_path() / (base_name + ".png");
          if(download_to_file_blocking(png_url, png_dest) &&
             !file_content_is_svg(png_dest))
          {
            std::filesystem::remove(final_dest, ec);
            final_dest = png_dest;
          }
        }
      }
    }

    {
      std::lock_guard<std::mutex> lk(g_url_fetch_mutex);
      auto &rec = g_url_fetches[url];
      rec.state = ok ? UrlFetchState::complete : UrlFetchState::failed;
      if(ok) rec.local_path = final_dest;
    }
  }).detach();
}

static TextureRecord load_texture_from_file(const std::filesystem::path &file)
{
  TextureRecord rec{};
  if(file.empty()) return rec;

  static bool img_ready = []() {
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    return true;
  }();
  (void)img_ready;

  SDL_Surface *loaded = IMG_Load(file.string().c_str());
  if(!loaded) return rec;

  SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(loaded);
  if(!rgba) return rec;

  GLuint tex = 0;
  glGenTextures(1, &tex);
  if(tex == 0)
  {
    SDL_FreeSurface(rgba);
    return rec;
  }

  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
  glBindTexture(GL_TEXTURE_2D, 0);

  rec.texture_id = tex;
  rec.size = ImVec2((float)rgba->w, (float)rgba->h);
  rec.loaded = true;
  SDL_FreeSurface(rgba);
  return rec;
}

// Remove a single leading '>' (and one optional following space) from a line
static std::string_view strip_quote_prefix(std::string_view line)
{
  line = ltrim(line);
  if(!line.empty() && line.front() == '>')
  {
    line.remove_prefix(1);
    if(!line.empty() && line.front() == ' ') line.remove_prefix(1);
  }
  return line;
}

// Collect blockquote chunks and normal chunks, detect note blocks by first meaningful line == "**Note**"
static std::vector<Chunk> split_note_blocks(std::string_view in)
{
  std::vector<Chunk> out;

  size_t i = 0;
  auto take_line = [&](size_t &pos) -> std::string_view {
    if(pos >= in.size()) return {};
    size_t e = in.find('\n', pos);
    if(e == std::string_view::npos) e = in.size();
    auto line = in.substr(pos, e - pos);
    pos = (e < in.size()) ? e + 1 : e;
    return line;
  };

  std::string normal_acc;

  auto flush_normal = [&]() {
    if(!normal_acc.empty())
    {
      out.push_back({false, std::move(normal_acc)});
      normal_acc.clear();
    }
  };

  while(i < in.size())
  {
    size_t line_start = i;
    std::string_view line = take_line(i);

    std::string_view t = ltrim(line);
    bool is_quote_line = starts_with(t, ">");

    if(!is_quote_line)
    {
      normal_acc.append(line.data(), line.size());
      normal_acc.push_back('\n');
      continue;
    }

    // Start of a quote block: keep consuming lines until an empty line.
    // Any line starting with '>' is treated as note content (prefix stripped).
    flush_normal();

    std::string quote_md;

    // Process first line + subsequent lines until blank line.
    size_t j = line_start;
    while(j < in.size())
    {
      std::string_view l = take_line(j);
      const std::string_view tl = trim(l);
      if(tl.empty())
      {
        // Blank line closes note mode.
        break;
      }

      std::string_view content = ltrim(l);
      if(starts_with(content, ">")) content = strip_quote_prefix(l);

      quote_md.append(content.data(), content.size());
      quote_md.push_back('\n');
    }

    i = j;
    out.push_back({true, std::move(quote_md)});
  }

  flush_normal();
  return out;
}

static bool parse_hex_color(std::string_view s, ImVec4 &out)
{
  // supports #RRGGBB or #RRGGBBAA
  if(s.size() != 7 && s.size() != 9) return false;
  if(s[0] != '#') return false;

  auto hex = [](char c) -> int {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };

  auto byte = [&](int i) -> int {
    int hi = hex(s[i]), lo = hex(s[i + 1]);
    if(hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
  };

  int r = byte(1), g = byte(3), b = byte(5);
  if(r < 0 || g < 0 || b < 0) return false;
  int a = 255;
  if(s.size() == 9)
  {
    a = byte(7);
    if(a < 0) return false;
  }

  out = ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
  return true;
}

static std::string normalize_markdown_link_destinations(std::string_view in)
{
  std::string out;
  out.reserve(in.size());

  bool in_code = false;
  for(size_t i = 0; i < in.size(); ++i)
  {
    const char c = in[i];
    if(c == '`')
    {
      in_code = !in_code;
      out.push_back(c);
      continue;
    }

    if(!in_code && c == ']' && i + 1 < in.size() && in[i + 1] == '(')
    {
      out.push_back(c);
      out.push_back('(');
      i += 2;

      bool wrapped = (i < in.size() && in[i] == '<');
      if(wrapped) out.push_back(in[i++]);

      for(; i < in.size(); ++i)
      {
        const char dc = in[i];
        if((wrapped && dc == '>') || (!wrapped && dc == ')'))
        {
          if(wrapped) out.push_back(dc);
          if(!wrapped) out.push_back(')');
          break;
        }
        if(dc == ' ')
          out += "%20";
        else
          out.push_back(dc);
      }
      if(i < in.size() && wrapped && in[i] == '>' && i + 1 < in.size() && in[i + 1] == ')')
      {
        out.push_back(')');
        ++i;
      }
      continue;
    }

    out.push_back(c);
  }

  return out;
}

static std::vector<Segment> split_color_spans(std::string_view in)
{
  std::vector<Segment> out;
  size_t i = 0;

  auto push_plain = [&](std::string_view sv) {
    if(!sv.empty()) out.push_back({std::string(sv), false, {}});
  };

  while(i < in.size())
  {
    size_t open = in.find("[color=", i);
    if(open == std::string_view::npos)
    {
      push_plain(in.substr(i));
      break;
    }

    // plain before tag
    push_plain(in.substr(i, open - i));

    // parse "[color=...]" header
    size_t close_bracket = in.find(']', open);
    if(close_bracket == std::string_view::npos)
    { // malformed
      push_plain(in.substr(open));
      break;
    }

    std::string_view spec = in.substr(open + 7, close_bracket - (open + 7)); // after "[color="
    ImVec4 c{};
    if(!parse_hex_color(spec, c))
    {
      // if invalid color, treat as plain text
      push_plain(in.substr(open, close_bracket - open + 1));
      i = close_bracket + 1;
      continue;
    }

    // find closing tag
    constexpr std::string_view end_tag = "[/color]";
    size_t end = in.find(end_tag, close_bracket + 1);
    if(end == std::string_view::npos)
    {
      // no closing tag -> treat everything as plain
      push_plain(in.substr(open));
      break;
    }

    // content inside
    std::string_view content = in.substr(close_bracket + 1, end - (close_bracket + 1));
    out.push_back({std::string(content), true, c});

    i = end + end_tag.size();
  }

  return out;
}

// Renderer: derive from imgui_md and override behavior.
struct MyMarkdown : public imgui_md
{
  bool   m_last_item_was_image    = false;
  ImVec2 m_last_rendered_image_sz = {0.0f, 0.0f};

  static bool fill_image_nfo(const TextureRecord &rec, image_info &nfo)
  {
    if(!rec.loaded || rec.texture_id == 0) return false;
    const float max_w = std::floor(std::max(8.0f, g_render_width));
    ImVec2 draw_size = rec.size;
    if(draw_size.x > max_w)
    {
      const float s = max_w / draw_size.x;
      draw_size.x = max_w;
      draw_size.y *= s;
    }
    draw_size.x = std::floor(draw_size.x);
    draw_size.y = std::floor(draw_size.y);
    nfo.texture_id = (ImTextureID)(uintptr_t)rec.texture_id;
    nfo.size = draw_size;
    nfo.uv0 = ImVec2(0, 0);
    nfo.uv1 = ImVec2(1, 1);
    nfo.col_tint = ImVec4(1, 1, 1, 1);
    nfo.col_border = ImVec4(0, 0, 0, 0);
    return true;
  }

  static void compact_newline(float tighten = 4.0f)
  {
    ImGui::NewLine();
    ImGui::SetCursorPosY(std::max(0.0f, ImGui::GetCursorPosY() - tighten));
  }

  bool get_image(image_info &nfo) const override
  {
    if(m_href.empty()) return false;

    if(is_external_link(m_href))
    {
      // Return immediately if already loaded as a texture
      auto tex_it = g_image_cache.find(m_href);
      if(tex_it != g_image_cache.end())
        return fill_image_nfo(tex_it->second, nfo);

      if(!url_is_downloadable(m_href)) return false;

      std::unique_lock<std::mutex> lk(g_url_fetch_mutex);
      auto fetch_it = g_url_fetches.find(m_href);

      if(fetch_it == g_url_fetches.end())
      {
        // First encounter: kick off background download
        g_url_fetches[m_href] = {UrlFetchState::pending, {}};
        const std::string url_copy = m_href;
        lk.unlock();
        start_url_fetch(url_copy);
        return false;
      }

      if(fetch_it->second.state != UrlFetchState::complete) return false;

      // Download finished: load texture on the main thread
      const std::filesystem::path local = fetch_it->second.local_path;
      lk.unlock();

      TextureRecord rec = load_texture_from_file(local);
      g_image_cache[m_href] = rec;
      return fill_image_nfo(rec, nfo);
    }

    const std::filesystem::path resolved = resolve_image_path(m_href);
    const std::string cache_key = resolved.empty() ? m_href : resolved.string();
    auto it = g_image_cache.find(cache_key);
    if(it == g_image_cache.end())
    {
      TextureRecord rec = load_texture_from_file(resolved);
      it = g_image_cache.emplace(cache_key, rec).first;
    }

    return fill_image_nfo(it->second, nfo);
  }

  static constexpr float k_inline_spacing = 12.0f;
  static constexpr float k_inline_max_h  = 48.0f;
  static constexpr float k_row_gap       =  6.0f;

  void SPAN_IMG(const MD_SPAN_IMG_DETAIL *d, bool e) override
  {
    if(e && d->src.size > 0)
    {
      // Pre-render: peek at cached size to decide wrapping before the image is placed.
      const std::string saved_href = m_href;
      m_href.assign(d->src.text, d->src.size);
      image_info pre_nfo;
      const bool has_pre = get_image(pre_nfo);
      m_href = saved_href;

      if(has_pre && m_last_item_was_image)
      {
        const float fscale = ImGui::GetIO().FontGlobalScale;
        const float est_h  = pre_nfo.size.y * fscale;
        const float est_w  = pre_nfo.size.x * fscale;
        if(est_h <= k_inline_max_h && ImGui::GetContentRegionAvail().x < est_w)
        {
          // Not enough room — wrap first so the image keeps its natural size.
          ImGui::NewLine();
          ImGui::Dummy(ImVec2(0.0f, k_row_gap)); // vertical breathing room between rows
          m_last_item_was_image = false;
        }
      }
      m_last_rendered_image_sz = {0.0f, 0.0f};
    }

    imgui_md::SPAN_IMG(d, e);

    if(e)
    {
      // Record rendered size now; spacing is applied in the leave callback.
      // Reason: render_text() calls SameLine(0,0) for the alt-text span (even
      // though the loop is skipped while m_is_image is true), which fires
      // between enter and leave and would overwrite any SameLine we set here.
      m_last_rendered_image_sz = ImGui::GetItemRectSize();
    }
    else
    {
      // Leave: apply inline spacing HERE, after alt-text's SameLine(0,0) has fired.
      // Our call wins because it is the last SameLine before the next widget.
      const ImVec2 sz = m_last_rendered_image_sz;

      // Right-click before SameLine so IsItemHovered() still refers to the Image widget.
      if(sz.x > 0.5f && sz.y > 0.5f &&
         ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        const std::string src_str(d->src.text, d->src.size);
        g_image_ctx.open_request  = true;
        g_image_ctx.is_html       = false;
        g_image_ctx.src           = src_str;
        g_image_ctx.src_decoded   = decode_link_component(src_str);
        g_image_ctx.orig_width    = 0;
        g_image_ctx.orig_height   = 0;
        g_image_ctx.html_tag.clear();
        g_image_ctx.natural_size  = get_natural_size_for_src(g_image_ctx.src_decoded);
        if(g_image_ctx.natural_size.x < 0.5f)
          g_image_ctx.natural_size = get_natural_size_for_src(src_str);
      }

      if(sz.x > 0.5f && sz.y > 0.5f && sz.y <= k_inline_max_h)
      {
        ImGui::SameLine(0.0f, k_inline_spacing);
        m_last_item_was_image = true;
      }
      else if(sz.x > 0.5f)
      {
        m_last_item_was_image = false;
      }
      m_last_rendered_image_sz = {0.0f, 0.0f};
    }
  }

  bool check_html(const char *str, const char *str_end) override
  {
    const size_t sz = str_end - str;

    // <img src="..." width="N" height="N"> — HTML image with optional size override
    if(sz >= 4 && strncmp(str, "<img", 4) == 0)
    {
      const std::string tag(str, str_end);

      auto get_attr = [&](const char *name) -> std::string {
        for(char q : {'"', '\''})
        {
          const std::string key = std::string(name) + "=" + q;
          const size_t p = tag.find(key);
          if(p == std::string::npos) continue;
          const size_t vs = p + key.size();
          const size_t ve = tag.find(q, vs);
          if(ve == std::string::npos) continue;
          return tag.substr(vs, ve - vs);
        }
        return {};
      };

      const std::string src = get_attr("src");
      if(!src.empty())
      {
        const std::string width_str = get_attr("width");
        const std::string height_str = get_attr("height");

        float ow = 0.0f, oh = 0.0f;
        try
        {
          if(!width_str.empty()) ow = (float)std::stoi(width_str);
          if(!height_str.empty()) oh = (float)std::stoi(height_str);
        }
        catch(...)
        {
        }

        const std::string saved = m_href;
        m_href = src;
        image_info nfo;
        const bool got = get_image(nfo);
        m_href = saved;

        if(got)
        {
          // Apply dimension overrides before scaling
          if(ow > 0.0f && oh > 0.0f)
            nfo.size = ImVec2(ow, oh);
          else if(ow > 0.0f && nfo.size.x > 0.0f)
            nfo.size = ImVec2(ow, ow * (nfo.size.y / nfo.size.x));
          else if(oh > 0.0f && nfo.size.y > 0.0f)
            nfo.size = ImVec2(oh * (nfo.size.x / nfo.size.y), oh);

          const float fscale = ImGui::GetIO().FontGlobalScale;
          nfo.size.x = std::floor(nfo.size.x * fscale);
          nfo.size.y = std::floor(nfo.size.y * fscale);

          ImGui::Image(nfo.texture_id, nfo.size, nfo.uv0, nfo.uv1, nfo.col_tint, nfo.col_border);

          if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
          {
            g_image_ctx.open_request  = true;
            g_image_ctx.is_html       = true;
            g_image_ctx.src           = src;
            g_image_ctx.src_decoded   = src; // HTML src is not URL-encoded
            g_image_ctx.orig_width    = (int)ow;
            g_image_ctx.orig_height   = (int)oh;
            g_image_ctx.html_tag      = tag;
            g_image_ctx.natural_size  = get_natural_size_for_src(src);
          }

          if(nfo.size.y <= k_inline_max_h)
          {
            ImGui::SameLine(0.0f, k_inline_spacing);
            m_last_item_was_image = true;
          }
        }
      }
      return true;
    }

    return imgui_md::check_html(str, str_end);
  }

  void BLOCK_P(bool e) override
  {
    if(!e)
    {
      // If a pending SameLine from inline images is open, cancel it before the paragraph break.
      if(m_last_item_was_image)
      {
        ImGui::NewLine();
        m_last_item_was_image = false;
        ImGui::Dummy(ImVec2(0.0f, k_row_gap)); // gap after last image row in paragraph
      }
      else
      {
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
      }
    }
  }

  void soft_break() override
  {
    // When the previous item was a small inline image, keep the cursor inline
    // so badge rows separated by single newlines flow horizontally.
    if(m_last_item_was_image) return;
    compact_newline(4.0f);
  }

  ImFont *get_font() const override
  {
    // imgui_md tracks current state for you:
    // m_is_em, m_is_strong, m_is_table_header, m_hlevel, etc.
    // See imgui_md.h. :contentReference[oaicite:1]{index=1}
    if(m_is_table_header)
    {
      return g_fonts.bold ? g_fonts.bold : ImGui::GetFont();
    }

    // Headings: make them bold (optionally bigger if you have a bigger font).
    if(m_hlevel > 0)
    {
      return g_fonts.bold ? g_fonts.bold : ImGui::GetFont();
    }

    // Strong -> bold
    if(m_is_strong)
    {
      return g_fonts.bold ? g_fonts.bold : ImGui::GetFont();
    }

    // Emphasis -> italic
    if(m_is_em)
    {
      return g_fonts.italic ? g_fonts.italic : ImGui::GetFont();
    }

    // Normal
    return g_fonts.regular ? g_fonts.regular : ImGui::GetFont();
  }

  ImVec4 get_color() const override
  {
    const ImGuiStyle &st = ImGui::GetStyle();

    // Link: use a bright readable accent independent of the note background
    if(!m_href.empty())
      return markdown_link_color();

    // Inline code: usually make it a bit “warm” or distinct
    if(m_is_code)
      return ImVec4(0.90f, 0.80f, 0.55f, 1.0f);

    // Headings: tint by level
    if(m_hlevel >= 1)
      return ImVec4(0.95f, 0.95f, 0.95f, 1.0f);

    // Emphasis: keep normal text color (don’t gray it out)
    if(m_is_em)
      return st.Colors[ImGuiCol_Text];

    // Strong: also normal (or slightly brighter)
    if(m_is_strong)
      return st.Colors[ImGuiCol_Text];

    // Default
    return st.Colors[ImGuiCol_Text];
  }

  bool on_link_hover() const override
  {
    return request_internal_link_preview(m_href);
  }

  void open_url() const override
  {
#if defined(__EMSCRIPTEN__)
    // no-op or use JS bridge
#else
    if(!resolve_internal_link(m_href).valid && !m_href.empty())
      SDL_OpenURL(m_href.c_str());
#endif
  }
};

static MyMarkdown &renderer()
{
  static MyMarkdown md;
  return md;
}

} // namespace

void MarkdownView::set_fonts(ImFont *regular, ImFont *italic, ImFont *bold)
{
  g_fonts = {regular, italic, bold};
}

void MarkdownView::set_render_width(float width)
{
  g_render_width = width > 0.0f ? width : 0.0f;
}

void MarkdownView::set_document_path(std::filesystem::path path)
{
  g_document_path = std::move(path);
}

void MarkdownView::set_assets_path(std::filesystem::path path)
{
  g_assets_path = std::move(path);
}

void MarkdownView::set_hover_preview_enabled(bool enabled)
{
  g_hover_preview_enabled = enabled;
}

MarkdownView::TextureHandle MarkdownView::get_or_load_texture(const std::filesystem::path &path)
{
  if(path.empty()) return TextureHandle{};
  const std::string key = path.string();
  auto it = g_image_cache.find(key);
  if(it == g_image_cache.end())
  {
    TextureRecord rec = load_texture_from_file(path);
    it = g_image_cache.emplace(key, rec).first;
  }
  if(!it->second.loaded || it->second.texture_id == 0) return TextureHandle{};
  TextureHandle h;
  h.id = (ImTextureID)(uintptr_t)it->second.texture_id;
  h.width = it->second.size.x;
  h.height = it->second.size.y;
  h.valid = true;
  return h;
}

bool MarkdownView::take_hover_preview(MarkdownHoverPreviewData &out)
{
  if(!g_hover_preview.active) return false;
  const int frame = ImGui::GetFrameCount();
  out.mouse_pos = g_hover_preview.mouse_pos;
  out.title = g_hover_preview.title;
  out.path = g_hover_preview.path;
  out.body = g_hover_preview.body;
  out.link_hovered = (g_hover_preview.requested_frame == frame);
  return true;
}

void MarkdownView::clear_hover_preview()
{
  g_hover_preview = HoverPreviewState{};
}

MarkdownView::ImageContextResult MarkdownView::render_image_context_menu(std::string &markdown)
{
  ImageContextResult out;

  // ── Step 1: if a right-click was detected this frame, open the context menu ──
  if(g_image_ctx.open_request)
  {
    out.consumed_right_click = true;
    g_image_ctx.open_request = false;

    // Initialise edit-size dialog fields from what was specified (HTML) or from
    // the natural texture dimensions.
    const int nat_w = (int)g_image_ctx.natural_size.x;
    const int nat_h = (int)g_image_ctx.natural_size.y;
    g_image_ctx.edit_width  = g_image_ctx.orig_width  > 0 ? g_image_ctx.orig_width
                            : nat_w > 0               ? nat_w : 100;
    g_image_ctx.edit_height = g_image_ctx.orig_height > 0 ? g_image_ctx.orig_height
                            : nat_h > 0               ? nat_h : 100;
    // Start proportional unless both dimensions were explicitly specified.
    g_image_ctx.proportional = !(g_image_ctx.orig_width > 0 && g_image_ctx.orig_height > 0);

    ImGui::OpenPopup("##img_ctx_menu");
  }

  // ── Step 2: render the context menu popup ────────────────────────────────────
  if(ImGui::BeginPopup("##img_ctx_menu"))
  {
    out.consumed_right_click = true;

    // Build the text that represents the image in the source note.
    const std::string copy_text = g_image_ctx.is_html
        ? g_image_ctx.html_tag
        : find_markdown_image_syntax(markdown, g_image_ctx.src, g_image_ctx.src_decoded);

    if(ImGui::MenuItem("Copy image text"))
      ImGui::SetClipboardText(copy_text.c_str());

    if(ImGui::MenuItem("Edit size..."))
      g_image_ctx.edit_size_pending = true;

    ImGui::EndPopup();
  }

  // ── Step 3: open the edit-size modal if requested ────────────────────────────
  if(g_image_ctx.edit_size_pending)
  {
    g_image_ctx.edit_size_pending = false;
    ImGui::OpenPopup("Edit Image Size##img_sz_modal");
  }

  // ── Step 4: render the edit-size modal ───────────────────────────────────────
  if(ImGui::BeginPopupModal("Edit Image Size##img_sz_modal", nullptr,
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
  {
    out.consumed_right_click = true;

    const float nat_w   = g_image_ctx.natural_size.x;
    const float nat_h   = g_image_ctx.natural_size.y;
    const float aspect  = (nat_w > 0.5f && nat_h > 0.5f) ? (nat_h / nat_w) : 1.0f;

    // Info row
    const std::string &src_label = g_image_ctx.src_decoded.empty()
        ? g_image_ctx.src : g_image_ctx.src_decoded;
    ImGui::TextUnformatted(src_label.c_str());
    if(nat_w > 0.5f && nat_h > 0.5f)
      ImGui::Text("Natural size: %d × %d px", (int)nat_w, (int)nat_h);
    ImGui::Separator();

    // Width field (always enabled)
    const bool w_changed = ImGui::InputInt("Width##img_edit_w",
        &g_image_ctx.edit_width, 1, 10);
    if(g_image_ctx.edit_width < 1) g_image_ctx.edit_width = 1;
    if(w_changed && g_image_ctx.proportional)
      g_image_ctx.edit_height = std::max(1, (int)(g_image_ctx.edit_width * aspect + 0.5f));

    // Height field — disabled when proportional, editable otherwise
    if(g_image_ctx.proportional)
    {
      ImGui::BeginDisabled(true);
      int disp_h = std::max(1, (int)(g_image_ctx.edit_width * aspect + 0.5f));
      ImGui::InputInt("Height##img_edit_h", &disp_h, 1, 10);
      ImGui::EndDisabled();
    }
    else
    {
      ImGui::InputInt("Height##img_edit_h", &g_image_ctx.edit_height, 1, 10);
      if(g_image_ctx.edit_height < 1) g_image_ctx.edit_height = 1;
    }

    // Proportional checkbox
    if(ImGui::Checkbox("Proportional##img_prop", &g_image_ctx.proportional))
    {
      if(g_image_ctx.proportional && nat_w > 0.5f)
        g_image_ctx.edit_height = std::max(1, (int)(g_image_ctx.edit_width * aspect + 0.5f));
    }
    if(g_image_ctx.proportional)
    {
      ImGui::SameLine();
      ImGui::TextDisabled("(width only)");
    }

    ImGui::Separator();

    // Apply / Cancel
    if(ImGui::Button("Apply##img_apply"))
    {
      const int fw = std::max(1, g_image_ctx.edit_width);
      const int fh = g_image_ctx.proportional
          ? std::max(1, (int)(fw * aspect + 0.5f))
          : std::max(1, g_image_ctx.edit_height);

      // Build the replacement HTML tag — always <img> regardless of original syntax.
      std::string new_tag;
      if(g_image_ctx.proportional)
        new_tag = "<img src=\"" + g_image_ctx.src + "\" width=\"" + std::to_string(fw) + "\">";
      else
        new_tag = "<img src=\"" + g_image_ctx.src + "\" width=\"" + std::to_string(fw)
                + "\" height=\"" + std::to_string(fh) + "\">";

      if(replace_image_in_text(markdown,
                               g_image_ctx.is_html,
                               g_image_ctx.html_tag,
                               g_image_ctx.src,
                               g_image_ctx.src_decoded,
                               new_tag))
        out.markdown_changed = true;

      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel##img_cancel"))
      ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
  }

  return out;
}

static void render_sections(const MdSection &s,
                            const std::function<void(std::string_view)> &render_body,
                            int &id_counter)
{
  // Render body before children (so text under a heading appears when expanded)
  // For root (level 0), we don't create a header; just render its body then children.
  if(s.level == 0)
  {
    if(!s.body.empty()) render_body(s.body);
    for(const auto &k : s.kids) render_sections(k, render_body, id_counter);
    return;
  }

  ImGui::PushID(id_counter++);

  // Collapsing header label (use the title only)
  const bool open = ImGui::CollapsingHeader(
      s.title.c_str(),
      ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

  if(open)
  {
    if(!s.body.empty()) render_body(s.body);
    for(const auto &k : s.kids) render_sections(k, render_body, id_counter);
  }

  ImGui::PopID();
}

static void render_inline_md_with_color_spans(std::string_view text)
{
  auto &md = renderer();
  auto render_code_chip = [&](std::string_view code, bool colored, ImVec4 color) {
    if(code.empty()) return;
    const ImVec2 pad(4.0f, 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(code.data(), code.data() + code.size());
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 sz(ts.x + pad.x * 2.0f, ts.y + pad.y * 2.0f);
    ImGui::Dummy(sz);

    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    bg.w = 1.0f;
    ImU32 bg_col = ImGui::GetColorU32(bg);
    ImU32 text_col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), bg_col, 3.0f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), text_col, code.data(), code.data() + code.size());
  };

  auto render_md_fragment = [&](std::string_view frag, bool colored, ImVec4 color) {
    if(frag.empty()) return;
    const std::string normalized = normalize_markdown_link_destinations(frag);
    ImGui::PushStyleColor(ImGuiCol_Button, markdown_link_color());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, markdown_link_hover_color());
    if(colored) ImGui::PushStyleColor(ImGuiCol_Text, color);
    md.print(normalized.data(), normalized.data() + normalized.size());
    if(colored) ImGui::PopStyleColor();
    ImGui::PopStyleColor(2);
  };
  struct InlineToken
  {
    std::string text;
    bool colored = false;
    ImVec4 color{};
    bool newline = false;
  };

  std::vector<InlineToken> tokens;
  for(auto &seg : split_color_spans(text))
  {
    size_t start = 0;
    while(start <= seg.text.size())
    {
      size_t nl = seg.text.find('\n', start);
      const bool has_nl = (nl != std::string::npos);
      const size_t end = has_nl ? nl : seg.text.size();
      if(end > start)
      {
        tokens.push_back(InlineToken{
            seg.text.substr(start, end - start),
            seg.colored,
            seg.color,
            false});
      }
      if(has_nl) tokens.push_back(InlineToken{"", false, ImVec4{}, true});
      if(!has_nl) break;
      start = nl + 1;
    }
  }

  for(size_t i = 0; i < tokens.size(); ++i)
  {
    const auto &t = tokens[i];
    if(t.newline)
    {
      MyMarkdown::compact_newline(4.0f);
      continue;
    }

    std::string_view token_text(t.text);
    size_t token_trimmed_start = 0;
    while(token_trimmed_start < token_text.size() && token_text[token_trimmed_start] == ' ') ++token_trimmed_start;
    size_t token_trimmed_end = token_text.size();
    while(token_trimmed_end > token_trimmed_start && token_text[token_trimmed_end - 1] == ' ') --token_trimmed_end;
    const int leading_spaces = (int)token_trimmed_start;
    const int trailing_spaces = (int)(token_text.size() - token_trimmed_end);
    token_text = token_text.substr(token_trimmed_start, token_trimmed_end - token_trimmed_start);

    size_t p = 0;
    bool emitted_inline = false;
    if(leading_spaces > 0)
    {
      const float sw = ImGui::CalcTextSize(" ").x;
      if(sw > 0.0f)
      {
        ImGui::Dummy(ImVec2(sw * (float)leading_spaces, 0.0f));
        emitted_inline = true;
        if(!token_text.empty()) ImGui::SameLine(0.0f, 0.0f);
      }
    }
    while(p < token_text.size())
    {
      size_t bq0 = token_text.find('`', p);
      if(bq0 == std::string::npos)
      {
        render_md_fragment(token_text.substr(p), t.colored, t.color);
        emitted_inline = true;
        break;
      }

      render_md_fragment(token_text.substr(p, bq0 - p), t.colored, t.color);
      emitted_inline = true;

      size_t bq1 = token_text.find('`', bq0 + 1);
      if(bq1 == std::string::npos)
      {
        render_md_fragment(token_text.substr(bq0), t.colored, t.color);
        emitted_inline = true;
        break;
      }

      ImGui::SameLine(0.0f, 0.0f);
      render_code_chip(token_text.substr(bq0 + 1, bq1 - bq0 - 1), t.colored, t.color);
      p = bq1 + 1;
      if(p < token_text.size()) ImGui::SameLine(0.0f, 0.0f);
    }

    const bool has_next = (i + 1 < tokens.size());
    if(has_next && !tokens[i + 1].newline)
    {
      if(emitted_inline) ImGui::SameLine(0.0f, 0.0f);
      if(trailing_spaces > 0)
      {
        const float sw = ImGui::CalcTextSize(" ").x;
        if(sw > 0.0f)
        {
          ImGui::Dummy(ImVec2(sw * (float)trailing_spaces, 0.0f));
          ImGui::SameLine(0.0f, 0.0f);
        }
      }
    }
  }
}

static std::vector<std::string> split_md_table_cells(std::string_view line)
{
  std::vector<std::string> cells;
  std::string_view t = trim(line);
  if(t.empty() || t.find('|') == std::string_view::npos) return cells;

  if(!t.empty() && t.front() == '|') t.remove_prefix(1);
  if(!t.empty() && t.back() == '|') t.remove_suffix(1);

  size_t start = 0;
  while(start <= t.size())
  {
    size_t sep = t.find('|', start);
    const size_t end = (sep == std::string_view::npos) ? t.size() : sep;
    cells.emplace_back(trim(t.substr(start, end - start)));
    if(sep == std::string_view::npos) break;
    start = sep + 1;
  }
  return cells;
}

static bool is_md_table_separator(std::string_view line, size_t expected_cols)
{
  const std::vector<std::string> parts = split_md_table_cells(line);
  if(parts.size() != expected_cols || parts.empty()) return false;

  for(const std::string &p : parts)
  {
    std::string_view s = trim(p);
    if(s.empty()) return false;
    if(s.front() == ':') s.remove_prefix(1);
    if(!s.empty() && s.back() == ':') s.remove_suffix(1);
    if(s.size() < 3) return false;
    for(char c : s)
    {
      if(c != '-') return false;
    }
  }
  return true;
}

static void render_md_table(const std::vector<std::string> &header, const std::vector<std::vector<std::string>> &rows, int table_id)
{
  if(header.empty()) return;
  const int cols = (int)header.size();
  const ImGuiTableFlags flags =
      ImGuiTableFlags_Borders |
      ImGuiTableFlags_RowBg |
      ImGuiTableFlags_SizingStretchSame |
      ImGuiTableFlags_NoHostExtendX;

  ImGui::PushID(table_id);
  ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.22f, 0.23f, 0.26f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1.0f, 1.0f, 1.0f, 0.04f));
  if(ImGui::BeginTable("##md_table", cols, flags))
  {
    for(int c = 0; c < cols; ++c) ImGui::TableSetupColumn(header[(size_t)c].c_str());
    ImGui::TableHeadersRow();

    for(const auto &r : rows)
    {
      ImGui::TableNextRow();
      for(int c = 0; c < cols; ++c)
      {
        ImGui::TableSetColumnIndex(c);
        if((size_t)c < r.size())
          render_inline_md_with_color_spans(r[(size_t)c]);
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopID();
}

static void render_markdown_block_with_tables(std::string_view text)
{
  auto &md = renderer();
  auto render_non_table = [&](std::string_view block) {
    if(block.empty()) return;
    if(block.find("[color=") == std::string_view::npos)
    {
      const std::string normalized = normalize_markdown_link_destinations(block);
      ImGui::PushStyleColor(ImGuiCol_Button, markdown_link_color());
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, markdown_link_hover_color());
      md.print(normalized.data(), normalized.data() + normalized.size());
      ImGui::PopStyleColor(2);
    }
    else
    {
      render_inline_md_with_color_spans(block);
    }
  };

  std::string normal_acc;
  int table_id = 0;
  int code_block_id = 0;

  size_t pos = 0;
  while(pos < text.size())
  {
    size_t line_end = text.find('\n', pos);
    const bool has_nl = (line_end != std::string_view::npos);
    if(!has_nl) line_end = text.size();
    std::string_view line = text.substr(pos, line_end - pos);
    const std::string_view trimmed_line = trim(line);

    if(starts_with(trimmed_line, "```"))
    {
      const std::string fence_info = std::string(trim(trimmed_line.substr(3)));
      size_t scan = has_nl ? (line_end + 1) : text.size();
      size_t block_end = text.size();
      std::string code;
      bool closed = false;

      while(scan < text.size())
      {
        size_t code_line_end = text.find('\n', scan);
        const bool code_has_nl = (code_line_end != std::string_view::npos);
        if(!code_has_nl) code_line_end = text.size();

        const std::string_view code_line = text.substr(scan, code_line_end - scan);
        if(trim(code_line) == "```")
        {
          block_end = code_has_nl ? (code_line_end + 1) : code_line_end;
          closed = true;
          break;
        }

        code.append(code_line.data(), code_line.size());
        if(code_has_nl) code.push_back('\n');
        scan = code_has_nl ? (code_line_end + 1) : code_line_end;
      }

      if(closed)
      {
        render_non_table(normal_acc);
        normal_acc.clear();
        MarkdownCodeHighlight::render_code_block(fence_info, code, 0x50000 + code_block_id++);
        pos = block_end;
        continue;
      }
    }

    const std::vector<std::string> header = split_md_table_cells(line);
    std::vector<std::vector<std::string>> rows;

    if(header.size() >= 2)
    {
      size_t sep_start = has_nl ? (line_end + 1) : text.size();
      if(sep_start < text.size())
      {
        size_t sep_end = text.find('\n', sep_start);
        const bool sep_has_nl = (sep_end != std::string_view::npos);
        if(!sep_has_nl) sep_end = text.size();
        std::string_view sep_line = text.substr(sep_start, sep_end - sep_start);
        if(is_md_table_separator(sep_line, header.size()))
        {
          size_t scan = sep_has_nl ? (sep_end + 1) : text.size();
          while(scan < text.size())
          {
            size_t row_end = text.find('\n', scan);
            const bool row_has_nl = (row_end != std::string_view::npos);
            if(!row_has_nl) row_end = text.size();
            std::string_view row_line = text.substr(scan, row_end - scan);
            const std::vector<std::string> row_cells = split_md_table_cells(row_line);
            if(row_cells.size() != header.size()) break;
            rows.push_back(row_cells);
            if(!row_has_nl)
            {
              scan = text.size();
              break;
            }
            scan = row_end + 1;
          }

          render_non_table(normal_acc);
          normal_acc.clear();
          render_md_table(header, rows, table_id++);

          pos = scan;
          continue;
        }
      }
    }

    normal_acc.append(line.data(), line.size());
    if(has_nl) normal_acc.push_back('\n');
    pos = has_nl ? (line_end + 1) : text.size();
  }

  render_non_table(normal_acc);
}

void MarkdownView::render_inline(std::string_view markdown_inline)
{
  render_inline_md_with_color_spans(markdown_inline);
}

void MarkdownView::render(std::string_view markdown)
{
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
  auto render_with_color_spans = [&](std::string_view text) { render_markdown_block_with_tables(text); };

  // Split into (normal markdown) and (note blocks). You already have this helper from earlier:
  // std::vector<Chunk> split_note_blocks(std::string_view)
  int note_idx = 0;
  for(const Chunk &c : split_note_blocks(markdown))
  {
    if(!c.is_note)
    {
      const MdSection root = parse_sections(c.text);
      int ids = 0;
      render_sections(root, render_with_color_spans, ids);
      continue;
    }

    // Render quote block with only the left accent line.
    ImGui::PushID(note_idx++);

    const ImVec2 pad(12.0f, 6.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);

    ImGui::BeginGroup();

    // Left padding inside the card
    ImGui::Dummy(ImVec2(pad.x, 0.0f));
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::BeginGroup();

    // Render markdown content (advances cursor)
    render_with_color_spans(c.text);

    ImGui::EndGroup();
    ImGui::EndGroup();

    ImGui::PopStyleVar(); // WindowPadding (pad)

    // Compute the rectangle that encloses the just-rendered groups
    ImVec2 rect_min = ImGui::GetItemRectMin();
    ImVec2 rect_max = ImGui::GetItemRectMax();

    auto *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        rect_min,
        ImVec2(rect_min.x + 5.0f, rect_max.y),
        ImGui::GetColorU32(ImGuiCol_ButtonHovered),
        2.0f);

    // Right click on note block -> copy only this note block text
    ImGui::SetCursorScreenPos(rect_min);
    ImGui::InvisibleButton("##note_block_ctx", ImVec2(rect_max.x - rect_min.x, rect_max.y - rect_min.y));
    if(ImGui::BeginPopupContextItem("##note_block_popup", ImGuiPopupFlags_MouseButtonRight))
    {
      if(ImGui::MenuItem("Copy note block"))
      {
        std::string plain;
        plain.reserve(c.text.size());
        size_t p = 0;
        while(p < c.text.size())
        {
          size_t e = c.text.find('\n', p);
          if(e == std::string::npos) e = c.text.size();
          std::string_view line(c.text.data() + p, e - p);
          std::string_view t = ltrim(line);
          if(starts_with(t, ">"))
          {
            t.remove_prefix(1);
            if(!t.empty() && t.front() == ' ') t.remove_prefix(1);
            plain.append(t.data(), t.size());
          }
          else
          {
            plain.append(line.data(), line.size());
          }
          if(e < c.text.size()) plain.push_back('\n');
          p = (e < c.text.size()) ? e + 1 : e;
        }
        ImGui::SetClipboardText(plain.c_str());
      }
      ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(ImVec2(rect_min.x, rect_max.y));

    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    ImGui::PopID();

    ImGui::Dummy(ImVec2(0.0f, 0.0f));
  }
  ImGui::PopStyleVar();
}
