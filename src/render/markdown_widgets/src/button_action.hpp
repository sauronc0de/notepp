#pragma once

#include <string>
#include <string_view>

namespace MarkdownWidgets::detail
{
enum class CommandActionStatus
{
  NotCommand,
  Valid,
  Invalid,
};

struct CommandAction
{
  CommandActionStatus status = CommandActionStatus::NotCommand;
  std::string argument;
  std::string error;
};

CommandAction parseCommandAction(std::string_view action);
} // namespace MarkdownWidgets::detail
