#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace MarkdownTables
{
struct ParsedMarkdownTable
{
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> rows;
  size_t block_start = 0;
  size_t block_end = 0;
  bool trailing_newline = false;
};

/**
 * @brief Split a single table line into its cell strings.
 *
 * Strips leading/trailing pipe characters and trims whitespace per cell.
 * Empty lines or lines without a pipe return an empty vector.
 */
std::vector<std::string> split_md_table_cells(std::string_view line);

/**
 * @brief Validate that a line is a markdown table separator row.
 *
 * A separator row consists of N cells separated by `|`, where each cell
 * contains only '-' (optionally prefixed/suffixed by ':') with at least 3 dashes.
 */
bool is_md_table_separator(std::string_view line, std::size_t expected_cols);

/**
 * @brief Parse a markdown table starting at line_start in @p markdown.
 *
 * @param markdown     Source markdown.
 * @param line_start   Byte offset of the header line's first byte.
 * @param line_end     Byte offset one past the header line's last byte
 *                     (excluding any trailing newline).
 * @param has_newline  True if the header line is followed by a newline.
 * @param out          Output structure populated on success.
 * @return true on successful parse, false if the block is not a table.
 */
bool try_parse_markdown_table(const std::string &markdown,
                              std::size_t line_start,
                              std::size_t line_end,
                              bool has_newline,
                              ParsedMarkdownTable &out);

/**
 * @brief Escape pipes and newlines for a cell value used in a markdown table.
 */
std::string normalize_table_cell_value(std::string_view in);

/**
 * @brief Build a single table line "| a | b | c |" from the given cells.
 */
std::string build_md_table_line(const std::vector<std::string> &cells);

/**
 * @brief Build a separator row of N columns (e.g. "| --- | --- |").
 */
std::string build_md_table_separator(std::size_t cols);

/**
 * @brief Build a full markdown table from a header and rows.
 *
 * @param header           Header cells.
 * @param rows             Data rows.
 * @param trailing_newline Append a trailing newline.
 */
std::string build_md_table_markdown(const std::vector<std::string> &header,
                                    const std::vector<std::vector<std::string>> &rows,
                                    bool trailing_newline);
} // namespace MarkdownTables