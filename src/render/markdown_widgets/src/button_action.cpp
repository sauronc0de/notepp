#include "button_action.hpp"

#include <cctype>
#include <string_view>

namespace MarkdownWidgets::detail
{
namespace
{
std::string_view trim(std::string_view value)
{
  while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
  while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
  return value;
}

bool isIdentifierCharacter(char value)
{
  const unsigned char character = static_cast<unsigned char>(value);
  return std::isalnum(character) != 0 || value == '_';
}
} // namespace

CommandAction parseCommandAction(std::string_view action)
{
  const std::string_view text = trim(action);
  size_t nameEnd = 0;
  while(nameEnd < text.size() && isIdentifierCharacter(text[nameEnd])) ++nameEnd;
  if(text.substr(0, nameEnd) != "command") return {};

  size_t open = nameEnd;
  while(open < text.size() && std::isspace(static_cast<unsigned char>(text[open]))) ++open;
  if(open >= text.size() || text[open] != '(')
    return {CommandActionStatus::Invalid, {}, "command action expects '(' after command"};

  int parenthesisDepth = 1;
  int bracketDepth = 0;
  int braceDepth = 0;
  bool inString = false;
  bool escaped = false;
  size_t argumentStart = open + 1;
  size_t argumentEnd = std::string_view::npos;
  size_t argumentCount = 1;
  for(size_t index = argumentStart; index < text.size(); ++index)
  {
    const char character = text[index];
    if(inString)
    {
      if(escaped)
        escaped = false;
      else if(character == '\\')
        escaped = true;
      else if(character == '"')
        inString = false;
      continue;
    }

    if(character == '"')
    {
      inString = true;
      continue;
    }
    if(character == '(')
      ++parenthesisDepth;
    else if(character == ')')
    {
      --parenthesisDepth;
      if(parenthesisDepth == 0)
      {
        argumentEnd = index;
        const std::string_view trailing = trim(text.substr(index + 1));
        if(!trailing.empty())
          return {CommandActionStatus::Invalid, {}, "command action has unexpected trailing text"};
        break;
      }
    }
    else if(character == '[')
      ++bracketDepth;
    else if(character == ']')
      --bracketDepth;
    else if(character == '{')
      ++braceDepth;
    else if(character == '}')
      --braceDepth;
    else if(character == ',' && parenthesisDepth == 1 && bracketDepth == 0 && braceDepth == 0)
      ++argumentCount;
  }

  if(inString) return {CommandActionStatus::Invalid, {}, "command action contains an unterminated string"};
  if(argumentEnd == std::string_view::npos)
    return {CommandActionStatus::Invalid, {}, "command action is missing its closing ')'"};

  const std::string_view argument = trim(text.substr(argumentStart, argumentEnd - argumentStart));
  if(argument.empty() || argumentCount != 1)
    return {CommandActionStatus::Invalid, {}, "command() expects exactly one argument"};

  return {CommandActionStatus::Valid, std::string(argument), {}};
}
} // namespace MarkdownWidgets::detail
