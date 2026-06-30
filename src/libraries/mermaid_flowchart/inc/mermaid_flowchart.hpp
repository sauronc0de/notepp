#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace MermaidFlowchart
{
enum class Direction
{
  TB,
  BT,
  LR,
  RL
};

struct Node
{
  std::string id;
  std::string label;
};

struct Edge
{
  int from = -1;
  int to = -1;
};

struct Graph
{
  Direction direction = Direction::TB;
  std::vector<Node> nodes;
  std::vector<Edge> edges;
};

bool parse(std::string_view src, Graph &out);
void render(const Graph &g, int id);
} // namespace MermaidFlowchart

