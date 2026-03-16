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
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

namespace MarkdownUi
{
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
  StringList,
};

struct Value
{
  ValueKind kind = ValueKind::Invalid;
  double number = 0.0;
  bool is_integer = false;
  std::string str;
  bool boolean = false;
  std::vector<std::string> list;
};

struct Statement
{
  enum class Kind
  {
    Declaration,
    TextOutput,
    TextInput,
    IntInput,
    Slider,
    Checkbox,
    Enum,
    MultiCheck,
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
};

struct ParsedBlock
{
  std::vector<Row> rows;
  std::vector<std::string> errors;
  std::map<std::string, VariableDecl> declarations;
};

bool is_widget_name(std::string_view name)
{
  return name == "text" ||
         name == "int" ||
         name == "slider" ||
         name == "checkbox" ||
         name == "enum" ||
         name == "multicheck" ||
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
    if(c == '(') ++depth;
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

std::string make_hidden_widget_id(const char *prefix, const Statement &stmt)
{
  return std::string("##") + prefix + "_" + std::to_string(stmt.span.start);
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

std::string escape_string(std::string_view s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for(char c : s)
  {
    switch(c)
    {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
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
    case ValueKind::StringList:
    {
      std::string out = "[";
      for(size_t i = 0; i < value.list.size(); ++i)
      {
        if(i != 0) out += ", ";
        out += "\"" + escape_string(value.list[i]) + "\"";
      }
      out += "]";
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
    case ValueKind::StringList:
    {
      std::string out;
      for(size_t i = 0; i < value.list.size(); ++i)
      {
        if(i != 0) out += ", ";
        out += value.list[i];
      }
      return out;
    }
    default:
      return "<invalid>";
  }
}

bool is_true(const Value &value)
{
  if(value.kind == ValueKind::Bool) return value.boolean;
  if(value.kind == ValueKind::Number) return std::fabs(value.number) > 1e-9;
  if(value.kind == ValueKind::String) return !value.str.empty();
  if(value.kind == ValueKind::StringList) return !value.list.empty();
  return false;
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
  ExprResult parse_additive();
  ExprResult parse_multiplicative();
  ExprResult parse_unary();
  ExprResult parse_primary();
  ExprResult parse_list();
  bool consume(char c);
  char peek() const;
  static ExprResult combine_numeric(const ExprResult &lhs, const ExprResult &rhs, char op);

  std::string_view source_;
  size_t pos_ = 0;
  EvalContext &ctx_;
};

struct EvalContext
{
  const ParsedBlock &block;
  std::unordered_map<std::string, Value> overrides;
  std::unordered_map<std::string, Value> cache;
  std::unordered_map<std::string, std::string> cache_errors;
  std::set<std::string> visiting;

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
    if(decl_it == block.declarations.end()) return {{}, "unknown variable '" + name + "'"};
    if(visiting.count(name) != 0) return {{}, "circular dependency involving '" + name + "'"};

    visiting.insert(name);
    ExprResult result = evaluate(decl_it->second.expr_source);
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
    case '+': out.number = lhs.value.number + rhs.value.number; break;
    case '-': out.number = lhs.value.number - rhs.value.number; break;
    case '*': out.number = lhs.value.number * rhs.value.number; break;
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
  return parse_additive();
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
  return parse_primary();
}

ExprResult ExprParser::parse_list()
{
  if(!consume('[')) return {{}, "expected '['"};
  Value out;
  out.kind = ValueKind::StringList;
  skip_ws();
  if(consume(']')) return {out, {}};

  while(true)
  {
    ExprResult item = parse_expression();
    if(!item.error.empty()) return item;
    if(item.value.kind == ValueKind::String)
      out.list.push_back(item.value.str);
    else if(item.value.kind == ValueKind::Number)
      out.list.push_back(format_number(item.value.number, item.value.is_integer));
    else if(item.value.kind == ValueKind::Bool)
      out.list.push_back(item.value.boolean ? "true" : "false");
    else
      return {{}, "list items must be scalar values"};

    skip_ws();
    if(consume(']')) break;
    if(!consume(',')) return {{}, "expected ',' or ']'"};
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

  if(peek() == '[') return parse_list();

  if(peek() == '"')
  {
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
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case '\\': out.push_back('\\'); break;
          case '"': out.push_back('"'); break;
          default: out.push_back(esc); break;
        }
      }
      else
      {
        out.push_back(c);
      }
    }
    return {{}, "unterminated string literal"};
  }

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
    return ctx_.resolve_variable(ident);
  }

  return {{}, "unexpected token in expression"};
}

std::vector<std::string> split_top_level_args(std::string_view args_text)
{
  std::vector<std::string> args;
  std::string current;
  int paren_depth = 0;
  int bracket_depth = 0;
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
    if(c == '(') ++paren_depth;
    else if(c == ')') --paren_depth;
    else if(c == '[') ++bracket_depth;
    else if(c == ']') --bracket_depth;

    if(c == ',' && paren_depth == 0 && bracket_depth == 0)
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
    if(pos >= line.size() || line[pos] != '(')
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
      if(c == '(') ++depth;
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

ParsedBlock parse_block(std::string_view body, size_t body_start)
{
  ParsedBlock block;
  std::vector<std::string> condition_stack;
  size_t pos = 0;
  size_t line_number = 0;
  while(pos < body.size())
  {
    const size_t line_start = pos;
    size_t line_end = body.find('\n', pos);
    const bool has_newline = line_end != std::string::npos;
    if(!has_newline) line_end = body.size();
    const std::string_view line(body.data() + line_start, line_end - line_start);

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

    Row row;
    row.conditions = condition_stack;
    row.line_number = line_number;
    parse_statement_line(line, body_start + line_start, line_number, row);
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
    pos = has_newline ? line_end + 1 : line_end;
    ++line_number;
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
  if(result.value.kind != ValueKind::StringList)
  {
    error = "options must evaluate to a string list";
    return {};
  }
  return result.value.list;
}

std::optional<std::pair<std::string, std::string>> parse_assignment(std::string_view expr)
{
  int paren_depth = 0;
  int bracket_depth = 0;
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
    if(c == '(') ++paren_depth;
    else if(c == ')') --paren_depth;
    else if(c == '[') ++bracket_depth;
    else if(c == ']') --bracket_depth;
    else if(c == '=' && paren_depth == 0 && bracket_depth == 0)
    {
      const std::string lhs(trim(expr.substr(0, i)));
      const std::string rhs(trim(expr.substr(i + 1)));
      if(lhs.empty() || rhs.empty()) return std::nullopt;
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
  if(it == block.declarations.end())
  {
    errors.push_back("unknown variable '" + name + "'");
    return;
  }
  if(it->second.computed)
  {
    errors.push_back("variable '" + name + "' is computed and readonly");
    return;
  }
  ctx.overrides[name] = value;
  ctx.cache.clear();
  ctx.cache_errors.clear();
  replacements[name] = serialize_value(value);
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
  std::string text_value = display_value(value_result.value);
  char buffer[512];
  std::snprintf(buffer, sizeof(buffer), "%s", text_value.c_str());
  if(!label.text.empty())
  {
    render_styled_label(label);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  }
  ImGui::SetNextItemWidth(width);
  ImGui::BeginDisabled(readonly);
  if(ImGui::InputText(make_hidden_widget_id("text", stmt).c_str(), buffer, sizeof(buffer)))
  {
    Value updated;
    updated.kind = ValueKind::String;
    updated.str = buffer;
    set_override(ctx, block, *var_name, updated, replacements, errors);
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
  if(!value_result.error.empty()) { render_error_inline(value_result.error); return; }
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
  if(!value_result.error.empty()) { render_error_inline(value_result.error); return; }
  if(!min_result.error.empty()) { render_error_inline(min_result.error); return; }
  if(!max_result.error.empty()) { render_error_inline(max_result.error); return; }
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
  if(!value_result.error.empty()) { render_error_inline(value_result.error); return; }
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
  if(!value_result.error.empty()) { render_error_inline(value_result.error); return; }
  const bool readonly = decl_it->second.computed;
  const std::string current = display_value(value_result.value);
  std::string option_error;
  const std::vector<std::string> options = evaluate_options(ctx, stmt.args[3], option_error);
  if(!option_error.empty()) { render_error_inline(option_error); return; }
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
  if(!value_result.error.empty()) { render_error_inline(value_result.error); return; }
  if(value_result.value.kind != ValueKind::StringList)
  {
    render_error_inline("multicheck() requires a string list variable");
    return;
  }
  std::string option_error;
  const std::vector<std::string> options = evaluate_options(ctx, stmt.args[3], option_error);
  if(!option_error.empty()) { render_error_inline(option_error); return; }
  const StyledLabel label = evaluate_label(ctx, stmt.args[1], *var_name);
  std::set<std::string> selected(value_result.value.list.begin(), value_result.value.list.end());
  std::string preview = display_value(value_result.value);
  if(preview.empty()) preview = "(none)";
  const bool readonly = decl_it->second.computed;
  if(!label.text.empty())
  {
    render_styled_label(label);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  }
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
      updated.kind = ValueKind::StringList;
      updated.list.assign(selected.begin(), selected.end());
      set_override(ctx, block, *var_name, updated, replacements, errors);
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
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
  if(ImGui::Button((label.text + "##button_" + std::to_string(stmt.span.start)).c_str(), ImVec2(width, 0.0f)))
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
    case Statement::Kind::Button:
      render_button(ctx, block, stmt, replacements, errors);
      break;
    case Statement::Kind::Error:
      render_error_inline(stmt.error.empty() ? "invalid UI statement" : stmt.error);
      break;
  }
}

void apply_replacements(std::string &markdown, const ParsedBlock &block, const std::unordered_map<std::string, std::string> &replacements)
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
    pending.push_back(PendingReplace{it->second.expr_span.start, it->second.expr_span.end, text});
  }
  std::sort(pending.begin(), pending.end(), [](const PendingReplace &a, const PendingReplace &b) {
    return a.start > b.start;
  });
  for(const PendingReplace &rep : pending)
  {
    markdown.replace(rep.start, rep.end - rep.start, rep.text);
  }
}

} // namespace

RenderResult try_render_ui_block(std::string &markdown, size_t fence_start, size_t fence_line_end, size_t block_end)
{
  RenderResult result;
  const size_t body_start = fence_line_end + 1;
  size_t fence_close_start = body_start;
  while(fence_close_start < block_end && markdown[fence_close_start] != '`') ++fence_close_start;
  if(fence_close_start >= block_end) return result;

  const std::string_view body(markdown.data() + body_start, fence_close_start - body_start);
  ParsedBlock block = parse_block(body, body_start);

  result.handled = true;
  ImGui::PushID(static_cast<int>(fence_start));
  ImGui::BeginGroup();

  EvalContext ctx{block};
  std::unordered_map<std::string, std::string> replacements;
  std::vector<std::string> runtime_errors = block.errors;

  for(const Row &row : block.rows)
  {
    if(!row_conditions_pass(ctx, row, runtime_errors)) continue;

    bool any_rendered = false;
    for(const Statement &stmt : row.statements)
    {
      const bool renders = stmt.kind != Statement::Kind::Declaration;
      if(renders && any_rendered) ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
      if(renders || !stmt.error.empty())
      {
        render_statement(ctx, block, stmt, replacements, runtime_errors);
        any_rendered = any_rendered || renders || !stmt.error.empty();
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
    apply_replacements(markdown, block, replacements);
    result.markdown_changed = true;
  }
  return result;
}
} // namespace MarkdownUi
