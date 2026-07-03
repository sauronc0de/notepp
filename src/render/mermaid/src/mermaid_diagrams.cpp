#include "mermaid_diagrams.hpp"

namespace MermaidDiagrams
{
// Diagram parsers and renderers live in src/diagrams/*_parser.cpp and
// src/diagrams/*_renderer.cpp. This translation unit keeps the shared
// interactive edit back-channel definitions.

PendingEdit g_pending_edit;
bool g_consumed_right_click = false;
} // namespace MermaidDiagrams
