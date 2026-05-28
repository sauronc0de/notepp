#include "markdown_ui.hpp"

#include "helpers.hpp"
#include "markdown_support.hpp"
#include "markdown_view.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace MarkdownUi
{

static std::filesystem::path g_widget_document_path;

void set_widget_document_path(std::filesystem::path path)
{
  g_widget_document_path = std::move(path);
}

namespace
{
using NoteCore::trim;

struct SourceSpan
{
  size_t start = 0;
  size_t end = 0;
};

enum class ValueKind
{
  Invalid,
  Number,
  String,
  Bool,
  Array,
  Object,
};

struct Value
{
  ValueKind kind = ValueKind::Invalid;
  double number = 0.0;
  bool is_integer = false;
  std::string str;
  bool boolean = false;
  std::vector<Value> array;
  std::vector<std::pair<std::string, Value>> object;
};

struct Statement
{
  enum class Kind
  {
    Declaration,
    Assignment,
    TextOutput,
    TextInput,
    IntInput,
    Slider,
    Checkbox,
    Enum,
    MultiCheck,
    List,
    Inventory,
    Button,
    Error,
  } kind = Kind::Error;

  std::string name;
  std::vector<std::string> args;
  std::string error;
  SourceSpan span;
  SourceSpan arg_span;
  size_t line_number = 0;
};

struct Row
{
  std::vector<Statement> statements;
  std::vector<std::string> conditions;
  size_t line_number = 0;
};

struct VariableDecl
{
  std::string name;
  std::string expr_source;
  SourceSpan expr_span;
  bool computed = false;
  size_t line_number = 0;
  std::string source_file;       // empty = local note; set = path to .globals.md that owns this decl
  size_t source_body_start = 0;  // byte offset of the UI block body within source_file
};

struct ParsedBlock
{
  std::vector<Row> rows;
  std::vector<std::string> errors;
  std::map<std::string, VariableDecl> declarations;
};

struct TextInputState
{
  std::string draft;
  bool editing = false;
};

bool is_widget_name(std::string_view name)
{
  return name == "text" ||
         name == "int" ||
         name == "slider" ||
         name == "checkbox" ||
         name == "enum" ||
         name == "multicheck" ||
         name == "list" ||
         name == "inventory" ||
         name == "button";
}

bool is_ident_start(char c)
{
  const unsigned char uc = static_cast<unsigned char>(c);
  return std::isalpha(uc) || c == '_';
}

bool is_ident_char(char c)
{
  const unsigned char uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '_';
}

bool parse_if_open_line(std::string_view line, std::string &condition_out)
{
  const std::string_view t = trim(line);
  if(t.size() < 5 || t.substr(0, 2) != "if") return false;

  size_t pos = 2;
  while(pos < t.size() && (t[pos] == ' ' || t[pos] == '\t')) ++pos;
  if(pos >= t.size() || t[pos] != '(') return false;

  int depth = 0;
  bool in_string = false;
  bool escape = false;
  size_t close = std::string_view::npos;
  for(size_t i = pos; i < t.size(); ++i)
  {
    const char c = t[i];
    if(in_string)
    {
      if(escape)
        escape = false;
      else if(c == '\\')
        escape = true;
      else if(c == '"')
        in_string = false;
      continue;
    }
    if(c == '"')
    {
      in_string = true;
      continue;
    }
    if(c == '(')
      ++depth;
    else if(c == ')')
    {
      --depth;
      if(depth == 0)
      {
        close = i;
        break;
      }
    }
  }

  if(close == std::string_view::npos) return false;
  size_t tail = close + 1;
  while(tail < t.size() && (t[tail] == ' ' || t[tail] == '\t')) ++tail;
  if(tail >= t.size() || t[tail] != '{') return false;
  ++tail;
  while(tail < t.size() && (t[tail] == ' ' || t[tail] == '\t')) ++tail;
  if(tail != t.size()) return false;

  condition_out = std::string(trim(t.substr(pos + 1, close - pos - 1)));
  return !condition_out.empty();
}

bool is_if_close_line(std::string_view line)
{
  return trim(line) == "}";
}

struct StyledLabel
{
  std::string text;
  bool has_color = false;
  ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
};

bool parse_hex_color_text(std::string_view text, ImVec4 &out)
{
  if(text.size() != 7 && text.size() != 9) return false;
  if(text.front() != '#') return false;

  auto hex = [](char c) -> int {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  auto byte = [&](size_t i) -> int {
    const int hi = hex(text[i]);
    const int lo = hex(text[i + 1]);
    if(hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
  };

  const int r = byte(1);
  const int g = byte(3);
  const int b = byte(5);
  if(r < 0 || g < 0 || b < 0) return false;
  int a = 255;
  if(text.size() == 9)
  {
    a = byte(7);
    if(a < 0) return false;
  }

  out = ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
  return true;
}

StyledLabel make_styled_label(std::string text)
{
  StyledLabel styled;
  const std::string_view prefix = "[color=";
  const std::string_view suffix = "[/color]";
  const std::string_view view(text);
  if(view.substr(0, prefix.size()) == prefix && view.size() > prefix.size() + suffix.size())
  {
    const size_t close = view.find(']', prefix.size());
    if(close != std::string_view::npos && view.size() >= close + 1 + suffix.size())
    {
      const std::string_view tail = view.substr(view.size() - suffix.size());
      if(tail == suffix)
      {
        ImVec4 color;
        if(parse_hex_color_text(view.substr(prefix.size(), close - prefix.size()), color))
        {
          styled.text = std::string(view.substr(close + 1, view.size() - (close + 1) - suffix.size()));
          styled.has_color = true;
          styled.color = color;
          return styled;
        }
      }
    }
  }
  styled.text = std::move(text);
  return styled;
}

void render_styled_label(const StyledLabel &label)
{
  if(label.text.empty()) return;
  if(label.has_color)
  {
    ImGui::PushStyleColor(ImGuiCol_Text, label.color);
    MarkdownView::render_inline(label.text);
    ImGui::PopStyleColor();
    return;
  }
  MarkdownView::render_inline(label.text);
}

std::string make_statement_token(const Statement &stmt)
{
  std::string key = std::to_string(static_cast<int>(stmt.kind));
  key.push_back('|');
  key += stmt.name;
  key.push_back('|');
  key += std::to_string(stmt.line_number);
  for(const std::string &arg : stmt.args)
  {
    key.push_back('|');
    key += arg;
  }
  if(!stmt.error.empty())
  {
    key.push_back('|');
    key += stmt.error;
  }
  return std::to_string(std::hash<std::string>{}(key));
}

std::string make_hidden_widget_id(const char *prefix, const Statement &stmt)
{
  return std::string("##") + prefix + "_" + make_statement_token(stmt);
}

std::string format_number(double v, bool prefer_integer)
{
  if(prefer_integer || std::fabs(v - std::round(v)) < 1e-6)
  {
    return std::to_string(static_cast<long long>(std::llround(v)));
  }

  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(6);
  oss << v;
  std::string s = oss.str();
  while(s.size() > 1 && s.back() == '0') s.pop_back();
  if(!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

bool is_scalar_value(const Value &value)
{
  return value.kind == ValueKind::Number ||
         value.kind == ValueKind::String ||
         value.kind == ValueKind::Bool;
}

const Value *find_object_field(const Value &value, std::string_view key)
{
  if(value.kind != ValueKind::Object) return nullptr;
  for(const auto &[field_name, field_value] : value.object)
  {
    if(field_name == key) return &field_value;
  }
  return nullptr;
}

Value *find_object_field(Value &value, std::string_view key)
{
  if(value.kind != ValueKind::Object) return nullptr;
  for(auto &[field_name, field_value] : value.object)
  {
    if(field_name == key) return &field_value;
  }
  return nullptr;
}

void upsert_object_field(Value &value, std::string key, Value field_value)
{
  if(value.kind != ValueKind::Object)
  {
    value.kind = ValueKind::Object;
    value.object.clear();
  }
  for(auto &[field_name, current_value] : value.object)
  {
    if(field_name == key)
    {
      current_value = std::move(field_value);
      return;
    }
  }
  value.object.emplace_back(std::move(key), std::move(field_value));
}

std::string escape_string(std::string_view s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for(char c : s)
  {
    switch(c)
    {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

std::string serialize_object_key(std::string_view key)
{
  if(!key.empty() && is_ident_start(key.front()))
  {
    bool valid = true;
    for(char c : key)
    {
      if(!is_ident_char(c))
      {
        valid = false;
        break;
      }
    }
    if(valid) return std::string(key);
  }
  return std::string{"\""} + escape_string(key) + "\"";
}

std::string serialize_value(const Value &value)
{
  switch(value.kind)
  {
  case ValueKind::Number:
    return format_number(value.number, value.is_integer);
  case ValueKind::String:
    return std::string{"\""} + escape_string(value.str) + "\"";
  case ValueKind::Bool:
    return value.boolean ? "true" : "false";
  case ValueKind::Array: {
    std::string out = "[";
    for(size_t i = 0; i < value.array.size(); ++i)
    {
      if(i != 0) out += ", ";
      out += serialize_value(value.array[i]);
    }
    out += "]";
    return out;
  }
  case ValueKind::Object: {
    std::string out = "{";
    for(size_t i = 0; i < value.object.size(); ++i)
    {
      if(i != 0) out += ", ";
      out += serialize_object_key(value.object[i].first);
      out += ":";
      out += serialize_value(value.object[i].second);
    }
    out += "}";
    return out;
  }
  default:
    return "null";
  }
}

std::string display_value(const Value &value)
{
  switch(value.kind)
  {
  case ValueKind::Number:
    return format_number(value.number, value.is_integer);
  case ValueKind::String:
    return value.str;
  case ValueKind::Bool:
    return value.boolean ? "true" : "false";
  case ValueKind::Array: {
    const bool all_scalars = std::all_of(value.array.begin(), value.array.end(), [](const Value &item) {
      return is_scalar_value(item);
    });
    if(!all_scalars) return serialize_value(value);

    std::string out;
    for(size_t i = 0; i < value.array.size(); ++i)
    {
      if(i != 0) out += ", ";
      out += display_value(value.array[i]);
    }
    return out;
  }
  case ValueKind::Object:
    return serialize_value(value);
  default:
    return "<invalid>";
  }
}

bool is_true(const Value &value)
{
  if(value.kind == ValueKind::Bool) return value.boolean;
  if(value.kind == ValueKind::Number) return std::fabs(value.number) > 1e-9;
  if(value.kind == ValueKind::String) return !value.str.empty();
  if(value.kind == ValueKind::Array) return !value.array.empty();
  if(value.kind == ValueKind::Object) return !value.object.empty();
  return false;
}

std::filesystem::path resolve_widget_image_path(std::string_view raw_path)
{
  if(raw_path.empty()) return {};

  std::filesystem::path path(raw_path);
  if(path.is_absolute() && std::filesystem::exists(path)) return path;

  if(!g_widget_document_path.empty())
  {
    const std::filesystem::path doc_candidate = g_widget_document_path.parent_path() / path;
    if(std::filesystem::exists(doc_candidate)) return std::filesystem::absolute(doc_candidate);
  }

  if(std::filesystem::exists(path)) return std::filesystem::absolute(path);

  const std::filesystem::path assets_root = std::filesystem::path(ASSETS_PATH);
  const std::filesystem::path asset_candidate = assets_root / path;
  if(std::filesystem::exists(asset_candidate)) return asset_candidate;

  const std::filesystem::path icon_candidate = assets_root / "icons" / path;
  if(std::filesystem::exists(icon_candidate)) return icon_candidate;

  return {};
}

ImTextureID get_widget_image_texture(std::string_view raw_path)
{
  static std::unordered_map<std::string, GLuint> texture_cache;

  const std::filesystem::path resolved = resolve_widget_image_path(raw_path);
  if(resolved.empty()) return static_cast<ImTextureID>(0);

  const std::string key = resolved.string();
  if(const auto it = texture_cache.find(key); it != texture_cache.end())
  {
    return (ImTextureID)(uintptr_t)it->second;
  }

  static const bool img_ready = []() {
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    return true;
  }();
  (void)img_ready;

  SDL_Surface *loaded = IMG_Load(key.c_str());
  if(!loaded)
  {
    texture_cache.emplace(key, 0);
    return static_cast<ImTextureID>(0);
  }

  SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(loaded);
  if(!rgba)
  {
    texture_cache.emplace(key, 0);
    return static_cast<ImTextureID>(0);
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  if(tex == 0)
  {
    SDL_FreeSurface(rgba);
    texture_cache.emplace(key, 0);
    return static_cast<ImTextureID>(0);
  }

  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
  SDL_FreeSurface(rgba);

  texture_cache.emplace(key, tex);
  return (ImTextureID)(uintptr_t)tex;
}

struct ExprResult
{
  Value value;
  std::string error;
};

struct EvalContext;

class ExprParser
{
public:
  ExprParser(std::string_view source, EvalContext &ctx)
      : source_(source), ctx_(ctx) {}

  ExprResult parse_expression();
  void skip_ws();
  bool eof() const;

private:
  ExprResult parse_logical_or();
  ExprResult parse_logical_and();
  ExprResult parse_comparison();
  ExprResult parse_additive();
  ExprResult parse_multiplicative();
  ExprResult parse_unary();
  ExprResult parse_primary();
  ExprResult parse_call(const std::string &name);
  ExprResult parse_string_literal();
  ExprResult parse_array();
  ExprResult parse_object();
  bool consume(char c);
  static ExprResult call_builtin(const std::string &name, const std::vector<Value> &args);
  char peek() const;
  static ExprResult combine_numeric(const ExprResult &lhs, const ExprResult &rhs, char op);

  std::string_view source_;
  size_t pos_ = 0;
  EvalContext &ctx_;
};

struct EvalContext
{
  const ParsedBlock &block;
  bool preview_state_changed = false;
  bool consumed_right_click = false;
  std::unordered_map<std::string, Value> overrides;
  std::unordered_map<std::string, Value> cache;
  std::unordered_map<std::string, std::string> cache_errors;
  std::set<std::string> visiting;
  const std::map<std::string, VariableDecl> *global_declarations = nullptr;
  std::unordered_map<std::string, std::string> global_replacements;

  ExprResult evaluate(std::string_view expr)
  {
    ExprParser parser(expr, *this);
    ExprResult result = parser.parse_expression();
    if(!result.error.empty()) return result;
    parser.skip_ws();
    if(!parser.eof()) return {{}, "unexpected trailing input"};
    return result;
  }

  ExprResult resolve_variable(const std::string &name)
  {
    if(const auto it = overrides.find(name); it != overrides.end()) return {it->second, {}};
    if(const auto it = cache.find(name); it != cache.end()) return {it->second, {}};
    if(const auto it = cache_errors.find(name); it != cache_errors.end()) return {{}, it->second};

    const auto decl_it = block.declarations.find(name);
    const bool in_local = decl_it != block.declarations.end();
    const auto *decl = in_local ? &decl_it->second : nullptr;

    if(!decl && global_declarations)
    {
      const auto git = global_declarations->find(name);
      if(git != global_declarations->end()) decl = &git->second;
    }

    if(!decl) return {{}, "unknown variable '" + name + "'"};
    if(visiting.count(name) != 0) return {{}, "circular dependency involving '" + name + "'"};

    visiting.insert(name);
    ExprResult result = evaluate(decl->expr_source);
    visiting.erase(name);
    if(result.error.empty())
      cache[name] = result.value;
    else
      cache_errors[name] = result.error;
    return result;
  }
};

void ExprParser::skip_ws()
{
  while(pos_ < source_.size() && (source_[pos_] == ' ' || source_[pos_] == '\t' || source_[pos_] == '\r' || source_[pos_] == '\n')) ++pos_;
}

bool ExprParser::consume(char c)
{
  skip_ws();
  if(pos_ < source_.size() && source_[pos_] == c)
  {
    ++pos_;
    return true;
  }
  return false;
}

bool ExprParser::eof() const
{
  return pos_ >= source_.size();
}

char ExprParser::peek() const
{
  return pos_ < source_.size() ? source_[pos_] : '\0';
}

ExprResult ExprParser::combine_numeric(const ExprResult &lhs, const ExprResult &rhs, char op)
{
  if(!lhs.error.empty()) return lhs;
  if(!rhs.error.empty()) return rhs;
  if(lhs.value.kind != ValueKind::Number || rhs.value.kind != ValueKind::Number)
    return {{}, "numeric expression expected"};

  Value out;
  out.kind = ValueKind::Number;
  out.is_integer = lhs.value.is_integer && rhs.value.is_integer && op != '/';
  switch(op)
  {
  case '+':
    out.number = lhs.value.number + rhs.value.number;
    break;
  case '-':
    out.number = lhs.value.number - rhs.value.number;
    break;
  case '*':
    out.number = lhs.value.number * rhs.value.number;
    break;
  case '/':
    if(std::fabs(rhs.value.number) < 1e-9) return {{}, "division by zero"};
    out.number = lhs.value.number / rhs.value.number;
    out.is_integer = false;
    break;
  default:
    return {{}, "unsupported operator"};
  }
  return {out, {}};
}

ExprResult ExprParser::parse_expression()
{
  return parse_logical_or();
}

ExprResult ExprParser::parse_logical_or()
{
  ExprResult lhs = parse_logical_and();
  if(!lhs.error.empty()) return lhs;
  while(true)
  {
    skip_ws();
    if(pos_ + 1 >= source_.size() || source_[pos_] != '|' || source_[pos_ + 1] != '|') break;
    pos_ += 2;
    ExprResult rhs = parse_logical_and();
    if(!rhs.error.empty()) return rhs;
    Value result;
    result.kind = ValueKind::Bool;
    result.boolean = is_true(lhs.value) || is_true(rhs.value);
    lhs = {result, {}};
  }
  return lhs;
}

ExprResult ExprParser::parse_logical_and()
{
  ExprResult lhs = parse_comparison();
  if(!lhs.error.empty()) return lhs;
  while(true)
  {
    skip_ws();
    if(pos_ + 1 >= source_.size() || source_[pos_] != '&' || source_[pos_ + 1] != '&') break;
    pos_ += 2;
    ExprResult rhs = parse_comparison();
    if(!rhs.error.empty()) return rhs;
    Value result;
    result.kind = ValueKind::Bool;
    result.boolean = is_true(lhs.value) && is_true(rhs.value);
    lhs = {result, {}};
  }
  return lhs;
}

ExprResult ExprParser::parse_comparison()
{
  ExprResult lhs = parse_additive();
  if(!lhs.error.empty()) return lhs;
  skip_ws();
  if(eof()) return lhs;

  const char c0 = source_[pos_];
  const char c1 = (pos_ + 1 < source_.size()) ? source_[pos_ + 1] : '\0';

  enum class CmpOp { Gt, Lt, Ge, Le, Eq, Ne, None };
  CmpOp op = CmpOp::None;
  if     (c0 == '>' && c1 == '=') { op = CmpOp::Ge; pos_ += 2; }
  else if(c0 == '<' && c1 == '=') { op = CmpOp::Le; pos_ += 2; }
  else if(c0 == '=' && c1 == '=') { op = CmpOp::Eq; pos_ += 2; }
  else if(c0 == '!' && c1 == '=') { op = CmpOp::Ne; pos_ += 2; }
  else if(c0 == '>')              { op = CmpOp::Gt; pos_ += 1; }
  else if(c0 == '<')              { op = CmpOp::Lt; pos_ += 1; }

  if(op == CmpOp::None) return lhs;

  ExprResult rhs = parse_additive();
  if(!rhs.error.empty()) return rhs;

  Value result;
  result.kind = ValueKind::Bool;

  if(op == CmpOp::Eq || op == CmpOp::Ne)
  {
    bool eq;
    if(lhs.value.kind == ValueKind::Number && rhs.value.kind == ValueKind::Number)
      eq = std::fabs(lhs.value.number - rhs.value.number) < 1e-9;
    else if(lhs.value.kind == ValueKind::String && rhs.value.kind == ValueKind::String)
      eq = lhs.value.str == rhs.value.str;
    else if(lhs.value.kind == ValueKind::Bool && rhs.value.kind == ValueKind::Bool)
      eq = lhs.value.boolean == rhs.value.boolean;
    else
      eq = false;
    result.boolean = (op == CmpOp::Eq) ? eq : !eq;
  }
  else
  {
    if(lhs.value.kind != ValueKind::Number || rhs.value.kind != ValueKind::Number)
      return {{}, "comparison requires numeric operands"};
    switch(op)
    {
    case CmpOp::Gt: result.boolean = lhs.value.number >  rhs.value.number; break;
    case CmpOp::Lt: result.boolean = lhs.value.number <  rhs.value.number; break;
    case CmpOp::Ge: result.boolean = lhs.value.number >= rhs.value.number; break;
    case CmpOp::Le: result.boolean = lhs.value.number <= rhs.value.number; break;
    default: break;
    }
  }
  return {result, {}};
}

ExprResult ExprParser::parse_additive()
{
  ExprResult lhs = parse_multiplicative();
  while(true)
  {
    skip_ws();
    if(eof() || (peek() != '+' && peek() != '-')) break;
    const char op = source_[pos_++];
    ExprResult rhs = parse_multiplicative();
    lhs = combine_numeric(lhs, rhs, op);
  }
  return lhs;
}

ExprResult ExprParser::parse_multiplicative()
{
  ExprResult lhs = parse_unary();
  while(true)
  {
    skip_ws();
    if(eof() || (peek() != '*' && peek() != '/')) break;
    const char op = source_[pos_++];
    ExprResult rhs = parse_unary();
    lhs = combine_numeric(lhs, rhs, op);
  }
  return lhs;
}

ExprResult ExprParser::parse_unary()
{
  skip_ws();
  if(consume('+')) return parse_unary();
  if(consume('-'))
  {
    ExprResult inner = parse_unary();
    if(!inner.error.empty()) return inner;
    if(inner.value.kind != ValueKind::Number) return {{}, "numeric expression expected"};
    inner.value.number = -inner.value.number;
    return inner;
  }
  // Logical not: '!' not followed by '=' (which would be the != operator)
  if(peek() == '!' && (pos_ + 1 >= source_.size() || source_[pos_ + 1] != '='))
  {
    ++pos_;
    ExprResult inner = parse_unary();
    if(!inner.error.empty()) return inner;
    Value result;
    result.kind = ValueKind::Bool;
    result.boolean = !is_true(inner.value);
    return {result, {}};
  }
  return parse_primary();
}

ExprResult ExprParser::parse_call(const std::string &name)
{
  ++pos_; // consume '('
  std::vector<Value> args;
  skip_ws();
  if(peek() != ')')
  {
    while(true)
    {
      ExprResult arg = parse_expression();
      if(!arg.error.empty()) return arg;
      args.push_back(std::move(arg.value));
      skip_ws();
      if(peek() == ')') break;
      if(!consume(',')) return {{}, "expected ',' or ')' in function call"};
    }
  }
  if(!consume(')')) return {{}, "expected ')'"};
  return call_builtin(name, args);
}

ExprResult ExprParser::call_builtin(const std::string &name, const std::vector<Value> &args)
{
  if(name == "len")
  {
    if(args.size() != 1) return {{}, "len() expects 1 argument"};
    Value result;
    result.kind = ValueKind::Number;
    result.is_integer = true;
    if(args[0].kind == ValueKind::String)
      result.number = static_cast<double>(args[0].str.size());
    else if(args[0].kind == ValueKind::Array)
      result.number = static_cast<double>(args[0].array.size());
    else
      return {{}, "len() expects a string or array"};
    return {result, {}};
  }
  if(name == "contains")
  {
    if(args.size() != 2) return {{}, "contains() expects 2 arguments"};
    Value result;
    result.kind = ValueKind::Bool;
    if(args[0].kind == ValueKind::String && args[1].kind == ValueKind::String)
    {
      result.boolean = args[0].str.find(args[1].str) != std::string::npos;
    }
    else if(args[0].kind == ValueKind::Array)
    {
      result.boolean = false;
      for(const Value &v : args[0].array)
      {
        if(v.kind == ValueKind::String && args[1].kind == ValueKind::String && v.str == args[1].str)
          { result.boolean = true; break; }
        if(v.kind == ValueKind::Number && args[1].kind == ValueKind::Number && std::fabs(v.number - args[1].number) < 1e-9)
          { result.boolean = true; break; }
        if(v.kind == ValueKind::Bool && args[1].kind == ValueKind::Bool && v.boolean == args[1].boolean)
          { result.boolean = true; break; }
      }
    }
    else
      return {{}, "contains() expects (string, string) or (array, value)"};
    return {result, {}};
  }
  if(name == "starts_with")
  {
    if(args.size() != 2 || args[0].kind != ValueKind::String || args[1].kind != ValueKind::String)
      return {{}, "starts_with() expects two string arguments"};
    Value result;
    result.kind = ValueKind::Bool;
    result.boolean = args[0].str.starts_with(args[1].str);
    return {result, {}};
  }
  if(name == "ends_with")
  {
    if(args.size() != 2 || args[0].kind != ValueKind::String || args[1].kind != ValueKind::String)
      return {{}, "ends_with() expects two string arguments"};
    Value result;
    result.kind = ValueKind::Bool;
    result.boolean = args[0].str.ends_with(args[1].str);
    return {result, {}};
  }
  if(name == "empty")
  {
    if(args.size() != 1) return {{}, "empty() expects 1 argument"};
    Value result;
    result.kind = ValueKind::Bool;
    result.boolean = !is_true(args[0]);
    return {result, {}};
  }
  return {{}, "unknown function '" + name + "'"};
}

ExprResult ExprParser::parse_string_literal()
{
  if(peek() != '"') return {{}, "expected string literal"};

  ++pos_;
  std::string out;
  while(pos_ < source_.size())
  {
    const char c = source_[pos_++];
    if(c == '"')
    {
      Value value;
      value.kind = ValueKind::String;
      value.str = std::move(out);
      return {value, {}};
    }
    if(c == '\\' && pos_ < source_.size())
    {
      const char esc = source_[pos_++];
      switch(esc)
      {
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      case '\\':
        out.push_back('\\');
        break;
      case '"':
        out.push_back('"');
        break;
      default:
        out.push_back(esc);
        break;
      }
    }
    else
    {
      out.push_back(c);
    }
  }
  return {{}, "unterminated string literal"};
}

ExprResult ExprParser::parse_array()
{
  if(!consume('[')) return {{}, "expected '['"};
  Value out;
  out.kind = ValueKind::Array;
  skip_ws();
  if(consume(']')) return {out, {}};

  while(true)
  {
    ExprResult item = parse_expression();
    if(!item.error.empty()) return item;
    out.array.push_back(item.value);

    skip_ws();
    if(consume(']')) break;
    if(!consume(',')) return {{}, "expected ',' or ']'"};
  }
  return {out, {}};
}

ExprResult ExprParser::parse_object()
{
  if(!consume('{')) return {{}, "expected '{'"};
  Value out;
  out.kind = ValueKind::Object;
  skip_ws();
  if(consume('}')) return {out, {}};

  while(true)
  {
    skip_ws();
    std::string key;
    if(peek() == '"')
    {
      ExprResult key_result = parse_string_literal();
      if(!key_result.error.empty()) return key_result;
      key = std::move(key_result.value.str);
    }
    else if(is_ident_start(peek()))
    {
      const size_t ident_start = pos_;
      ++pos_;
      while(pos_ < source_.size() && is_ident_char(source_[pos_])) ++pos_;
      key = std::string(source_.substr(ident_start, pos_ - ident_start));
    }
    else
    {
      return {{}, "expected object key"};
    }

    if(!consume(':')) return {{}, "expected ':'"};
    ExprResult item = parse_expression();
    if(!item.error.empty()) return item;
    upsert_object_field(out, key, item.value);

    skip_ws();
    if(consume('}')) break;
    if(!consume(',')) return {{}, "expected ',' or '}'"};
  }
  return {out, {}};
}

ExprResult ExprParser::parse_primary()
{
  skip_ws();
  if(eof()) return {{}, "unexpected end of expression"};

  if(peek() == '(')
  {
    ++pos_;
    ExprResult inner = parse_expression();
    if(!inner.error.empty()) return inner;
    if(!consume(')')) return {{}, "expected ')'"};
    return inner;
  }

  if(peek() == '[') return parse_array();
  if(peek() == '{') return parse_object();
  if(peek() == '"') return parse_string_literal();

  if(std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.')
  {
    const size_t start = pos_;
    bool saw_dot = false;
    while(pos_ < source_.size())
    {
      const char c = source_[pos_];
      if(std::isdigit(static_cast<unsigned char>(c)))
      {
        ++pos_;
        continue;
      }
      if(c == '.' && !saw_dot)
      {
        saw_dot = true;
        ++pos_;
        continue;
      }
      break;
    }
    const std::string number_text(source_.substr(start, pos_ - start));
    Value value;
    value.kind = ValueKind::Number;
    value.is_integer = !saw_dot;
    value.number = std::strtod(number_text.c_str(), nullptr);
    return {value, {}};
  }

  if(is_ident_start(peek()))
  {
    const size_t start = pos_;
    ++pos_;
    while(pos_ < source_.size() && is_ident_char(source_[pos_])) ++pos_;
    const std::string ident(source_.substr(start, pos_ - start));
    if(ident == "true" || ident == "false")
    {
      Value value;
      value.kind = ValueKind::Bool;
      value.boolean = ident == "true";
      return {value, {}};
    }
    skip_ws();
    if(peek() == '(') return parse_call(ident);
    ExprResult result = ctx_.resolve_variable(ident);
    if(!result.error.empty()) return result;
    while(true)
    {
      skip_ws();
      if(peek() != '[') break;
      ++pos_;
      ExprResult idx = parse_expression();
      if(!idx.error.empty()) return idx;
      if(!consume(']')) return {{}, "expected ']' after array index"};
      if(result.value.kind != ValueKind::Array)
        return {{}, "cannot index into non-array value"};
      if(idx.value.kind != ValueKind::Number || !idx.value.is_integer)
        return {{}, "array index must be an integer"};
      const int i = static_cast<int>(idx.value.number);
      const int sz = static_cast<int>(result.value.array.size());
      const int actual = i < 0 ? sz + i : i;
      if(actual < 0 || actual >= sz)
        return {{}, "array index out of bounds"};
      result = {result.value.array[static_cast<size_t>(actual)], {}};
    }
    return result;
  }

  return {{}, "unexpected token in expression"};
}

std::vector<std::string> split_top_level_args(std::string_view args_text)
{
  std::vector<std::string> args;
  std::string current;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  bool in_string = false;
  bool escape = false;

  for(char c : args_text)
  {
    if(in_string)
    {
      current.push_back(c);
      if(escape)
        escape = false;
      else if(c == '\\')
        escape = true;
      else if(c == '"')
        in_string = false;
      continue;
    }

    if(c == '"')
    {
      in_string = true;
      current.push_back(c);
      continue;
    }
    if(c == '(')
      ++paren_depth;
    else if(c == ')')
      --paren_depth;
    else if(c == '[')
      ++bracket_depth;
    else if(c == ']')
      --bracket_depth;
    else if(c == '{')
      ++brace_depth;
    else if(c == '}')
      --brace_depth;

    if(c == ',' && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0)
    {
      args.push_back(std::string(trim(current)));
      current.clear();
      continue;
    }
    current.push_back(c);
  }

  if(!trim(current).empty() || !args.empty()) args.push_back(std::string(trim(current)));
  return args;
}

bool parse_statement_line(std::string_view line, size_t line_offset, size_t line_number, Row &row)
{
  size_t pos = 0;
  while(pos < line.size())
  {
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if(pos >= line.size()) break;

    Statement stmt;
    stmt.line_number = line_number;
    if(!is_ident_start(line[pos]))
    {
      stmt.kind = Statement::Kind::Error;
      stmt.error = "expected statement name";
      stmt.span = {line_offset + pos, line_offset + pos + 1};
      row.statements.push_back(std::move(stmt));
      return false;
    }

    const size_t name_start = pos;
    ++pos;
    while(pos < line.size() && is_ident_char(line[pos])) ++pos;
    stmt.name = std::string(line.substr(name_start, pos - name_start));

    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if(pos < line.size() && line[pos] != '(')
    {
      char compound_op = '\0';
      size_t op_len = 0;
      if(pos + 1 < line.size() && line[pos + 1] == '=')
      {
        if(line[pos] == '+') { compound_op = '+'; op_len = 2; }
        else if(line[pos] == '-') { compound_op = '-'; op_len = 2; }
        else if(line[pos] == '*') { compound_op = '*'; op_len = 2; }
        else if(line[pos] == '/') { compound_op = '/'; op_len = 2; }
      }
      if(op_len == 0 && line[pos] == '=' && (pos + 1 >= line.size() || line[pos + 1] != '='))
        op_len = 1;

      if(op_len > 0)
      {
        const std::string rhs_raw(trim(line.substr(pos + op_len)));
        stmt.kind = Statement::Kind::Assignment;
        stmt.span = {line_offset + name_start, line_offset + line.size()};
        if(rhs_raw.empty())
          stmt.error = "expected expression after assignment operator";
        else if(compound_op != '\0')
          stmt.args.push_back(stmt.name + " " + compound_op + " (" + rhs_raw + ")");
        else
          stmt.args.push_back(rhs_raw);
        row.statements.push_back(std::move(stmt));
        pos = line.size();
        continue;
      }

      stmt.kind = Statement::Kind::Error;
      stmt.error = "expected '('";
      stmt.span = {line_offset + name_start, line_offset + pos};
      row.statements.push_back(std::move(stmt));
      return false;
    }
    if(pos >= line.size())
    {
      stmt.kind = Statement::Kind::Error;
      stmt.error = "expected '('";
      stmt.span = {line_offset + name_start, line_offset + pos};
      row.statements.push_back(std::move(stmt));
      return false;
    }

    const size_t open = pos;
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    size_t close = std::string_view::npos;
    for(size_t i = pos; i < line.size(); ++i)
    {
      const char c = line[i];
      if(in_string)
      {
        if(escape)
          escape = false;
        else if(c == '\\')
          escape = true;
        else if(c == '"')
          in_string = false;
        continue;
      }
      if(c == '"')
      {
        in_string = true;
        continue;
      }
      if(c == '(')
        ++depth;
      else if(c == ')')
      {
        --depth;
        if(depth == 0)
        {
          close = i;
          break;
        }
      }
    }

    if(close == std::string_view::npos)
    {
      stmt.kind = Statement::Kind::Error;
      stmt.error = "unterminated statement";
      stmt.span = {line_offset + name_start, line_offset + line.size()};
      row.statements.push_back(std::move(stmt));
      return false;
    }

    stmt.args = split_top_level_args(line.substr(open + 1, close - open - 1));
    stmt.span = {line_offset + name_start, line_offset + close + 1};
    stmt.arg_span = {line_offset + open + 1, line_offset + close};
    if(is_widget_name(stmt.name))
    {
      if(stmt.name == "text")
        stmt.kind = stmt.args.size() >= 2 ? Statement::Kind::TextInput : Statement::Kind::TextOutput;
      else if(stmt.name == "int")
        stmt.kind = Statement::Kind::IntInput;
      else if(stmt.name == "slider")
        stmt.kind = Statement::Kind::Slider;
      else if(stmt.name == "checkbox")
        stmt.kind = Statement::Kind::Checkbox;
      else if(stmt.name == "enum")
        stmt.kind = Statement::Kind::Enum;
      else if(stmt.name == "multicheck")
        stmt.kind = Statement::Kind::MultiCheck;
      else if(stmt.name == "list")
        stmt.kind = Statement::Kind::List;
      else if(stmt.name == "inventory")
        stmt.kind = Statement::Kind::Inventory;
      else if(stmt.name == "button")
        stmt.kind = Statement::Kind::Button;
    }
    else
    {
      stmt.kind = Statement::Kind::Declaration;
      if(stmt.args.size() != 1) stmt.error = "variable declarations take exactly one argument";
    }

    row.statements.push_back(std::move(stmt));
    pos = close + 1;
  }
  return true;
}

bool parse_literal_expr(std::string_view expr, Value &out)
{
  ParsedBlock empty_block;
  EvalContext ctx{empty_block};
  ExprParser parser(expr, ctx);
  ExprResult result = parser.parse_expression();
  parser.skip_ws();
  if(!result.error.empty() || !parser.eof()) return false;
  if(result.value.kind == ValueKind::Invalid) return false;
  out = result.value;
  return true;
}

bool statement_needs_more_input(std::string_view text)
{
  int paren_depth = 0;
  bool saw_open = false;
  bool in_string = false;
  bool escape = false;

  for(char c : text)
  {
    if(in_string)
    {
      if(escape)
        escape = false;
      else if(c == '\\')
        escape = true;
      else if(c == '"')
        in_string = false;
      continue;
    }

    if(c == '"')
    {
      in_string = true;
      continue;
    }
    if(c == '(')
    {
      saw_open = true;
      ++paren_depth;
    }
    else if(c == ')' && paren_depth > 0)
    {
      --paren_depth;
    }
  }

  return saw_open && paren_depth > 0;
}

ParsedBlock parse_block(std::string_view body)
{
  ParsedBlock block;
  std::vector<std::string> condition_stack;
  std::string pending_statement;
  size_t pending_offset = 0;
  size_t pending_line_number = 0;
  std::vector<std::string> pending_conditions;

  auto flush_pending = [&](bool force_error) {
    if(pending_statement.empty()) return;

    Row row;
    row.conditions = pending_conditions;
    row.line_number = pending_line_number;
    parse_statement_line(pending_statement, pending_offset, pending_line_number, row);
    if(force_error && !row.statements.empty())
    {
      Statement &stmt = row.statements.front();
      if(stmt.kind == Statement::Kind::Error && stmt.error.empty()) stmt.error = "unterminated statement";
    }
    for(const Statement &stmt : row.statements)
    {
      if(stmt.kind == Statement::Kind::Error && !stmt.error.empty()) block.errors.push_back(stmt.error);
      if(stmt.kind == Statement::Kind::Declaration && stmt.error.empty())
      {
        VariableDecl decl;
        decl.name = stmt.name;
        decl.expr_source = stmt.args.front();
        decl.expr_span = stmt.arg_span;
        decl.line_number = stmt.line_number;
        Value literal;
        decl.computed = !parse_literal_expr(decl.expr_source, literal);
        if(block.declarations.count(decl.name) != 0)
          block.errors.push_back("duplicate variable '" + decl.name + "'");
        else
          block.declarations.emplace(decl.name, std::move(decl));
      }
    }
    block.rows.push_back(std::move(row));
    pending_statement.clear();
    pending_conditions.clear();
  };

  size_t pos = 0;
  size_t line_number = 0;
  while(pos < body.size())
  {
    const size_t line_start = pos;
    size_t line_end = body.find('\n', pos);
    const bool has_newline = line_end != std::string::npos;
    if(!has_newline) line_end = body.size();
    const std::string_view line(body.data() + line_start, line_end - line_start);

    if(line.size() >= 2 && line[0] == '/' && line[1] == '/')
    {
      pos = has_newline ? line_end + 1 : line_end;
      ++line_number;
      continue;
    }

    if(pending_statement.empty())
    {
      std::string condition_expr;
      if(parse_if_open_line(line, condition_expr))
      {
        condition_stack.push_back(std::move(condition_expr));
        pos = has_newline ? line_end + 1 : line_end;
        ++line_number;
        continue;
      }
      if(is_if_close_line(line))
      {
        if(condition_stack.empty())
          block.errors.push_back("unexpected '}' in UI block");
        else
          condition_stack.pop_back();
        pos = has_newline ? line_end + 1 : line_end;
        ++line_number;
        continue;
      }

      pending_statement = std::string(line);
      pending_offset = line_start;
      pending_line_number = line_number;
      pending_conditions = condition_stack;
    }
    else
    {
      pending_statement.push_back('\n');
      pending_statement.append(line.data(), line.size());
    }

    const bool needs_more = statement_needs_more_input(pending_statement);
    pos = has_newline ? line_end + 1 : line_end;
    ++line_number;
    if(needs_more) continue;

    flush_pending(false);
  }

  if(!pending_statement.empty())
  {
    flush_pending(statement_needs_more_input(pending_statement));
  }
  if(!condition_stack.empty()) block.errors.push_back("unterminated if(...) block in UI block");
  return block;
}

std::optional<std::string> parse_identifier_arg(const std::string &arg)
{
  const std::string_view t = trim(arg);
  if(t.empty() || !is_ident_start(t.front())) return std::nullopt;
  for(char c : t)
  {
    if(!is_ident_char(c)) return std::nullopt;
  }
  return std::string(t);
}

float evaluate_width(EvalContext &ctx, const std::string &expr, float fallback)
{
  ExprResult result = ctx.evaluate(expr);
  if(result.error.empty() && result.value.kind == ValueKind::Number) return std::max(24.0f, static_cast<float>(result.value.number));
  return fallback;
}

StyledLabel evaluate_label(EvalContext &ctx, const std::string &expr, const std::string &fallback)
{
  ExprResult result = ctx.evaluate(expr);
  if(!result.error.empty()) return make_styled_label(fallback);
  return make_styled_label(display_value(result.value));
}

std::vector<std::string> evaluate_options(EvalContext &ctx, const std::string &expr, std::string &error)
{
  ExprResult result = ctx.evaluate(expr);
  if(!result.error.empty())
  {
    error = result.error;
    return {};
  }
  if(result.value.kind != ValueKind::Array)
  {
    error = "options must evaluate to a list";
    return {};
  }

  std::vector<std::string> options;
  options.reserve(result.value.array.size());
  for(const Value &item : result.value.array)
  {
    if(!is_scalar_value(item))
    {
      error = "options must contain scalar values";
      return {};
    }
    options.push_back(display_value(item));
  }
  return options;
}

std::optional<std::pair<std::string, std::string>> parse_assignment(std::string_view expr)
{
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  bool in_string = false;
  bool escape = false;
  for(size_t i = 0; i < expr.size(); ++i)
  {
    const char c = expr[i];
    if(in_string)
    {
      if(escape)
        escape = false;
      else if(c == '\\')
        escape = true;
      else if(c == '"')
        in_string = false;
      continue;
    }
    if(c == '"')
    {
      in_string = true;
      continue;
    }
    if(c == '(')
      ++paren_depth;
    else if(c == ')')
      --paren_depth;
    else if(c == '[')
      ++bracket_depth;
    else if(c == ']')
      --bracket_depth;
    else if(c == '{')
      ++brace_depth;
    else if(c == '}')
      --brace_depth;
    else if(c == '=' && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0)
    {
      if(i + 1 < expr.size() && expr[i + 1] == '=') { ++i; continue; } // skip ==
      std::string_view lhs_raw = trim(expr.substr(0, i));
      const std::string rhs(trim(expr.substr(i + 1)));
      char compound_op = '\0';
      if(!lhs_raw.empty() && (lhs_raw.back() == '+' || lhs_raw.back() == '-' || lhs_raw.back() == '*' || lhs_raw.back() == '/'))
      {
        compound_op = lhs_raw.back();
        lhs_raw = trim(lhs_raw.substr(0, lhs_raw.size() - 1));
      }
      const std::string lhs(lhs_raw);
      if(lhs.empty() || rhs.empty()) return std::nullopt;
      if(compound_op != '\0')
        return std::make_pair(lhs, lhs + " " + compound_op + " (" + rhs + ")");
      return std::make_pair(lhs, rhs);
    }
  }
  return std::nullopt;
}

void render_error_inline(const std::string &message)
{
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
  ImGui::TextWrapped("UI error: %s", message.c_str());
  ImGui::PopStyleColor();
}

void set_override(EvalContext &ctx, const ParsedBlock &block, const std::string &name, const Value &value, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  const auto it = block.declarations.find(name);
  if(it != block.declarations.end())
  {
    if(it->second.computed)
    {
      errors.push_back("variable '" + name + "' is computed and readonly");
      return;
    }
    ctx.overrides[name] = value;
    ctx.cache.clear();
    ctx.cache_errors.clear();
    replacements[name] = serialize_value(value);
    return;
  }

  if(ctx.global_declarations)
  {
    const auto git = ctx.global_declarations->find(name);
    if(git != ctx.global_declarations->end())
    {
      if(git->second.computed)
      {
        errors.push_back("global variable '" + name + "' is computed and readonly");
        return;
      }
      ctx.overrides[name] = value;
      ctx.cache.clear();
      ctx.cache_errors.clear();
      ctx.global_replacements[name] = serialize_value(value);
      return;
    }
  }

  errors.push_back("unknown variable '" + name + "'");
}

void render_text_output(EvalContext &ctx, const Statement &stmt)
{
  if(stmt.args.size() != 1)
  {
    render_error_inline("text() expects 1 arg for output or 4 args for input");
    return;
  }
  ExprResult result = ctx.evaluate(stmt.args[0]);
  if(!result.error.empty())
  {
    render_error_inline(result.error);
    return;
  }
  const std::string rendered = display_value(result.value);
  MarkdownView::render_inline(rendered);
}

void render_text_input(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  static std::unordered_map<std::string, TextInputState> input_states;

  if(stmt.args.size() < 3)
  {
    render_error_inline("text() input expects value, label, width[, tooltip]");
    return;
  }
  const auto var_name = parse_identifier_arg(stmt.args[0]);
  if(!var_name)
  {
    render_error_inline("text() input must bind to a variable name");
    return;
  }
  const auto decl_it = block.declarations.find(*var_name);
  if(decl_it == block.declarations.end())
  {
    render_error_inline("unknown variable '" + *var_name + "'");
    return;
  }
  ExprResult value_result = ctx.resolve_variable(*var_name);
  if(!value_result.error.empty())
  {
    render_error_inline(value_result.error);
    return;
  }
  const bool readonly = decl_it->second.computed;
  const StyledLabel label = evaluate_label(ctx, stmt.args[1], *var_name);
  const float width = evaluate_width(ctx, stmt.args[2], 140.0f);
  const std::string widget_id = make_hidden_widget_id("text", stmt);
  std::string text_value = display_value(value_result.value);
  TextInputState &state = input_states[widget_id];
  if(!state.editing) state.draft = text_value;

  char buffer[512];
  std::snprintf(buffer, sizeof(buffer), "%s", state.draft.c_str());
  if(!label.text.empty())
  {
    render_styled_label(label);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  }
  ImGui::SetNextItemWidth(width);
  ImGui::BeginDisabled(readonly);
  const bool changed = ImGui::InputText(widget_id.c_str(), buffer, sizeof(buffer));
  const bool item_active = ImGui::IsItemActive();
  if(item_active) state.editing = true;
  if(changed)
  {
    state.draft = buffer;

    Value updated;
    updated.kind = ValueKind::String;
    updated.str = buffer;
    set_override(ctx, block, *var_name, updated, replacements, errors);
  }
  if(item_active && (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                     (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered())))
  {
    ImGui::ClearActiveID();
    state.editing = false;
  }
  else if(!item_active)
  {
    state.editing = false;
    state.draft = text_value;
  }
  ImGui::EndDisabled();
  if(stmt.args.size() >= 4)
  {
    const StyledLabel tooltip = evaluate_label(ctx, stmt.args[3], {});
    if(!tooltip.text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip.text.c_str());
  }
}

void render_int_input(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  if(stmt.args.size() != 4)
  {
    render_error_inline("int() expects value, label, width, buttons");
    return;
  }
  const auto var_name = parse_identifier_arg(stmt.args[0]);
  if(!var_name)
  {
    render_error_inline("int() must bind to a variable name");
    return;
  }
  const auto decl_it = block.declarations.find(*var_name);
  if(decl_it == block.declarations.end())
  {
    render_error_inline("unknown variable '" + *var_name + "'");
    return;
  }
  ExprResult value_result = ctx.resolve_variable(*var_name);
  if(!value_result.error.empty())
  {
    render_error_inline(value_result.error);
    return;
  }
  if(value_result.value.kind != ValueKind::Number)
  {
    render_error_inline("int() requires a numeric variable");
    return;
  }
  const bool readonly = decl_it->second.computed;
  const StyledLabel label = evaluate_label(ctx, stmt.args[1], *var_name);
  const float width = evaluate_width(ctx, stmt.args[2], 90.0f);
  ExprResult buttons_result = ctx.evaluate(stmt.args[3]);
  const bool buttons = buttons_result.error.empty() ? is_true(buttons_result.value) : false;
  int value = static_cast<int>(std::llround(value_result.value.number));
  if(!label.text.empty())
  {
    render_styled_label(label);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  }
  ImGui::SetNextItemWidth(width);
  ImGui::BeginDisabled(readonly);
  if(ImGui::InputInt(make_hidden_widget_id("int", stmt).c_str(), &value, buttons ? 1 : 0, buttons ? 10 : 0))
  {
    Value updated;
    updated.kind = ValueKind::Number;
    updated.number = static_cast<double>(value);
    updated.is_integer = true;
    set_override(ctx, block, *var_name, updated, replacements, errors);
  }
  ImGui::EndDisabled();
}

void render_slider(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  if(stmt.args.size() != 5)
  {
    render_error_inline("slider() expects value, label, width, min, max");
    return;
  }
  const auto var_name = parse_identifier_arg(stmt.args[0]);
  if(!var_name)
  {
    render_error_inline("slider() must bind to a variable name");
    return;
  }
  const auto decl_it = block.declarations.find(*var_name);
  if(decl_it == block.declarations.end())
  {
    render_error_inline("unknown variable '" + *var_name + "'");
    return;
  }
  ExprResult value_result = ctx.resolve_variable(*var_name);
  ExprResult min_result = ctx.evaluate(stmt.args[3]);
  ExprResult max_result = ctx.evaluate(stmt.args[4]);
  if(!value_result.error.empty())
  {
    render_error_inline(value_result.error);
    return;
  }
  if(!min_result.error.empty())
  {
    render_error_inline(min_result.error);
    return;
  }
  if(!max_result.error.empty())
  {
    render_error_inline(max_result.error);
    return;
  }
  if(value_result.value.kind != ValueKind::Number || min_result.value.kind != ValueKind::Number || max_result.value.kind != ValueKind::Number)
  {
    render_error_inline("slider() requires numeric values");
    return;
  }
  const bool readonly = decl_it->second.computed;
  const StyledLabel label = evaluate_label(ctx, stmt.args[1], *var_name);
  const float width = evaluate_width(ctx, stmt.args[2], 140.0f);
  if(!label.text.empty())
  {
    render_styled_label(label);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  }
  ImGui::SetNextItemWidth(width);
  ImGui::BeginDisabled(readonly);
  if(value_result.value.is_integer)
  {
    int value = static_cast<int>(std::llround(value_result.value.number));
    const int min_v = static_cast<int>(std::llround(min_result.value.number));
    const int max_v = static_cast<int>(std::llround(max_result.value.number));
    if(ImGui::SliderInt(make_hidden_widget_id("slider", stmt).c_str(), &value, min_v, max_v))
    {
      Value updated;
      updated.kind = ValueKind::Number;
      updated.number = static_cast<double>(value);
      updated.is_integer = true;
      set_override(ctx, block, *var_name, updated, replacements, errors);
    }
  }
  else
  {
    float value = static_cast<float>(value_result.value.number);
    if(ImGui::SliderFloat(make_hidden_widget_id("slider", stmt).c_str(), &value, static_cast<float>(min_result.value.number), static_cast<float>(max_result.value.number)))
    {
      Value updated;
      updated.kind = ValueKind::Number;
      updated.number = value;
      updated.is_integer = false;
      set_override(ctx, block, *var_name, updated, replacements, errors);
    }
  }
  ImGui::EndDisabled();
}

void render_checkbox(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  if(stmt.args.size() < 2)
  {
    render_error_inline("checkbox() expects value, label[, width]");
    return;
  }
  const auto var_name = parse_identifier_arg(stmt.args[0]);
  if(!var_name)
  {
    render_error_inline("checkbox() must bind to a variable name");
    return;
  }
  const auto decl_it = block.declarations.find(*var_name);
  if(decl_it == block.declarations.end())
  {
    render_error_inline("unknown variable '" + *var_name + "'");
    return;
  }
  ExprResult value_result = ctx.resolve_variable(*var_name);
  if(!value_result.error.empty())
  {
    render_error_inline(value_result.error);
    return;
  }
  if(value_result.value.kind != ValueKind::Bool)
  {
    render_error_inline("checkbox() requires a boolean variable");
    return;
  }
  const bool readonly = decl_it->second.computed;
  const StyledLabel label = evaluate_label(ctx, stmt.args[1], *var_name);
  bool value = value_result.value.boolean;
  ImGui::BeginDisabled(readonly);
  if(ImGui::Checkbox(make_hidden_widget_id("checkbox", stmt).c_str(), &value))
  {
    Value updated;
    updated.kind = ValueKind::Bool;
    updated.boolean = value;
    set_override(ctx, block, *var_name, updated, replacements, errors);
  }
  ImGui::EndDisabled();
  if(!label.text.empty())
  {
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    render_styled_label(label);
  }
}

void render_enum(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  if(stmt.args.size() != 4)
  {
    render_error_inline("enum() expects value, label, width, options");
    return;
  }
  const auto var_name = parse_identifier_arg(stmt.args[0]);
  if(!var_name)
  {
    render_error_inline("enum() must bind to a variable name");
    return;
  }
  const auto decl_it = block.declarations.find(*var_name);
  if(decl_it == block.declarations.end())
  {
    render_error_inline("unknown variable '" + *var_name + "'");
    return;
  }
  ExprResult value_result = ctx.resolve_variable(*var_name);
  if(!value_result.error.empty())
  {
    render_error_inline(value_result.error);
    return;
  }
  const bool readonly = decl_it->second.computed;
  const std::string current = display_value(value_result.value);
  std::string option_error;
  const std::vector<std::string> options = evaluate_options(ctx, stmt.args[3], option_error);
  if(!option_error.empty())
  {
    render_error_inline(option_error);
    return;
  }
  const StyledLabel label = evaluate_label(ctx, stmt.args[1], *var_name);
  const float width = evaluate_width(ctx, stmt.args[2], 140.0f);
  if(!label.text.empty())
  {
    render_styled_label(label);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  }
  ImGui::SetNextItemWidth(width);
  ImGui::BeginDisabled(readonly);
  if(ImGui::BeginCombo(make_hidden_widget_id("enum", stmt).c_str(), current.c_str()))
  {
    for(const std::string &option : options)
    {
      const bool selected = option == current;
      if(ImGui::Selectable(option.c_str(), selected))
      {
        Value updated;
        updated.kind = ValueKind::String;
        updated.str = option;
        set_override(ctx, block, *var_name, updated, replacements, errors);
      }
      if(selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
}

void render_multicheck(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  if(stmt.args.size() != 4)
  {
    render_error_inline("multicheck() expects value, label, width, options");
    return;
  }
  const auto var_name = parse_identifier_arg(stmt.args[0]);
  if(!var_name)
  {
    render_error_inline("multicheck() must bind to a variable name");
    return;
  }
  const auto decl_it = block.declarations.find(*var_name);
  if(decl_it == block.declarations.end())
  {
    render_error_inline("unknown variable '" + *var_name + "'");
    return;
  }
  ExprResult value_result = ctx.resolve_variable(*var_name);
  if(!value_result.error.empty())
  {
    render_error_inline(value_result.error);
    return;
  }
  if(value_result.value.kind != ValueKind::Array)
  {
    render_error_inline("multicheck() requires a list variable");
    return;
  }
  std::string option_error;
  const std::vector<std::string> options = evaluate_options(ctx, stmt.args[3], option_error);
  if(!option_error.empty())
  {
    render_error_inline(option_error);
    return;
  }
  const StyledLabel label = evaluate_label(ctx, stmt.args[1], *var_name);
  const float width = evaluate_width(ctx, stmt.args[2], 180.0f);
  std::set<std::string> selected;
  for(const Value &item : value_result.value.array)
  {
    if(!is_scalar_value(item))
    {
      render_error_inline("multicheck() list values must be scalar");
      return;
    }
    selected.insert(display_value(item));
  }
  std::string preview = display_value(value_result.value);
  if(preview.empty()) preview = "(none)";
  const bool readonly = decl_it->second.computed;
  if(!label.text.empty())
  {
    render_styled_label(label);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  }
  ImGui::SetNextItemWidth(width);
  ImGui::BeginDisabled(readonly);
  if(ImGui::BeginCombo(make_hidden_widget_id("multicheck", stmt).c_str(), preview.c_str()))
  {
    bool changed = false;
    for(const std::string &option : options)
    {
      bool checked = selected.count(option) != 0;
      if(ImGui::Selectable(option.c_str(), checked, ImGuiSelectableFlags_DontClosePopups))
      {
        if(checked)
          selected.erase(option);
        else
          selected.insert(option);
        changed = true;
      }
    }
    if(changed)
    {
      Value updated;
      updated.kind = ValueKind::Array;
      for(const std::string &option : options)
      {
        if(selected.count(option) == 0) continue;
        Value item;
        item.kind = ValueKind::String;
        item.str = option;
        updated.array.push_back(std::move(item));
      }
      set_override(ctx, block, *var_name, updated, replacements, errors);
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
}

bool parse_index_path(std::string_view text, std::vector<int> &path)
{
  path.clear();
  if(text.empty()) return false;

  size_t start = 0;
  while(start < text.size())
  {
    const size_t slash = text.find('/', start);
    const std::string_view part = text.substr(start, slash == std::string_view::npos ? text.size() - start : slash - start);
    if(part.empty()) return false;

    int value = 0;
    for(char c : part)
    {
      if(c < '0' || c > '9') return false;
      value = value * 10 + (c - '0');
    }
    path.push_back(value);
    if(slash == std::string_view::npos) break;
    start = slash + 1;
  }
  return !path.empty();
}

std::string make_index_path(const std::vector<int> &path)
{
  std::string out;
  for(size_t i = 0; i < path.size(); ++i)
  {
    if(i != 0) out.push_back('/');
    out += std::to_string(path[i]);
  }
  return out;
}

Value make_string_value(std::string text)
{
  Value value;
  value.kind = ValueKind::String;
  value.str = std::move(text);
  return value;
}

Value make_list_item_value(std::string name, std::string tooltip)
{
  Value item;
  item.kind = ValueKind::Object;
  upsert_object_field(item, "name", make_string_value(std::move(name)));
  upsert_object_field(item, "tooltip", make_string_value(std::move(tooltip)));
  return item;
}

Value make_inventory_slot_value(
    std::string title,
    std::string image,
    std::string tooltip,
    std::optional<int> quantity = std::nullopt,
    std::string mark_color = {},
    bool enabled = true)
{
  Value slot;
  slot.kind = ValueKind::Object;
  if(!title.empty()) upsert_object_field(slot, "name", make_string_value(std::move(title)));
  if(!image.empty()) upsert_object_field(slot, "image", make_string_value(std::move(image)));
  if(!tooltip.empty()) upsert_object_field(slot, "tooltip", make_string_value(std::move(tooltip)));
  if(quantity && *quantity > 0)
  {
    Value quantity_value;
    quantity_value.kind = ValueKind::Number;
    quantity_value.number = static_cast<double>(*quantity);
    upsert_object_field(slot, "quantity", std::move(quantity_value));
  }
  if(!mark_color.empty()) upsert_object_field(slot, "color", make_string_value(std::move(mark_color)));
  if(!enabled)
  {
    Value enabled_value;
    enabled_value.kind = ValueKind::Bool;
    enabled_value.boolean = false;
    upsert_object_field(slot, "enabled", std::move(enabled_value));
  }
  return slot;
}

Value *get_children_array(Value &item)
{
  Value *children = find_object_field(item, "children");
  if(!children || children->kind != ValueKind::Array) return nullptr;
  return children;
}

const Value *get_children_array(const Value &item)
{
  const Value *children = find_object_field(item, "children");
  if(!children || children->kind != ValueKind::Array) return nullptr;
  return children;
}

Value *get_list_parent_array(Value &root, const std::vector<int> &path)
{
  if(root.kind != ValueKind::Array) return nullptr;
  if(path.empty()) return &root;

  Value *current_array = &root;
  for(size_t depth = 0; depth + 1 < path.size(); ++depth)
  {
    const int index = path[depth];
    if(index < 0 || static_cast<size_t>(index) >= current_array->array.size()) return nullptr;
    Value &item = current_array->array[static_cast<size_t>(index)];
    current_array = get_children_array(item);
    if(!current_array) return nullptr;
  }
  return current_array;
}

Value *get_list_item(Value &root, const std::vector<int> &path)
{
  if(path.empty()) return nullptr;
  Value *parent = get_list_parent_array(root, path);
  if(!parent) return nullptr;
  const int index = path.back();
  if(index < 0 || static_cast<size_t>(index) >= parent->array.size()) return nullptr;
  return &parent->array[static_cast<size_t>(index)];
}

bool swap_list_items(Value &root, const std::vector<int> &from_path, const std::vector<int> &to_path)
{
  if(from_path.size() != to_path.size() || from_path.empty()) return false;
  if(!std::equal(from_path.begin(), from_path.end() - 1, to_path.begin())) return false;

  Value *parent = get_list_parent_array(root, from_path);
  if(!parent) return false;

  const int from_index = from_path.back();
  const int to_index = to_path.back();
  if(from_index < 0 || to_index < 0) return false;
  if(static_cast<size_t>(from_index) >= parent->array.size() || static_cast<size_t>(to_index) >= parent->array.size()) return false;
  if(from_index == to_index) return false;

  std::swap(parent->array[static_cast<size_t>(from_index)], parent->array[static_cast<size_t>(to_index)]);
  return true;
}

bool insert_list_item_after(Value &root, const std::vector<int> &path, Value item)
{
  if(path.empty())
  {
    if(root.kind != ValueKind::Array) return false;
    root.array.push_back(std::move(item));
    return true;
  }

  Value *parent = get_list_parent_array(root, path);
  if(!parent) return false;
  const int index = path.back();
  if(index < 0 || static_cast<size_t>(index) >= parent->array.size()) return false;
  parent->array.insert(parent->array.begin() + index + 1, std::move(item));
  return true;
}

bool remove_list_item(Value &root, const std::vector<int> &path)
{
  if(path.empty()) return false;
  Value *parent = get_list_parent_array(root, path);
  if(!parent) return false;
  const int index = path.back();
  if(index < 0 || static_cast<size_t>(index) >= parent->array.size()) return false;
  parent->array.erase(parent->array.begin() + index);
  return true;
}

bool append_list_child(Value &root, const std::vector<int> &path, Value item)
{
  Value *target = get_list_item(root, path);
  if(!target) return false;

  Value *children = get_children_array(*target);
  if(!children)
  {
    Value new_children;
    new_children.kind = ValueKind::Array;
    upsert_object_field(*target, "children", std::move(new_children));
    children = get_children_array(*target);
  }
  if(!children) return false;
  children->array.push_back(std::move(item));
  return true;
}

size_t count_list_rows(const Value &value, bool allow_children)
{
  if(value.kind != ValueKind::Array) return 0;
  (void)allow_children;

  size_t count = value.array.size();
  for(const Value &item : value.array)
  {
    if(const Value *children = get_children_array(item); children)
      count += count_list_rows(*children, true);
  }
  return count;
}

bool extract_list_item_info(const Value &item, std::string &name, std::string &tooltip, std::string &error)
{
  if(item.kind != ValueKind::Object)
  {
    error = "list items must be objects with a name field";
    return false;
  }

  const Value *name_value = find_object_field(item, "name");
  if(!name_value)
  {
    error = "list items require a name field";
    return false;
  }

  name = display_value(*name_value);
  tooltip.clear();
  if(const Value *tooltip_value = find_object_field(item, "tooltip"); tooltip_value)
    tooltip = display_value(*tooltip_value);
  return true;
}

bool update_object_string_field(Value &object_value, std::string_view field_name, const char *new_text)
{
  if(object_value.kind != ValueKind::Object) return false;
  const std::string updated = new_text ? std::string(new_text) : std::string();
  const Value *existing = find_object_field(object_value, field_name);
  if(existing && existing->kind == ValueKind::String && existing->str == updated) return false;
  upsert_object_field(object_value, std::string(field_name), make_string_value(updated));
  return true;
}

struct ListMutation
{
  enum class Kind
  {
    None,
    InsertRoot,
    InsertAfter,
    InsertChild,
    Remove,
  } kind = Kind::None;

  std::vector<int> path;
};

struct ListPopupEditorState
{
  char name[512]{};
  char tooltip[4096]{};
};

std::unordered_map<std::string, bool> g_list_collapsed_paths;

bool &list_item_collapsed_state(const std::string &path_text)
{
  return g_list_collapsed_paths[path_text];
}

void render_list_description_popup(const std::string &popup_id, const ImVec2 &anchor, float width, std::string_view text)
{
  if(text.empty()) return;

  ImGui::SetNextWindowPos(ImVec2(anchor.x + 10.0f, anchor.y + 8.0f));
  ImGui::SetNextWindowBgAlpha(0.96f);
  ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 0.0f), ImVec2(std::max(220.0f, width), 420.0f));
  if(ImGui::BeginTooltip())
  {
    ImGui::PushID(popup_id.c_str());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(220.0f, width));
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopID();
  }
  ImGui::EndTooltip();
}

bool render_list_row(EvalContext &ctx, Value &root, Value &item, const std::vector<int> &item_path, bool allow_children, const char *payload_type, float width, bool &changed, ListMutation &mutation)
{
  std::string name;
  std::string tooltip;
  std::string item_error;
  if(!extract_list_item_info(item, name, tooltip, item_error))
  {
    render_error_inline(item_error);
    return false;
  }

  static std::unordered_map<std::string, ListPopupEditorState> popup_states;

  const ImGuiStyle &style = ImGui::GetStyle();
  const float indent = 18.0f * static_cast<float>(item_path.size() > 0 ? item_path.size() - 1 : 0);
  const float row_width = std::max(220.0f, width - indent - style.ScrollbarSize);
  const float arrow_width = 18.0f;
  const float marker_width = 10.0f;
  const float text_width = std::max(140.0f, row_width - arrow_width - marker_width - style.FramePadding.x * 4.0f);
  const std::string path_text = make_index_path(item_path);
  const Value *children = get_children_array(item);
  const bool has_children = children && !children->array.empty();
  bool &collapsed = list_item_collapsed_state(path_text);
  if(!has_children) collapsed = false;

  if(indent > 0.0f) ImGui::Indent(indent);
  ImGui::PushID(path_text.c_str());

  const ImVec2 text_size = ImGui::CalcTextSize(name.c_str(), nullptr, false, text_width);
  const float row_height = std::max(text_size.y + style.FramePadding.y * 2.0f, ImGui::GetFrameHeight());
  ImGui::InvisibleButton("##row", ImVec2(row_width, row_height));

  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const bool row_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  if(hovered || active)
  {
    draw_list->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header), 6.0f);
  }

  const float arrow_x = min.x + style.FramePadding.x + 2.0f;
  const float arrow_y = min.y + row_height * 0.5f;
  if(has_children)
  {
    if(row_clicked && mouse.x <= min.x + arrow_width + style.FramePadding.x)
    {
      collapsed = !collapsed;
      ctx.preview_state_changed = true;
    }

    ImVec2 a, b, c;
    if(collapsed)
    {
      a = ImVec2(arrow_x, arrow_y - 5.0f);
      b = ImVec2(arrow_x, arrow_y + 5.0f);
      c = ImVec2(arrow_x + 6.0f, arrow_y);
    }
    else
    {
      a = ImVec2(arrow_x - 2.0f, arrow_y - 3.0f);
      b = ImVec2(arrow_x + 6.0f, arrow_y - 3.0f);
      c = ImVec2(arrow_x + 2.0f, arrow_y + 4.0f);
    }
    draw_list->AddTriangleFilled(a, b, c, ImGui::GetColorU32(ImGuiCol_TextDisabled));
  }

  const ImVec2 marker_center(min.x + arrow_width + style.FramePadding.x + 2.0f, min.y + row_height * 0.5f);
  draw_list->AddCircleFilled(marker_center, 2.5f, ImGui::GetColorU32(ImGuiCol_TextDisabled));
  draw_list->AddText(
      ImGui::GetFont(),
      ImGui::GetFontSize(),
      ImVec2(min.x + arrow_width + marker_width + style.FramePadding.x * 2.0f, min.y + style.FramePadding.y),
      ImGui::GetColorU32(ImGuiCol_Text),
      name.c_str(),
      nullptr,
      text_width);

  if(!tooltip.empty() && hovered && !ImGui::IsPopupOpen("##row_menu") && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
  {
    render_list_description_popup("##list_desc_" + path_text, max, std::min(420.0f, row_width), tooltip);
  }

  if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
  {
    ImGui::SetDragDropPayload(payload_type, path_text.c_str(), path_text.size() + 1);
    ImGui::TextUnformatted(name.c_str());
    ImGui::EndDragDropSource();
  }
  if(ImGui::BeginDragDropTarget())
  {
    if(const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(payload_type))
    {
      std::vector<int> source_path;
      const char *payload_text = static_cast<const char *>(payload->Data);
      if(payload_text && parse_index_path(payload_text, source_path) && swap_list_items(root, source_path, item_path))
        changed = true;
    }
    ImGui::EndDragDropTarget();
  }

  if(ImGui::BeginPopupContextItem("##row_menu"))
  {
    ListPopupEditorState &state = popup_states[path_text];
    if(ImGui::IsWindowAppearing())
    {
      std::snprintf(state.name, sizeof(state.name), "%s", name.c_str());
      std::snprintf(state.tooltip, sizeof(state.tooltip), "%s", tooltip.c_str());
    }

    const float popup_width = std::max(280.0f, std::min(row_width, 460.0f));
    bool apply_changes = false;
    bool discard_changes = false;
    ImGui::TextDisabled("List item");
    ImGui::SetNextItemWidth(popup_width);
    ImGui::InputText("Name", state.name, sizeof(state.name));

    ImGui::SetNextItemWidth(popup_width);
    ImGui::InputTextMultiline(
        "Description",
        state.tooltip,
        sizeof(state.tooltip),
        ImVec2(popup_width, 120.0f));

    ImGui::TextDisabled("Apply saves changes. Escape closes without saving.");
    if(ImGui::Button("Apply")) apply_changes = true;
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) discard_changes = true;
    if(apply_changes)
    {
      changed = update_object_string_field(item, "name", state.name) || changed;
      changed = update_object_string_field(item, "tooltip", state.tooltip) || changed;
      ImGui::CloseCurrentPopup();
    }
    else if(discard_changes)
    {
      ImGui::CloseCurrentPopup();
    }

    ImGui::Separator();
    if(ImGui::MenuItem("New item below"))
    {
      mutation.kind = ListMutation::Kind::InsertAfter;
      mutation.path = item_path;
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("New child"))
    {
      mutation.kind = ListMutation::Kind::InsertChild;
      mutation.path = item_path;
      ImGui::CloseCurrentPopup();
    }
    if(has_children && ImGui::MenuItem(collapsed ? "Expand children" : "Collapse children"))
    {
      collapsed = !collapsed;
      ctx.preview_state_changed = true;
    }
    if(ImGui::MenuItem("Remove"))
    {
      mutation.kind = ListMutation::Kind::Remove;
      mutation.path = item_path;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::PopID();
  if(indent > 0.0f) ImGui::Unindent(indent);
  return mutation.kind != ListMutation::Kind::None;
}

void render_list_branch(EvalContext &ctx, Value &root, Value &array_value, const std::vector<int> &parent_path, bool allow_children, const char *payload_type, float width, bool &changed, ListMutation &mutation)
{
  if(array_value.kind != ValueKind::Array) return;

  for(size_t i = 0; i < array_value.array.size(); ++i)
  {
    Value &item = array_value.array[i];
    std::vector<int> item_path = parent_path;
    item_path.push_back(static_cast<int>(i));
    const std::string path_text = make_index_path(item_path);
    if(render_list_row(ctx, root, item, item_path, allow_children, payload_type, width, changed, mutation)) return;

    if(Value *children = get_children_array(item); children && !children->array.empty() && !list_item_collapsed_state(path_text))
    {
      render_list_branch(ctx, root, *children, item_path, true, payload_type, width, changed, mutation);
      if(mutation.kind != ListMutation::Kind::None) return;
    }
  }
}

void render_list_widget(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  if(stmt.args.size() != 4)
  {
    render_error_inline("list() expects value, label, width, allow_children");
    return;
  }

  const auto var_name = parse_identifier_arg(stmt.args[0]);
  if(!var_name)
  {
    render_error_inline("list() must bind to a variable name");
    return;
  }
  const auto decl_it = block.declarations.find(*var_name);
  if(decl_it == block.declarations.end())
  {
    render_error_inline("unknown variable '" + *var_name + "'");
    return;
  }

  ExprResult value_result = ctx.resolve_variable(*var_name);
  if(!value_result.error.empty())
  {
    render_error_inline(value_result.error);
    return;
  }
  if(value_result.value.kind != ValueKind::Array)
  {
    render_error_inline("list() requires a list variable");
    return;
  }

  const StyledLabel label = evaluate_label(ctx, stmt.args[1], *var_name);
  const float requested_width = evaluate_width(ctx, stmt.args[2], 220.0f);
  const float available_width = std::max(0.0f, ImGui::GetContentRegionAvail().x);
  float widget_width = std::max(requested_width, 280.0f);
  if(available_width > 1.0f) widget_width = std::min(widget_width, available_width);
  ExprResult children_result = ctx.evaluate(stmt.args[3]);
  const bool allow_children = children_result.error.empty() && is_true(children_result.value);
  const bool readonly = decl_it->second.computed;
  const size_t row_count = std::max<size_t>(1, count_list_rows(value_result.value, allow_children));
  const float per_row_height = ImGui::GetTextLineHeightWithSpacing() * 2.2f;
  const float height = std::min(420.0f, std::max(120.0f, 20.0f + static_cast<float>(row_count) * per_row_height));
  const std::string child_id = make_hidden_widget_id("list", stmt);
  const std::string payload_type = "MDUI_LIST_" + make_statement_token(stmt);

  if(!label.text.empty()) render_styled_label(label);

  Value updated = value_result.value;
  bool changed = false;
  ImGui::BeginDisabled(readonly);
  if(ImGui::BeginChild(child_id.c_str(), ImVec2(widget_width, height), true))
  {
    if(updated.array.empty())
      ImGui::TextDisabled("(empty list)");
    else
    {
      ListMutation mutation;
      render_list_branch(ctx, updated, updated, {}, allow_children, payload_type.c_str(), widget_width - 10.0f, changed, mutation);
      if(mutation.kind == ListMutation::Kind::InsertAfter)
        changed = insert_list_item_after(updated, mutation.path, make_list_item_value("New item", "")) || changed;
      else if(mutation.kind == ListMutation::Kind::InsertChild)
        changed = append_list_child(updated, mutation.path, make_list_item_value("New child", "")) || changed;
      else if(mutation.kind == ListMutation::Kind::Remove)
        changed = remove_list_item(updated, mutation.path) || changed;
    }

    if(ImGui::BeginPopupContextWindow("##list_window_menu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
      if(ImGui::MenuItem("New item"))
      {
        changed = insert_list_item_after(updated, {}, make_list_item_value("New item", "")) || changed;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
  ImGui::EndChild();
  ImGui::EndDisabled();

  if(changed) set_override(ctx, block, *var_name, updated, replacements, errors);
}

struct InventoryDragPayload
{
  int index = -1;
  bool copy = false;
};

struct InventorySlotInfo
{
  std::string title;
  std::string image;
  std::string tooltip;
  std::string color_text;
  std::optional<int> quantity;
  std::optional<int> position;
  bool enabled = true;
  bool has_mark_color = false;
  ImVec4 mark_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
};

struct InventoryPopupEditorState
{
  char title[512]{};
  char image[512]{};
  char tooltip[4096]{};
  char quantity[64]{};
  char color[32]{};
  bool enabled = true;
  std::string error;
};

struct InventoryGridEditorState
{
  int cell_size = 48;
};

bool extract_inventory_slot(const Value &slot_value, InventorySlotInfo &slot, std::string &error)
{
  if(slot_value.kind != ValueKind::Object)
  {
    error = "inventory slots must be objects";
    return false;
  }

  slot.title.clear();
  slot.image.clear();
  slot.tooltip.clear();
  slot.color_text.clear();
  slot.quantity.reset();
  slot.position.reset();
  slot.enabled = true;
  slot.has_mark_color = false;
  slot.mark_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

  if(const Value *title_value = find_object_field(slot_value, "title"); title_value)
    slot.title = display_value(*title_value);
  else if(const Value *name_value = find_object_field(slot_value, "name"); name_value)
    slot.title = display_value(*name_value);
  else if(const Value *id_value = find_object_field(slot_value, "id"); id_value)
    slot.title = display_value(*id_value);

  if(const Value *image_value = find_object_field(slot_value, "image"); image_value)
    slot.image = display_value(*image_value);
  if(const Value *tooltip_value = find_object_field(slot_value, "tooltip"); tooltip_value)
    slot.tooltip = display_value(*tooltip_value);
  if(const Value *quantity_value = find_object_field(slot_value, "quantity"); quantity_value)
  {
    if(quantity_value->kind != ValueKind::Number)
    {
      error = "inventory quantity must be numeric";
      return false;
    }
    const int quantity = static_cast<int>(std::llround(quantity_value->number));
    if(quantity > 0) slot.quantity = quantity;
  }
  if(const Value *position_value = find_object_field(slot_value, "position"); position_value)
  {
    if(position_value->kind != ValueKind::Number)
    {
      error = "inventory position must be numeric";
      return false;
    }
    slot.position = static_cast<int>(std::llround(position_value->number));
  }
  if(const Value *color_value = find_object_field(slot_value, "color"); color_value)
  {
    slot.color_text = display_value(*color_value);
    if(!slot.color_text.empty())
    {
      if(!parse_hex_color_text(slot.color_text, slot.mark_color))
      {
        error = "inventory color must be #RRGGBB or #RRGGBBAA";
        return false;
      }
      slot.has_mark_color = true;
    }
  }
  if(const Value *enabled_value = find_object_field(slot_value, "enabled"); enabled_value)
  {
    if(enabled_value->kind != ValueKind::Bool)
    {
      error = "inventory enabled flag must be true or false";
      return false;
    }
    slot.enabled = enabled_value->boolean;
  }
  else if(const Value *disabled_value = find_object_field(slot_value, "disabled"); disabled_value)
  {
    if(disabled_value->kind != ValueKind::Bool)
    {
      error = "inventory disabled flag must be true or false";
      return false;
    }
    slot.enabled = !disabled_value->boolean;
  }
  return true;
}

bool inventory_slot_has_content(const Value &slot_value)
{
  if(slot_value.kind != ValueKind::Object) return true;
  for(const auto &[field_name, field_value] : slot_value.object)
  {
    if(field_name == "quantity")
    {
      if(field_value.kind == ValueKind::Number && std::llround(field_value.number) > 0) return true;
      continue;
    }
    if(field_name == "position") continue;
    if(field_name == "enabled")
    {
      if(field_value.kind == ValueKind::Bool && !field_value.boolean) return true;
      continue;
    }
    if(field_name == "disabled")
    {
      if(field_value.kind == ValueKind::Bool && field_value.boolean) return true;
      continue;
    }
    if(field_value.kind == ValueKind::String)
    {
      if(!trim(field_value.str).empty()) return true;
      continue;
    }
    if(field_value.kind == ValueKind::Number && std::fabs(field_value.number) > 1e-9) return true;
    if(field_value.kind == ValueKind::Bool && field_value.boolean) return true;
    if(field_value.kind == ValueKind::Object || field_value.kind == ValueKind::Array) return true;
  }
  return false;
}

bool inventory_slot_has_visual_content(const InventorySlotInfo &slot)
{
  return !slot.title.empty() || !slot.image.empty() || !slot.tooltip.empty() || slot.quantity.has_value();
}

void trim_inventory_slots(Value &items_value)
{
  if(items_value.kind != ValueKind::Array) return;
  while(!items_value.array.empty() && !inventory_slot_has_content(items_value.array.back()))
    items_value.array.pop_back();
}

bool parse_inventory_quantity(const char *text, std::optional<int> &quantity)
{
  const std::string trimmed_text(trim(text ? std::string(text) : std::string()));
  if(trimmed_text.empty())
  {
    quantity.reset();
    return true;
  }

  char *end = nullptr;
  const long parsed = std::strtol(trimmed_text.c_str(), &end, 10);
  if(end == trimmed_text.c_str() || (end && *end != '\0')) return false;
  if(parsed <= 0)
  {
    quantity.reset();
    return true;
  }

  quantity = static_cast<int>(parsed);
  return true;
}

bool parse_inventory_mark_color(const char *text, std::string &color_text, ImVec4 &color)
{
  const std::string trimmed_text(trim(text ? std::string(text) : std::string()));
  if(trimmed_text.empty())
  {
    color_text.clear();
    return true;
  }
  if(!parse_hex_color_text(trimmed_text, color)) return false;
  color_text = trimmed_text;
  return true;
}

void set_inventory_slot_position(Value &slot_value, int position)
{
  Value position_value;
  position_value.kind = ValueKind::Number;
  position_value.number = static_cast<double>(position);
  upsert_object_field(slot_value, "position", std::move(position_value));
}

bool remove_inventory_item(Value &items_value, int item_index)
{
  if(items_value.kind != ValueKind::Array) return false;
  if(item_index < 0 || static_cast<size_t>(item_index) >= items_value.array.size()) return false;
  items_value.array.erase(items_value.array.begin() + item_index);
  return true;
}

std::vector<int> build_inventory_cell_lookup(const Value &items_value, int cell_count)
{
  std::vector<int> lookup(static_cast<size_t>(std::max(0, cell_count)), -1);
  if(items_value.kind != ValueKind::Array) return lookup;

  for(size_t item_index = 0; item_index < items_value.array.size(); ++item_index)
  {
    InventorySlotInfo slot;
    std::string error;
    const bool slot_ok = extract_inventory_slot(items_value.array[item_index], slot, error);
    int cell_index = static_cast<int>(item_index);
    if(slot_ok && slot.position) cell_index = *slot.position;
    if(cell_index < 0 || cell_index >= cell_count)
    {
      if(static_cast<int>(item_index) < cell_count)
        cell_index = static_cast<int>(item_index);
      else
        continue;
    }
    if(lookup[static_cast<size_t>(cell_index)] == -1)
      lookup[static_cast<size_t>(cell_index)] = static_cast<int>(item_index);
  }
  return lookup;
}

std::string inventory_slot_fallback_label(const InventorySlotInfo &slot)
{
  std::string base = slot.title;
  if(base.empty() && !slot.image.empty()) base = std::filesystem::path(slot.image).stem().string();
  base = trim(base);
  if(base.empty()) return {};

  std::string compact;
  for(char c : base)
  {
    if(std::isalnum(static_cast<unsigned char>(c))) compact.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if(compact.size() == 2) break;
  }
  if(compact.empty() && !base.empty()) compact.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(base.front()))));
  return compact;
}

void render_inventory_hover_popup(const std::string &popup_id, const InventorySlotInfo &slot)
{
  if(slot.title.empty() && slot.tooltip.empty() && !slot.quantity.has_value() && slot.enabled) return;

  ImGui::SetNextWindowBgAlpha(0.96f);
  if(ImGui::BeginTooltip())
  {
    ImGui::PushID(popup_id.c_str());
    if(!slot.title.empty())
    {
      ImGui::TextUnformatted(slot.title.c_str());
      if(slot.quantity)
      {
        ImGui::SameLine();
        ImGui::TextDisabled("x%d", *slot.quantity);
      }
    }
    else if(slot.quantity)
    {
      ImGui::TextDisabled("x%d", *slot.quantity);
    }
    if(!slot.enabled)
    {
      if(!slot.title.empty() || slot.quantity) ImGui::Separator();
      ImGui::TextDisabled("Disabled slot");
    }
    if((!slot.title.empty() || slot.quantity || !slot.enabled) && !slot.tooltip.empty()) ImGui::Separator();
    if(!slot.tooltip.empty())
    {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 320.0f);
      ImGui::TextUnformatted(slot.tooltip.c_str());
      ImGui::PopTextWrapPos();
    }
    ImGui::PopID();
  }
  ImGui::EndTooltip();
}

void draw_inventory_slot_preview(const InventorySlotInfo &slot, const ImVec2 &min, const ImVec2 &max, bool selected, bool hovered)
{
  auto mix = [](const ImVec4 &a, const ImVec4 &b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t);
  };

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const bool has_content = inventory_slot_has_visual_content(slot);
  ImVec4 fill_color = has_content
      ? ImGui::GetStyleColorVec4(hovered && slot.enabled ? ImGuiCol_ButtonHovered : ImGuiCol_FrameBg)
      : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  if(slot.has_mark_color) fill_color = mix(fill_color, slot.mark_color, has_content ? 0.18f : 0.10f);
  ImVec4 border_color = selected
      ? ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram)
      : slot.has_mark_color ? slot.mark_color : ImGui::GetStyleColorVec4(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Border);
  if(!slot.enabled) fill_color = mix(fill_color, ImVec4(0.05f, 0.05f, 0.06f, fill_color.w), 0.35f);

  draw_list->AddRectFilled(min, max, ImGui::GetColorU32(fill_color), 6.0f);

  if(has_content)
  {
    const ImTextureID texture = get_widget_image_texture(slot.image);
    const ImU32 tint = ImGui::GetColorU32(slot.enabled ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.72f, 0.72f, 0.72f, 0.55f));
    if(texture != static_cast<ImTextureID>(0))
    {
      draw_list->AddImage(texture, min, max, ImVec2(0, 0), ImVec2(1, 1), tint);
    }
    else
    {
      const std::string label = inventory_slot_fallback_label(slot);
      if(!label.empty())
      {
        const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        const ImVec2 text_pos(
            min.x + (max.x - min.x - text_size.x) * 0.5f,
            min.y + (max.y - min.y - text_size.y) * 0.5f);
        draw_list->AddText(text_pos, ImGui::GetColorU32(slot.enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImVec4(0.78f, 0.78f, 0.78f, 0.55f)), label.c_str());
      }
    }

    if(slot.quantity && *slot.quantity > 1)
    {
      const std::string qty = std::to_string(*slot.quantity);
      const ImVec2 qty_size = ImGui::CalcTextSize(qty.c_str());
      const ImVec2 badge_min(max.x - qty_size.x - 14.0f, max.y - qty_size.y - 10.0f);
      const ImVec2 badge_max(max.x - 4.0f, max.y - 4.0f);
      draw_list->AddRectFilled(badge_min, badge_max, ImGui::GetColorU32(slot.enabled ? ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram) : ImVec4(0.30f, 0.30f, 0.32f, 0.92f)), 9.0f);
      draw_list->AddText(ImVec2(badge_min.x + 6.0f, badge_min.y + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), qty.c_str());
    }
  }

  // Border drawn last so it always appears on top of the image.
  draw_list->AddRect(min, max, ImGui::GetColorU32(border_color), 6.0f, 0, selected ? 2.0f : 1.0f);

  if(!slot.enabled)
  {
    draw_list->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.26f)), 6.0f);
    draw_list->AddLine(ImVec2(min.x + 6.0f, max.y - 6.0f), ImVec2(max.x - 6.0f, min.y + 6.0f), ImGui::GetColorU32(ImVec4(0.90f, 0.90f, 0.92f, 0.22f)), 1.5f);
  }
}

void render_inventory_widget(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  if(stmt.args.size() != 5)
  {
    render_error_inline("inventory() expects value, label, width, rows, cols");
    return;
  }

  const auto var_name = parse_identifier_arg(stmt.args[0]);
  if(!var_name)
  {
    render_error_inline("inventory() must bind to a variable name");
    return;
  }
  const auto decl_it = block.declarations.find(*var_name);
  if(decl_it == block.declarations.end())
  {
    render_error_inline("unknown variable '" + *var_name + "'");
    return;
  }

  ExprResult value_result = ctx.resolve_variable(*var_name);
  ExprResult rows_result = ctx.evaluate(stmt.args[3]);
  ExprResult cols_result = ctx.evaluate(stmt.args[4]);
  if(!value_result.error.empty())
  {
    render_error_inline(value_result.error);
    return;
  }
  if(!rows_result.error.empty())
  {
    render_error_inline(rows_result.error);
    return;
  }
  if(!cols_result.error.empty())
  {
    render_error_inline(cols_result.error);
    return;
  }
  if(value_result.value.kind != ValueKind::Object)
  {
    render_error_inline("inventory() requires an object variable");
    return;
  }
  if(rows_result.value.kind != ValueKind::Number || cols_result.value.kind != ValueKind::Number)
  {
    render_error_inline("inventory() rows and cols must be numeric");
    return;
  }

  const int rows = std::max(1, static_cast<int>(std::llround(rows_result.value.number)));
  const int cols = std::max(1, static_cast<int>(std::llround(cols_result.value.number)));
  const int total_cells = rows * cols;
  const StyledLabel label = evaluate_label(ctx, stmt.args[1], *var_name);
  const float requested_width = evaluate_width(ctx, stmt.args[2], 220.0f);
  const bool readonly = decl_it->second.computed;
  const ImGuiStyle &style = ImGui::GetStyle();
  const float spacing = style.ItemSpacing.x;
  const float available_width = std::max(0.0f, ImGui::GetContentRegionAvail().x);

  Value updated = value_result.value;

  constexpr float kDefaultCellSize = 48.0f;
  float cell_size = kDefaultCellSize;
  {
    const Value *cs = find_object_field(updated, "cell_size");
    if(cs && cs->kind == ValueKind::Number)
      cell_size = std::max(8.0f, std::min(256.0f, static_cast<float>(cs->number)));
  }
  const float grid_width = static_cast<float>(cols) * cell_size + static_cast<float>(cols - 1) * spacing + style.WindowPadding.x * 2.0f;
  float widget_width = std::max(160.0f, requested_width);
  if(available_width > 1.0f)
    widget_width = std::min(std::max(widget_width, std::min(grid_width, available_width)), available_width);
  else
    widget_width = std::max(widget_width, grid_width);
  const bool needs_scroll = grid_width > widget_width + 0.5f;
  const float height = rows * cell_size + (rows - 1) * spacing + style.WindowPadding.y * 2.0f;
  const std::string child_id = make_hidden_widget_id("inventory", stmt);
  const std::string payload_type = "MDUI_INV_" + make_statement_token(stmt);
  static std::unordered_map<std::string, int> selected_slot_by_widget;
  static std::unordered_map<std::string, InventoryPopupEditorState> popup_states;

  if(!label.text.empty()) render_styled_label(label);

  Value *items = find_object_field(updated, "items");
  if(!items || items->kind != ValueKind::Array)
  {
    render_error_inline("inventory() requires an items list inside the bound variable");
    return;
  }

  int &selected_index = selected_slot_by_widget[child_id];
  if(selected_index < 0 || selected_index >= total_cells) selected_index = 0;

  bool changed = false;
  ImGui::BeginDisabled(readonly);
  std::vector<int> cell_lookup = build_inventory_cell_lookup(*items, total_cells);
  bool any_slot_hovered = false;
  if(ImGui::BeginChild(child_id.c_str(), ImVec2(widget_width, height), true, needs_scroll ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None))
  {
    for(int row = 0; row < rows; ++row)
    {
      for(int col = 0; col < cols; ++col)
      {
        const int index = row * cols + col;
        if(col != 0) ImGui::SameLine(0.0f, spacing);

        ImGui::PushID(index);
        ImGui::InvisibleButton("##slot", ImVec2(cell_size, cell_size));
        if(ImGui::IsItemClicked(ImGuiMouseButton_Left)) selected_index = index;
        const bool hovered = ImGui::IsItemHovered();
        if(hovered) any_slot_hovered = true;
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        const int item_index = cell_lookup[static_cast<size_t>(index)];
        const bool has_slot_value = item_index >= 0 && static_cast<size_t>(item_index) < items->array.size();
        InventorySlotInfo slot;
        std::string slot_error;
        const bool slot_ok = !has_slot_value || extract_inventory_slot(items->array[static_cast<size_t>(item_index)], slot, slot_error);
        const bool has_content = has_slot_value && slot_ok && inventory_slot_has_content(items->array[static_cast<size_t>(item_index)]);
        if(!slot_ok)
        {
          slot.title = "!";
          slot.tooltip = slot_error;
          errors.push_back(slot_error);
        }
        draw_inventory_slot_preview(slot, min, max, selected_index == index, hovered);

        if((inventory_slot_has_visual_content(slot) || !slot.enabled) && hovered && !ImGui::IsPopupOpen("##slot_menu") && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          render_inventory_hover_popup("##inventory_desc_" + child_id + "_" + std::to_string(index), slot);

        if(has_content && slot.enabled && ImGui::BeginDragDropSource())
        {
          InventoryDragPayload payload;
          payload.index = index;
          payload.copy = ImGui::GetIO().KeyCtrl;
          ImGui::SetDragDropPayload(payload_type.c_str(), &payload, sizeof(payload));
          ImGui::TextUnformatted(slot.title.empty() ? (payload.copy ? "Copy item" : "Move item") : slot.title.c_str());
          if(payload.copy) ImGui::TextDisabled("Release over an empty slot to duplicate");
          ImGui::EndDragDropSource();
        }
        if(slot.enabled && ImGui::BeginDragDropTarget())
        {
          if(const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(payload_type.c_str()))
          {
            const auto *drag = static_cast<const InventoryDragPayload *>(payload->Data);
            if(drag && drag->index >= 0 && drag->index < total_cells && drag->index != index)
            {
              const int source_item_index = cell_lookup[static_cast<size_t>(drag->index)];
              const int target_item_index = item_index;
              if(source_item_index >= 0 && static_cast<size_t>(source_item_index) < items->array.size())
              {
                InventorySlotInfo source_slot;
                std::string source_error;
                if(extract_inventory_slot(items->array[static_cast<size_t>(source_item_index)], source_slot, source_error) && source_slot.enabled)
                {
                  const bool target_enabled = !has_slot_value || (slot_ok && slot.enabled);
                  if(target_enabled)
                  {
                    if(drag->copy && !has_slot_value)
                    {
                      Value duplicated = items->array[static_cast<size_t>(source_item_index)];
                      set_inventory_slot_position(duplicated, index);
                      items->array.push_back(std::move(duplicated));
                      changed = true;
                    }
                    else if(has_slot_value && target_item_index >= 0 && target_item_index != source_item_index)
                    {
                      set_inventory_slot_position(items->array[static_cast<size_t>(source_item_index)], index);
                      set_inventory_slot_position(items->array[static_cast<size_t>(target_item_index)], drag->index);
                      changed = true;
                    }
                    else if(!has_slot_value)
                    {
                      set_inventory_slot_position(items->array[static_cast<size_t>(source_item_index)], index);
                      changed = true;
                    }
                    if(changed) trim_inventory_slots(*items);
                  }
                }
              }
            }
          }
          if(const ImGuiPayload *img_payload = ImGui::AcceptDragDropPayload("NOTEPP_IMAGE_INSERT"))
          {
            const char *img_path_raw = static_cast<const char *>(img_payload->Data);
            if(img_path_raw)
            {
              std::string stored_path = img_path_raw;
              if(!g_widget_document_path.empty())
              {
                std::error_code ec;
                std::filesystem::path rel = std::filesystem::relative(
                    std::filesystem::path(img_path_raw),
                    g_widget_document_path.parent_path(), ec);
                if(!ec && !rel.empty()) stored_path = rel.generic_string();
              }
              if(has_slot_value && item_index >= 0 && static_cast<size_t>(item_index) < items->array.size())
              {
                InventorySlotInfo existing;
                std::string existing_error;
                extract_inventory_slot(items->array[static_cast<size_t>(item_index)], existing, existing_error);
                Value new_slot = make_inventory_slot_value(existing.title, stored_path, existing.tooltip, existing.quantity, existing.color_text, existing.enabled);
                set_inventory_slot_position(new_slot, index);
                items->array[static_cast<size_t>(item_index)] = std::move(new_slot);
              }
              else
              {
                Value new_slot = make_inventory_slot_value({}, stored_path, {});
                set_inventory_slot_position(new_slot, index);
                items->array.push_back(std::move(new_slot));
              }
              trim_inventory_slots(*items);
              changed = true;
            }
          }
          ImGui::EndDragDropTarget();
        }

        const std::string popup_key = child_id + "_" + std::to_string(index);
        if(ImGui::BeginPopupContextItem("##slot_menu"))
        {
          InventoryPopupEditorState &state = popup_states[popup_key];
          if(ImGui::IsWindowAppearing())
          {
            std::snprintf(state.title, sizeof(state.title), "%s", slot.title.c_str());
            std::snprintf(state.image, sizeof(state.image), "%s", slot.image.c_str());
            std::snprintf(state.tooltip, sizeof(state.tooltip), "%s", slot.tooltip.c_str());
            std::snprintf(state.quantity, sizeof(state.quantity), "%s", slot.quantity ? std::to_string(*slot.quantity).c_str() : "");
            std::snprintf(state.color, sizeof(state.color), "%s", slot.color_text.c_str());
            state.enabled = slot.enabled;
            state.error.clear();
          }

          const float popup_width = std::max(240.0f, std::min(widget_width, 360.0f));
          ImGui::TextDisabled(has_content ? "Inventory cell" : "Empty cell");
          ImGui::SetNextItemWidth(popup_width);
          ImGui::InputText("Title", state.title, sizeof(state.title));
          ImGui::SetNextItemWidth(popup_width);
          ImGui::InputText("Image", state.image, sizeof(state.image));
          ImGui::SetNextItemWidth(popup_width);
          ImGui::InputTextMultiline("Description", state.tooltip, sizeof(state.tooltip), ImVec2(popup_width, 96.0f));
          ImGui::SetNextItemWidth(120.0f);
          ImGui::InputText("Quantity", state.quantity, sizeof(state.quantity));
          ImGui::SetNextItemWidth(120.0f);
          ImGui::InputText("Mark color", state.color, sizeof(state.color));
          ImGui::Checkbox("Enabled", &state.enabled);
          ImGui::TextDisabled("Quick marks");
          auto preset_mark = [&](const char *button_label, const char *hex) {
            if(ImGui::SmallButton(button_label)) std::snprintf(state.color, sizeof(state.color), "%s", hex);
          };
          preset_mark("Green", "#58C472");
          ImGui::SameLine();
          preset_mark("Blue", "#57A7FF");
          ImGui::SameLine();
          preset_mark("Amber", "#FFB347");
          ImGui::SameLine();
          preset_mark("Red", "#FF6B6B");
          ImGui::SameLine();
          if(ImGui::SmallButton("Clear mark")) state.color[0] = '\0';
          if(!state.error.empty()) ImGui::TextColored(ImVec4(0.92f, 0.38f, 0.38f, 1.0f), "%s", state.error.c_str());
          ImGui::TextDisabled("Apply saves this cell. Hold Ctrl while dragging to copy into an empty slot.");

          if(ImGui::Button("Apply"))
          {
            std::optional<int> quantity;
            if(!parse_inventory_quantity(state.quantity, quantity))
            {
              state.error = "Quantity must be a whole number.";
            }
            else
            {
              std::string color_text;
              ImVec4 parsed_color;
              if(!parse_inventory_mark_color(state.color, color_text, parsed_color))
              {
                state.error = "Color must be #RRGGBB or #RRGGBBAA.";
              }
              else
              {
                const std::string title(trim(state.title));
                const std::string image(trim(state.image));
                const std::string tooltip(trim(state.tooltip));
                Value new_slot = make_inventory_slot_value(title, image, tooltip, quantity, color_text, state.enabled);
                if(inventory_slot_has_content(new_slot))
                {
                  set_inventory_slot_position(new_slot, index);
                  if(has_slot_value && item_index >= 0 && static_cast<size_t>(item_index) < items->array.size())
                    items->array[static_cast<size_t>(item_index)] = std::move(new_slot);
                  else
                    items->array.push_back(std::move(new_slot));
                }
                else if(has_slot_value)
                {
                  remove_inventory_item(*items, item_index);
                }
                trim_inventory_slots(*items);
                changed = true;
                selected_index = index;
                state.error.clear();
                ImGui::CloseCurrentPopup();
              }
            }
          }
          if(ImGui::Button("Clear"))
          {
            if(has_slot_value) remove_inventory_item(*items, item_index);
            trim_inventory_slots(*items);
            changed = true;
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }

        ImGui::PopID();
      }
    }

    static std::unordered_map<std::string, InventoryGridEditorState> grid_editor_states;
    if(!readonly && !any_slot_hovered && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      ImGui::OpenPopup("grid_settings");
      ctx.consumed_right_click = true;
    }
    if(!readonly && ImGui::BeginPopup("grid_settings"))
    {
      InventoryGridEditorState &gs = grid_editor_states[child_id];
      if(ImGui::IsWindowAppearing())
        gs.cell_size = static_cast<int>(cell_size);
      ImGui::TextDisabled("Grid settings");
      ImGui::SetNextItemWidth(120.0f);
      ImGui::InputInt("Cell size (px)", &gs.cell_size);
      gs.cell_size = std::clamp(gs.cell_size, 8, 256);
      if(ImGui::Button("Apply"))
      {
        Value cs_val;
        cs_val.kind = ValueKind::Number;
        cs_val.number = static_cast<double>(gs.cell_size);
        cs_val.is_integer = true;
        upsert_object_field(updated, "cell_size", std::move(cs_val));
        changed = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
  ImGui::EndChild();
  ImGui::EndDisabled();

  if(changed) set_override(ctx, block, *var_name, updated, replacements, errors);
}

void render_button(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  if(stmt.args.size() != 3)
  {
    render_error_inline("button() expects label, width, action");
    return;
  }
  const StyledLabel label = evaluate_label(ctx, stmt.args[0], "Button");
  const float width = evaluate_width(ctx, stmt.args[1], 90.0f);
  const auto assignment = parse_assignment(stmt.args[2]);
  if(!assignment)
  {
    render_error_inline("button() action must be assignment like variable=value");
    return;
  }
  if(label.has_color) ImGui::PushStyleColor(ImGuiCol_Text, label.color);
  if(ImGui::Button((label.text + "##button_" + make_statement_token(stmt)).c_str(), ImVec2(width, 0.0f)))
  {
    ExprResult value_result = ctx.evaluate(assignment->second);
    if(!value_result.error.empty())
      errors.push_back(value_result.error);
    else
      set_override(ctx, block, assignment->first, value_result.value, replacements, errors);
  }
  if(label.has_color) ImGui::PopStyleColor();
}

bool row_conditions_pass(EvalContext &ctx, const Row &row, std::vector<std::string> &errors)
{
  for(const std::string &condition : row.conditions)
  {
    ExprResult result = ctx.evaluate(condition);
    if(!result.error.empty())
    {
      errors.push_back("if(" + condition + ") error: " + result.error);
      return false;
    }
    if(!is_true(result.value)) return false;
  }
  return true;
}

void render_statement(EvalContext &ctx, const ParsedBlock &block, const Statement &stmt, std::unordered_map<std::string, std::string> &replacements, std::vector<std::string> &errors)
{
  if(!stmt.error.empty())
  {
    render_error_inline(stmt.error);
    return;
  }

  switch(stmt.kind)
  {
  case Statement::Kind::Declaration:
    break;
  case Statement::Kind::Assignment:
  {
    ExprResult result = ctx.evaluate(stmt.args[0]);
    if(!result.error.empty())
      errors.push_back(result.error);
    else
      set_override(ctx, block, stmt.name, result.value, replacements, errors);
    break;
  }
  case Statement::Kind::TextOutput:
    render_text_output(ctx, stmt);
    break;
  case Statement::Kind::TextInput:
    render_text_input(ctx, block, stmt, replacements, errors);
    break;
  case Statement::Kind::IntInput:
    render_int_input(ctx, block, stmt, replacements, errors);
    break;
  case Statement::Kind::Slider:
    render_slider(ctx, block, stmt, replacements, errors);
    break;
  case Statement::Kind::Checkbox:
    render_checkbox(ctx, block, stmt, replacements, errors);
    break;
  case Statement::Kind::Enum:
    render_enum(ctx, block, stmt, replacements, errors);
    break;
  case Statement::Kind::MultiCheck:
    render_multicheck(ctx, block, stmt, replacements, errors);
    break;
  case Statement::Kind::List:
    render_list_widget(ctx, block, stmt, replacements, errors);
    break;
  case Statement::Kind::Inventory:
    render_inventory_widget(ctx, block, stmt, replacements, errors);
    break;
  case Statement::Kind::Button:
    render_button(ctx, block, stmt, replacements, errors);
    break;
  case Statement::Kind::Error:
    render_error_inline(stmt.error.empty() ? "invalid UI statement" : stmt.error);
    break;
  }
}

void apply_replacements(std::string &markdown, size_t body_start, const ParsedBlock &block, const std::unordered_map<std::string, std::string> &replacements)
{
  struct PendingReplace
  {
    size_t start = 0;
    size_t end = 0;
    std::string text;
  };
  std::vector<PendingReplace> pending;
  pending.reserve(replacements.size());
  for(const auto &[name, text] : replacements)
  {
    const auto it = block.declarations.find(name);
    if(it == block.declarations.end()) continue;
    pending.push_back(PendingReplace{body_start + it->second.expr_span.start, body_start + it->second.expr_span.end, text});
  }
  std::sort(pending.begin(), pending.end(), [](const PendingReplace &a, const PendingReplace &b) {
    return a.start > b.start;
  });
  for(const PendingReplace &rep : pending)
  {
    markdown.replace(rep.start, rep.end - rep.start, rep.text);
  }
}

static std::unordered_map<std::string, ParsedBlock> g_block_parse_cache;

struct GlobalsFileCacheEntry
{
  std::filesystem::file_time_type mtime;
  std::string content;
};
static std::unordered_map<std::string, GlobalsFileCacheEntry> g_globals_file_cache;

static std::string read_globals_file(const std::filesystem::path &path)
{
  const std::string key = path.string();
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path, ec);
  if(ec) return {};

  if(auto it = g_globals_file_cache.find(key); it != g_globals_file_cache.end())
  {
    if(it->second.mtime == mtime) return it->second.content;
  }

  std::ifstream in(path, std::ios::binary);
  if(!in) return {};
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  g_globals_file_cache[key] = {mtime, content};
  return content;
}

static std::map<std::string, VariableDecl> g_globals_decl_cache;
static int g_globals_decl_cache_frame = -1;
static std::filesystem::path g_globals_decl_cache_doc;

static void invalidate_globals_decl_cache() { g_globals_decl_cache_frame = -1; }

static void apply_global_replacements_to_disk(
    const std::unordered_map<std::string, std::string> &global_replacements,
    const std::map<std::string, VariableDecl> &global_decls)
{
  if(global_replacements.empty()) return;

  // Group pending writes by source file
  using Change = std::pair<const VariableDecl *, std::string>;
  std::unordered_map<std::string, std::vector<Change>> by_file;
  for(const auto &[name, value] : global_replacements)
  {
    const auto it = global_decls.find(name);
    if(it == global_decls.end() || it->second.source_file.empty()) continue;
    by_file[it->second.source_file].emplace_back(&it->second, value);
  }

  for(auto &[file_path, changes] : by_file)
  {
    // Get current content (may already be cached)
    const std::string cached = read_globals_file(std::filesystem::path(file_path));
    if(cached.empty()) continue;
    std::string content = cached;

    // Apply replacements in reverse offset order so earlier spans stay valid
    std::sort(changes.begin(), changes.end(), [](const Change &a, const Change &b) {
      return (a.first->source_body_start + a.first->expr_span.start) >
             (b.first->source_body_start + b.first->expr_span.start);
    });
    for(const auto &[decl, value] : changes)
    {
      const size_t abs_start = decl->source_body_start + decl->expr_span.start;
      const size_t abs_end   = decl->source_body_start + decl->expr_span.end;
      if(abs_end > content.size()) continue;
      content.replace(abs_start, abs_end - abs_start, value);
    }

    // Write back to disk
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if(!out) continue;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();

    // Update the in-memory file cache with the new content so subsequent
    // reads in the same frame see the updated values without a false mtime miss
    std::error_code ec;
    const auto new_mtime = std::filesystem::last_write_time(file_path, ec);
    g_globals_file_cache[file_path] = {ec ? std::filesystem::file_time_type{} : new_mtime, content};
  }

  // Force reload of the merged declarations on the next render call
  invalidate_globals_decl_cache();
}

static const std::map<std::string, VariableDecl> &load_global_declarations()
{
  const int cur_frame = ImGui::GetFrameCount();
  if(cur_frame == g_globals_decl_cache_frame && g_globals_decl_cache_doc == g_widget_document_path)
    return g_globals_decl_cache;

  g_globals_decl_cache_frame = cur_frame;
  g_globals_decl_cache_doc = g_widget_document_path;
  g_globals_decl_cache.clear();

  if(g_widget_document_path.empty()) return g_globals_decl_cache;

  std::vector<std::filesystem::path> chain;
  std::filesystem::path dir = g_widget_document_path.parent_path();
  while(!dir.empty() && dir != dir.parent_path())
  {
    chain.push_back(dir / ".globals.md");
    dir = dir.parent_path();
  }
  std::reverse(chain.begin(), chain.end());

  for(const auto &globals_path : chain)
  {
    const std::string content = read_globals_file(globals_path);
    if(content.empty()) continue;

    size_t pos = 0;
    while(pos < content.size())
    {
      const size_t fence_pos = content.find("```ui", pos);
      if(fence_pos == std::string::npos) break;

      const size_t after_fence = content.find('\n', fence_pos);
      if(after_fence == std::string::npos) break;
      const size_t body_start = after_fence + 1;

      size_t scan = body_start;
      size_t block_end = std::string::npos;
      while(scan < content.size())
      {
        const size_t ls = scan;
        size_t le = content.find('\n', scan);
        const bool ln = (le != std::string::npos);
        if(!ln) le = content.size();
        const std::string_view line(content.data() + ls, le - ls);
        if(trim(line) == "```") { block_end = ls; break; }
        scan = ln ? le + 1 : le;
      }
      if(block_end == std::string::npos) break;

      const std::string_view body(content.data() + body_start, block_end - body_start);
      const std::string body_key(body);
      auto cache_it = g_block_parse_cache.find(body_key);
      if(cache_it == g_block_parse_cache.end())
        cache_it = g_block_parse_cache.emplace(body_key, parse_block(body)).first;

      for(const auto &[name, parsed_decl] : cache_it->second.declarations)
      {
        VariableDecl d = parsed_decl;
        d.source_file = globals_path.string();
        d.source_body_start = body_start;
        g_globals_decl_cache[name] = std::move(d);
      }

      pos = block_end;
    }
  }
  return g_globals_decl_cache;
}

} // namespace

std::string capture_ui_state_snapshot()
{
  std::string snapshot;
  for(const auto &[path, is_collapsed] : g_list_collapsed_paths)
  {
    snapshot += (is_collapsed ? "1\t" : "0\t");
    snapshot += path;
    snapshot += "\n";
  }
  return snapshot;
}

void apply_ui_state_snapshot(std::string_view snapshot)
{
  g_list_collapsed_paths.clear();

  size_t start = 0;
  while(start < snapshot.size())
  {
    size_t end = snapshot.find("\n", start);
    if(end == std::string_view::npos) end = snapshot.size();
    const std::string_view line = snapshot.substr(start, end - start);
    const size_t tab = line.find("\t");
    if(tab != std::string_view::npos && tab > 0)
    {
      const bool is_collapsed = line.front() == '1';
      const std::string path(line.substr(tab + 1));
      if(!path.empty()) g_list_collapsed_paths[path] = is_collapsed;
    }
    start = end + 1;
  }
}

RenderResult try_render_ui_block(std::string &markdown, size_t fence_start, size_t fence_line_end, size_t block_end)
{
  RenderResult result;
  const size_t body_start = fence_line_end + 1;
  size_t fence_close_start = body_start;
  while(fence_close_start < block_end && markdown[fence_close_start] != '`') ++fence_close_start;
  if(fence_close_start >= block_end) return result;

  const std::string_view body(markdown.data() + body_start, fence_close_start - body_start);
  std::string body_key(body);
  auto cache_it = g_block_parse_cache.find(body_key);
  if(cache_it == g_block_parse_cache.end())
    cache_it = g_block_parse_cache.emplace(body_key, parse_block(body)).first;
  const ParsedBlock &block = cache_it->second;

  result.handled = true;
  ImGui::PushID(static_cast<int>(fence_start));
  ImGui::BeginGroup();

  const std::map<std::string, VariableDecl> &globals = load_global_declarations();

  EvalContext ctx{block};
  ctx.global_declarations = globals.empty() ? nullptr : &globals;
  std::unordered_map<std::string, std::string> replacements;
  std::vector<std::string> runtime_errors = block.errors;

  for(const Row &row : block.rows)
  {
    if(!row_conditions_pass(ctx, row, runtime_errors)) continue;

    bool any_rendered = false;
    for(const Statement &stmt : row.statements)
    {
      const bool renders = stmt.kind != Statement::Kind::Declaration && stmt.kind != Statement::Kind::Assignment;
      if(renders && any_rendered) ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
      if(renders || !stmt.error.empty())
      {
        ImGui::BeginGroup();
        render_statement(ctx, block, stmt, replacements, runtime_errors);
        ImGui::EndGroup();
        any_rendered = any_rendered || renders || !stmt.error.empty();
      }
      else if(stmt.kind == Statement::Kind::Assignment)
      {
        render_statement(ctx, block, stmt, replacements, runtime_errors);
      }
    }
    if(any_rendered) ImGui::Dummy(ImVec2(0.0f, 0.0f));
  }

  for(const std::string &error : runtime_errors)
  {
    render_error_inline(error);
  }

  ImGui::EndGroup();
  ImGui::PopID();

  if(!replacements.empty())
  {
    apply_replacements(markdown, body_start, block, replacements);
    result.markdown_changed = true;
  }
  if(!ctx.global_replacements.empty())
    apply_global_replacements_to_disk(ctx.global_replacements, globals);
  result.preview_state_changed = ctx.preview_state_changed;
  result.consumed_right_click = ctx.consumed_right_click;
  // Erase stale cache entry after all reads from block are done
  if(result.markdown_changed)
    g_block_parse_cache.erase(body_key);
  return result;
}

static std::string format_value_for_mermaid(const Value &v)
{
  switch(v.kind)
  {
  case ValueKind::Number:
    if(v.is_integer)
      return std::to_string(static_cast<long long>(v.number));
    else
    {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%g", v.number);
      return std::string(buf);
    }
  case ValueKind::String:
    return v.str;
  case ValueKind::Bool:
    return v.boolean ? "true" : "false";
  case ValueKind::Array:
  {
    std::string out = "[";
    for(size_t i = 0; i < v.array.size(); ++i)
    {
      if(i > 0) out += ", ";
      out += format_value_for_mermaid(v.array[i]);
    }
    out += "]";
    return out;
  }
  default:
    return "";
  }
}

static ParsedBlock collect_note_ui_declarations(std::string_view markdown)
{
  ParsedBlock merged;
  size_t pos = 0;
  while(pos < markdown.size())
  {
    const size_t fence_pos = markdown.find("```ui", pos);
    if(fence_pos == std::string::npos) break;

    const size_t after_fence = markdown.find('\n', fence_pos);
    if(after_fence == std::string::npos) break;

    const std::string_view fence_line(markdown.data() + fence_pos, after_fence - fence_pos);
    if(trim(fence_line) != "```ui")
    {
      pos = after_fence + 1;
      continue;
    }

    const size_t body_start = after_fence + 1;
    size_t scan = body_start;
    size_t body_end = std::string::npos;
    while(scan < markdown.size())
    {
      const size_t ls = scan;
      size_t le = markdown.find('\n', scan);
      const bool ln = (le != std::string::npos);
      if(!ln) le = markdown.size();
      const std::string_view line(markdown.data() + ls, le - ls);
      if(trim(line) == "```") { body_end = ls; break; }
      scan = ln ? le + 1 : le;
    }
    if(body_end == std::string::npos) break;

    const std::string_view body(markdown.data() + body_start, body_end - body_start);
    const std::string body_key(body);
    auto cache_it = g_block_parse_cache.find(body_key);
    if(cache_it == g_block_parse_cache.end())
      cache_it = g_block_parse_cache.emplace(body_key, parse_block(body)).first;

    for(const auto &[name, decl] : cache_it->second.declarations)
      merged.declarations[name] = decl;

    pos = body_end + 1;
  }
  return merged;
}

std::string resolve_ui_mermaid_template(std::string_view note_markdown, std::string_view template_body)
{
  ParsedBlock note_decls = collect_note_ui_declarations(note_markdown);
  const std::map<std::string, VariableDecl> &globals = load_global_declarations();

  EvalContext ctx{note_decls};
  ctx.global_declarations = globals.empty() ? nullptr : &globals;

  std::string result;
  result.reserve(template_body.size());

  size_t pos = 0;
  while(pos < template_body.size())
  {
    const size_t marker = template_body.find("${", pos);
    if(marker == std::string_view::npos)
    {
      result.append(template_body.data() + pos, template_body.size() - pos);
      break;
    }

    result.append(template_body.data() + pos, marker - pos);

    const size_t expr_start = marker + 2;
    const size_t close = template_body.find('}', expr_start);
    if(close == std::string_view::npos)
    {
      result.append(template_body.data() + marker, template_body.size() - marker);
      break;
    }

    const std::string_view expr(template_body.data() + expr_start, close - expr_start);
    const ExprResult eval_result = ctx.evaluate(expr);
    if(eval_result.error.empty())
      result += format_value_for_mermaid(eval_result.value);
    else
      result.append(template_body.data() + marker, close - marker + 1);

    pos = close + 1;
  }

  return result;
}
} // namespace MarkdownUi
