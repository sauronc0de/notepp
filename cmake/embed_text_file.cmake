cmake_minimum_required(VERSION 3.16)

# Required arguments: INPUT_FILE, OUTPUT_FILE
# Reads INPUT_FILE as raw bytes (hex), then writes OUTPUT_FILE as a C++ header
# exposing kDemoNoteContent as an inline constexpr std::string_view.

file(READ "${INPUT_FILE}" content HEX)

# Convert hex pairs to "0xNN," tokens
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," content "${content}")

get_filename_component(input_name "${INPUT_FILE}" NAME)

file(WRITE "${OUTPUT_FILE}"
"#pragma once
#include <string_view>
// Auto-generated from ${input_name} — do not edit manually.
static const unsigned char kDemoNoteContentData[] = {
${content}0x00
};
inline const std::string_view kDemoNoteContent{
    reinterpret_cast<const char*>(kDemoNoteContentData), sizeof(kDemoNoteContentData) - 1
};
")
