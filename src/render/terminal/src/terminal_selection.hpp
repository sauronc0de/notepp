#pragma once

namespace notepp::terminal_detail
{

struct TerminalCell
{
  int row = -1;
  int col = -1;
};

constexpr bool cellBeforeOrEqual(TerminalCell lhs, TerminalCell rhs) noexcept
{
  return lhs.row < rhs.row || (lhs.row == rhs.row && lhs.col <= rhs.col);
}

constexpr TerminalCell selectionStart(TerminalCell first, TerminalCell second) noexcept
{
  return cellBeforeOrEqual(first, second) ? first : second;
}

constexpr TerminalCell selectionEnd(TerminalCell first, TerminalCell second) noexcept
{
  return cellBeforeOrEqual(first, second) ? second : first;
}

constexpr bool terminalSelectionContains(TerminalCell first, TerminalCell last, int row, int col) noexcept
{
  if(first.row < 0 || first.col < 0 || last.row < 0 || last.col < 0) return false;
  const TerminalCell start = selectionStart(first, last);
  const TerminalCell end = selectionEnd(first, last);
  if(row < start.row || row > end.row) return false;
  if(start.row == end.row) return col >= start.col && col <= end.col;
  if(row == start.row) return col >= start.col;
  if(row == end.row) return col <= end.col;
  return true;
}

} // namespace notepp::terminal_detail
