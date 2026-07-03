// ── git_renderer.cpp ───────────────────────────────────────────────────────
//
// Git graph diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace gitrender
{
static ImVec2 git_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 git_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace gitrender

void render_git(const GitDiagram &d, int id)
{
  using namespace gitrender;
  if(d.branches.empty()) return;
  ImGui::PushID(id);
  const float commit_r = 8.0f, commit_gap = 40.0f, branch_gap = 36.0f, pad = 20.0f;
  int nb = static_cast<int>(d.branches.size());
  std::unordered_map<std::string, int> b_commit_count;
  for(auto &c : d.commits) b_commit_count[c.branch]++;
  int max_commits = 0;
  for(auto &p : b_commit_count) max_commits = std::max(max_commits, p.second + 1);
  max_commits = std::max(max_commits, static_cast<int>(d.commits.size()) / std::max(1, nb) + 2);
  float cw = pad * 2.0f + max_commits * commit_gap;
  float ch = pad * 2.0f + nb * branch_gap;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##git", git_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  std::unordered_map<std::string, int> bidx;
  for(int i = 0; i < nb; ++i) bidx[d.branches[i]] = i;
  auto branch_y = [&](const std::string &b) -> float {
    auto it = bidx.find(b);
    int bi = it != bidx.end() ? it->second : 0;
    return orig.y + pad + bi * branch_gap;
  };
  for(int i = 0; i < nb; ++i)
  {
    float y = orig.y + pad + i * branch_gap;
    ImU32 bc = git_series_color(i);
    dl->AddLine(ImVec2(orig.x + pad, y), ImVec2(orig.x + pad + max_commits * commit_gap, y), bc, 2.5f);
    dl->AddText(ImVec2(orig.x + 2.0f, y - 8.0f), bc, d.branches[i].c_str());
  }
  std::unordered_map<std::string, ImVec2> last_pos_on_branch;
  std::unordered_map<std::string, int> b_idx_counter;
  std::unordered_map<std::string, ImVec2> commit_id_pos;
  for(auto &c : d.commits)
  {
    int bi = bidx.count(c.branch) ? bidx[c.branch] : 0;
    int ci = b_idx_counter[c.branch]++;
    float x = orig.x + pad + (ci + 1) * commit_gap;
    float y = branch_y(c.branch);
    ImVec2 pos(x, y);
    if(!c.id.empty()) commit_id_pos[c.id] = pos;
    ImU32 bc = git_series_color(bi);
    if(c.is_merge && !c.merge_from.empty())
    {
      auto it = last_pos_on_branch.find(c.merge_from);
      if(it != last_pos_on_branch.end())
        dl->AddLine(it->second, pos, git_series_color(bidx.count(c.merge_from) ? bidx[c.merge_from] : 0), 2.0f);
    }
    last_pos_on_branch[c.branch] = pos;
    ImU32 fc = c.type == GitCommit::T::Highlight ? ImGui::GetColorU32(ImVec4(1, 0.8f, 0.2f, 1)) : bc;
    if(c.type == GitCommit::T::Reverse)
    {
      dl->AddCircle(pos, commit_r, bc, 0, 2.5f);
      dl->AddCircleFilled(pos, commit_r - 3.0f, ImGui::GetColorU32(ImGuiCol_WindowBg));
    }
    else
    {
      dl->AddCircleFilled(pos, commit_r, fc);
      dl->AddCircle(pos, commit_r, ImGui::GetColorU32(ImGuiCol_Border), 0, 1.5f);
    }
    if(!c.id.empty())
    {
      ImVec2 ts = ImGui::CalcTextSize(c.id.c_str());
      dl->AddText(ImVec2(x - ts.x * 0.5f, y + commit_r + 2.0f), tcol, c.id.c_str());
    }
    if(!c.tag.empty())
    {
      std::string t2 = "[" + c.tag + "]";
      ImVec2 ts = ImGui::CalcTextSize(t2.c_str());
      dl->AddText(ImVec2(x - ts.x * 0.5f, y - commit_r - ts.y - 2.0f),
                  ImGui::GetColorU32(ImVec4(1, 0.85f, 0.2f, 1)), t2.c_str());
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
