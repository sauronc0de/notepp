#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

struct TSLanguage;

namespace MarkdownCodeHighlight
{
enum class HighlightKind
{
  Plain,
  Comment,
  Keyword,
  String,
  Function,
  Variable,
  Type,
  Number,
  Operator,
  Property,
  Constant,
  Punctuation,
};

struct HighlightSpan
{
  size_t start = 0;
  size_t end = 0;
  HighlightKind kind = HighlightKind::Plain;
};

using LanguageFactory = const TSLanguage *(*)();

struct LanguageDefinition
{
  std::string name;
  std::vector<std::string> aliases;
  LanguageFactory language_factory = nullptr;
  std::string_view highlight_query;
};

struct HighlightedCodeBlock
{
  std::string requested_language;
  std::string resolved_language;
  bool recognized_language = false;
  std::vector<HighlightSpan> spans;
};

void register_language(LanguageDefinition definition);
const LanguageDefinition *find_language(std::string_view fence_info);
HighlightedCodeBlock highlight_code_block(std::string_view fence_info, std::string_view source);
void render_code_block(std::string_view fence_info, std::string_view source, int imgui_id);
} // namespace MarkdownCodeHighlight
