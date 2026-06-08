#include "mermaid_diagrams.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <imgui.h>

static constexpr float kPi = 3.14159265f;

namespace MermaidDiagrams
{
namespace
{
// ── parsing helpers ───────────────────────────────────────────────────────────
static bool sw(std::string_view s, std::string_view p) { return NoteCore::starts_with(s, p); }
static std::string_view tr(std::string_view s) { return NoteCore::trim(s); }
static std::string lc(std::string_view s) { return NoteCore::to_lower_copy(s); }

static std::string strip_quotes(std::string_view s)
{
  s = tr(s);
  if(s.size() >= 2 && ((s.front()=='"'&&s.back()=='"')||(s.front()=='\''&&s.back()=='\'')))
    return std::string(s.substr(1, s.size()-2));
  return std::string(s);
}

// iterate lines, trimmed, skipping empty, %% and // comments
struct Lines {
  std::string_view src;
  size_t pos = 0;
  bool next(std::string_view &out) {
    while(pos < src.size()) {
      size_t e = src.find('\n', pos);
      if(e == std::string_view::npos) e = src.size();
      std::string_view line = tr(src.substr(pos, e - pos));
      pos = (e < src.size()) ? e + 1 : e;
      if(line.empty() || sw(line, "%%") || sw(line, "//")) continue;
      out = line;
      return true;
    }
    return false;
  }
};

// split "A --> B : label" at first occurrence of any arrow
static bool split_arrow(std::string_view line, std::string_view &lhs,
                         std::string_view &rhs, std::string &label_out)
{
  static const char *arrows[] = {"-->>","--x","-->","-.->","<-->>","<-->","<--","-.","---","->","==>",nullptr};
  size_t best = std::string_view::npos, best_len = 0;
  for(int i = 0; arrows[i]; ++i) {
    size_t p = line.find(arrows[i]);
    if(p != std::string_view::npos && (best == std::string_view::npos || p < best)) {
      best = p; best_len = std::strlen(arrows[i]);
    }
  }
  if(best == std::string_view::npos) return false;
  lhs = tr(line.substr(0, best));
  std::string_view r = tr(line.substr(best + best_len));
  // check label after ":"
  size_t col = r.rfind(':');
  if(col != std::string_view::npos && col+1 < r.size()) {
    label_out = std::string(tr(r.substr(col+1)));
    rhs = tr(r.substr(0, col));
  } else {
    rhs = r; label_out = "";
  }
  return !lhs.empty() && !rhs.empty();
}

// drawing helpers
static ImVec2 center_text(ImVec2 p, ImVec2 sz, const std::string &t)
{
  ImVec2 ts = ImGui::CalcTextSize(t.c_str());
  return ImVec2(p.x + (sz.x - ts.x)*0.5f, p.y + (sz.y - ts.y)*0.5f);
}

static void draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float sz, ImU32 col, bool open=false)
{
  ImVec2 n(-dir.y, dir.x);
  ImVec2 l(tip.x - dir.x*sz + n.x*(sz*0.5f), tip.y - dir.y*sz + n.y*(sz*0.5f));
  ImVec2 r(tip.x - dir.x*sz - n.x*(sz*0.5f), tip.y - dir.y*sz - n.y*(sz*0.5f));
  if(open) { dl->AddLine(l, tip, col, 1.5f); dl->AddLine(r, tip, col, 1.5f); }
  else      dl->AddTriangleFilled(tip, l, r, col);
}

static ImU32 series_color(int i, float alpha=1.0f)
{
  float h = static_cast<float>(i) * 0.618034f; h -= std::floor(h);
  float r,g,b; ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r,g,b,alpha));
}

// layout: place n items in rows of w, return {col, row} for index i
static std::pair<int,int> grid_pos(int i, int cols)
{ return {i%cols, i/cols}; }

// wrap label to max_w pixels, returns multi-line string
static std::string wrap_label(const std::string &s, float max_w)
{
  if(ImGui::CalcTextSize(s.c_str()).x <= max_w) return s;
  std::istringstream ss(s);
  std::string word, line, result;
  while(ss >> word) {
    std::string test = line.empty() ? word : line+" "+word;
    if(ImGui::CalcTextSize(test.c_str()).x <= max_w) { line = test; }
    else { if(!result.empty()) result+="\n"; result+=line; line=word; }
  }
  if(!line.empty()) { if(!result.empty()) result+="\n"; result+=line; }
  return result;
}

// draw a rounded box and return the rect painted
static ImVec4 draw_box(ImDrawList *dl, ImVec2 p, ImVec2 sz, ImU32 fill,
                        ImU32 border, const std::string &label, float rounding=4.0f)
{
  ImVec2 p2(p.x+sz.x, p.y+sz.y);
  dl->AddRectFilled(p, p2, fill, rounding);
  dl->AddRect(p, p2, border, rounding);
  ImVec2 tp = center_text(p, sz, label);
  dl->AddText(tp, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
  return ImVec4(p.x, p.y, p2.x, p2.y);
}

// like Lines but preserves raw indentation; returns trimmed text + indent count
struct IndentLines {
  std::string_view src; size_t pos=0;
  bool next(std::string_view &out, int &indent) {
    while(pos < src.size()) {
      size_t e=src.find('\n',pos);
      if(e==std::string_view::npos) e=src.size();
      std::string_view raw=src.substr(pos,e-pos);
      pos=(e<src.size())?e+1:e;
      std::string_view t=tr(raw);
      if(t.empty()||sw(t,"%%")) continue;
      indent=0; for(char c:raw){if(c==' ')indent++;else if(c=='\t')indent+=2;else break;}
      out=t; return true;
    }
    return false;
  }
};

// point on box boundary (half-extents hw,hh) in direction of 'other'
static ImVec2 rect_edge(ImVec2 cen, float hw, float hh, ImVec2 other)
{
  float dx=other.x-cen.x, dy=other.y-cen.y;
  if(std::abs(dx)<0.001f&&std::abs(dy)<0.001f) return cen;
  float tx=hw/std::abs(dx), ty=hh/std::abs(dy);
  float t=std::min(tx,ty);
  return ImVec2(cen.x+dx*t, cen.y+dy*t);
}

// point on circle boundary of radius r toward 'other'
static ImVec2 circ_edge(ImVec2 cen, float r, ImVec2 other)
{
  float dx=other.x-cen.x, dy=other.y-cen.y;
  float len=std::sqrt(dx*dx+dy*dy);
  if(len<0.001f) return cen;
  return ImVec2(cen.x+dx/len*r, cen.y+dy/len*r);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// SEQUENCE DIAGRAM
// ═══════════════════════════════════════════════════════════════════════════
bool parse_sequence(std::string_view src, SequenceDiagram &out)
{
  out = SequenceDiagram{};
  std::unordered_map<std::string, int> pidx;
  auto ensure_part = [&](const std::string &id, const std::string &lbl = "") {
    auto it = pidx.find(id);
    if(it != pidx.end()) return it->second;
    SeqParticipant p; p.id = id; p.label = lbl.empty() ? id : lbl;
    int n = (int)out.participants.size();
    out.participants.push_back(p); pidx[id] = n; return n;
  };

  Lines L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line)) {
    std::string ll = lc(line);
    if(!header) {
      if(sw(ll,"sequencediagram")) { header=true; continue; }
      continue;
    }
    if(sw(ll,"title "))  { out.title = std::string(tr(line.substr(6))); continue; }
    if(sw(ll,"participant ") || sw(ll,"actor ")) {
      bool actor = sw(ll,"actor ");
      std::string_view rest = tr(line.substr(actor?6:12));
      std::string id_s = std::string(rest), lbl_s = id_s;
      size_t as = rest.find(" as "); if(as==std::string_view::npos) as=lc(rest).find(" as ");
      if(as!=std::string_view::npos){ id_s=std::string(tr(rest.substr(0,as))); lbl_s=std::string(tr(rest.substr(as+4))); }
      int n = ensure_part(id_s, lbl_s); out.participants[n].is_actor = actor; continue;
    }
    if(sw(ll,"note ")) {
      // Note over A, B: text  or  Note left of A: text  or  Note right of A: text
      size_t col = line.find(':');
      std::string text = (col!=std::string_view::npos) ? std::string(tr(line.substr(col+1))) : "";
      std::string_view spec = (col!=std::string_view::npos) ? tr(line.substr(5, col-5)) : tr(line.substr(5));
      SeqNote note; note.text = text;
      size_t comma = spec.find(',');
      if(sw(lc(spec),"over ")) {
        std::string_view ids = tr(spec.substr(5));
        if(comma!=std::string_view::npos) { note.over1=std::string(tr(ids.substr(0,comma-5+5-5))); note.over2=std::string(tr(ids.substr(comma-5+5-5+1))); }
        // simpler: just split by comma after "over "
        std::string_view ids2 = tr(spec.substr(5));
        size_t c2 = ids2.find(',');
        note.over1 = std::string(c2!=std::string_view::npos ? tr(ids2.substr(0,c2)) : ids2);
        note.over2 = c2!=std::string_view::npos ? std::string(tr(ids2.substr(c2+1))) : "";
      } else { note.over1 = std::string(spec); }
      ensure_part(note.over1); if(!note.over2.empty()) ensure_part(note.over2);
      int ni = (int)out.notes.size(); out.notes.push_back(note);
      out.events.push_back({SequenceDiagram::Event::T::Note, ni, "", "", ""});
      continue;
    }
    if(sw(ll,"activate "))  { std::string a=std::string(tr(line.substr(9))); ensure_part(a); out.events.push_back({SequenceDiagram::Event::T::Activate,-1,"","",a}); continue; }
    if(sw(ll,"deactivate ")){ std::string a=std::string(tr(line.substr(11)));ensure_part(a); out.events.push_back({SequenceDiagram::Event::T::Deactivate,-1,"","",a}); continue; }
    // group keywords
    for(auto kw : {"loop ","alt ","opt ","par ","break ","critical "}) {
      if(sw(ll,kw)) { out.events.push_back({SequenceDiagram::Event::T::GroupStart,-1,std::string(tr(line.substr(std::strlen(kw)))),std::string(kw).substr(0,std::strlen(kw)-1),""}); goto next_line; }
    }
    if(ll=="end") { out.events.push_back({SequenceDiagram::Event::T::GroupEnd,-1,"","",""}); continue; }
    // message arrows: A->>B: text  A->B  A-->B  A-->>B  A-xB  A-)B
    {
      std::string_view lhs, rhs; std::string lbl;
      if(split_arrow(line, lhs, rhs, lbl)) {
        std::string from=std::string(tr(lhs)), to=std::string(tr(rhs));
        // handle "+/-" activations
        if(!to.empty()&&to.back()=='+'){to.pop_back(); out.events.push_back({SequenceDiagram::Event::T::Activate,-1,"","",to});}
        if(!to.empty()&&to.back()=='-'){to.pop_back(); out.events.push_back({SequenceDiagram::Event::T::Deactivate,-1,"","",to});}
        ensure_part(from); ensure_part(to);
        // determine style from the arrow in line
        bool dotted = line.find("--") != std::string::npos;
        bool open   = line.find(">>")!=std::string::npos || line.find(">")!=std::string::npos;
        SeqMessage msg{from, to, lbl, dotted, open};
        int mi = (int)out.messages.size(); out.messages.push_back(msg);
        out.events.push_back({SequenceDiagram::Event::T::Message, mi,"","",""});
      }
    }
    next_line:;
  }
  return header && !out.participants.empty();
}

void render_sequence(const SequenceDiagram &d, int id)
{
  if(d.participants.empty()) return;
  ImGui::PushID(id);
  const float pw    = 110.0f, ph = 28.0f;
  const float hgap  = 24.0f;
  const float row_h = 28.0f;
  const int   np    = (int)d.participants.size();
  int msg_count = 0;
  for(auto &e : d.events) if(e.type==SequenceDiagram::Event::T::Message) msg_count++;

  float cw = np*(pw+hgap)+hgap;
  float ch = ph + (msg_count+2)*row_h + 16.0f + ph;

  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##seq", ImVec2(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 fill  = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord  = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 tcol  = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol  = ImGui::GetColorU32(ImGuiCol_TextDisabled);

  // participant boxes + lifelines
  std::vector<float> cx(np);
  for(int i=0;i<np;++i) {
    float x = orig.x + hgap + i*(pw+hgap);
    cx[i] = x + pw*0.5f;
    draw_box(dl, ImVec2(x, orig.y), ImVec2(pw,ph), fill, bord, d.participants[i].label);
    // bottom box
    draw_box(dl, ImVec2(x, orig.y+ch-ph), ImVec2(pw,ph), fill, bord, d.participants[i].label);
    // lifeline
    dl->AddLine(ImVec2(cx[i], orig.y+ph), ImVec2(cx[i], orig.y+ch-ph), lcol, 1.0f);
  }

  // auto map id → index
  auto part_idx = [&](const std::string &id_s) {
    for(int i=0;i<np;++i) if(d.participants[i].id==id_s) return i;
    return 0;
  };

  float y = orig.y + ph + 8.0f;
  int active_depth[32] = {};
  for(auto &e : d.events) {
    if(e.type==SequenceDiagram::Event::T::Message) {
      auto &m = d.messages[e.idx];
      int fi = part_idx(m.from), ti = part_idx(m.to);
      float x0 = cx[fi], x1 = cx[ti];
      float cy2 = y + row_h*0.5f;
      // activation box
      if(active_depth[fi]>0) { float ax=cx[fi]-4; dl->AddRectFilled(ImVec2(ax,y),ImVec2(ax+8,y+row_h),ImGui::GetColorU32(ImGuiCol_Button),0); }
      if(active_depth[ti]>0) { float ax=cx[ti]-4; dl->AddRectFilled(ImVec2(ax,y),ImVec2(ax+8,y+row_h),ImGui::GetColorU32(ImGuiCol_Button),0); }
      // line
      ImU32 ac = m.dotted ? lcol : tcol;
      if(m.dotted) { // dashed
        float dx = x1-x0; float len=std::abs(dx); float seg=6.0f;
        int n2=(int)(len/seg); for(int k=0;k<n2;k+=2){
          float t0=k*(dx/n2), t1=(k+1)*(dx/n2);
          dl->AddLine(ImVec2(x0+t0,cy2),ImVec2(x0+t1,cy2),lcol,1.5f);
        }
      } else dl->AddLine(ImVec2(x0,cy2),ImVec2(x1,cy2),tcol,1.5f);
      // arrowhead
      float dir = (x1>x0)?1.0f:-1.0f;
      draw_arrow_head(dl,ImVec2(x1,cy2),ImVec2(dir,0),8.0f,ac,m.open);
      // label
      if(!m.text.empty()) {
        ImVec2 ts=ImGui::CalcTextSize(m.text.c_str());
        float tx=std::min(x0,x1)+(std::abs(x1-x0)-ts.x)*0.5f;
        dl->AddText(ImVec2(tx, cy2-ts.y-2), tcol, m.text.c_str());
      }
      y += row_h;
    } else if(e.type==SequenceDiagram::Event::T::Note) {
      auto &n2 = d.notes[e.idx];
      int ni = part_idx(n2.over1);
      float nx = cx[ni]-pw*0.5f, nw=pw;
      if(!n2.over2.empty()) { int ni2=part_idx(n2.over2); float rx=cx[ni2]+pw*0.5f; nw=rx-nx; }
      float ncy = y;
      dl->AddRectFilled(ImVec2(nx,ncy),ImVec2(nx+nw,ncy+row_h),
        ImGui::GetColorU32(ImVec4(1,1,0.6f,0.25f)),3);
      dl->AddRect(ImVec2(nx,ncy),ImVec2(nx+nw,ncy+row_h),bord,3);
      ImVec2 ts=ImGui::CalcTextSize(n2.text.c_str());
      dl->AddText(ImVec2(nx+(nw-ts.x)*0.5f, ncy+(row_h-ts.y)*0.5f), tcol, n2.text.c_str());
      y += row_h;
    } else if(e.type==SequenceDiagram::Event::T::Activate) {
      int pi = part_idx(e.actor_id); if(pi<32) active_depth[pi]++;
    } else if(e.type==SequenceDiagram::Event::T::Deactivate) {
      int pi = part_idx(e.actor_id); if(pi<32&&active_depth[pi]>0) active_depth[pi]--;
    } else if(e.type==SequenceDiagram::Event::T::GroupStart) {
      // draw group label bar
      dl->AddRectFilled(ImVec2(orig.x,y),ImVec2(orig.x+cw,y+20),
        ImGui::GetColorU32(ImVec4(0.3f,0.5f,0.9f,0.18f)));
      std::string gt = e.group_type + (e.label.empty() ? "" : ": "+e.label);
      dl->AddText(ImVec2(orig.x+4,y+2), ImGui::GetColorU32(ImVec4(0.5f,0.7f,1,1)), gt.c_str());
      y += 20;
    } else if(e.type==SequenceDiagram::Event::T::GroupEnd) {
      dl->AddLine(ImVec2(orig.x,y),ImVec2(orig.x+cw,y),lcol,1.0f); y+=4;
    }
  }
  if(!d.title.empty()) {
    ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f, orig.y-ts.y-4), tcol, d.title.c_str());
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// CLASS DIAGRAM
// ═══════════════════════════════════════════════════════════════════════════
bool parse_class(std::string_view src, ClassDiagram &out)
{
  out = ClassDiagram{};
  std::unordered_map<std::string,int> cidx;
  auto ensure_cls = [&](const std::string &name) {
    auto it = cidx.find(name);
    if(it != cidx.end()) return it->second;
    ClassDef c; c.name = name; int n=(int)out.classes.size();
    out.classes.push_back(c); cidx[name]=n; return n;
  };
  Lines L{src}; std::string_view line; bool header=false;
  std::string cur_class;
  while(L.next(line)) {
    std::string ll=lc(line);
    if(!header){ if(sw(ll,"classdiagram")){ header=true; continue; } continue; }
    if(sw(ll,"class ")) {
      std::string_view rest=tr(line.substr(6));
      size_t brace=rest.find('{'); std::string name=std::string(brace!=std::string_view::npos?tr(rest.substr(0,brace)):rest);
      // check for <<stereotype>>
      size_t sq=name.find("<<"); if(sq!=std::string::npos) name=std::string(tr(name.substr(0,sq)));
      cur_class=name; ensure_cls(name);
      // annotation on same line e.g. class Foo { +field }
      continue;
    }
    if(line=="{") continue;
    if(line=="}") { cur_class=""; continue; }
    // stereotype e.g. <<interface>> inside class body
    if(!cur_class.empty()&&sw(tr(line),"<<")&&line.find(">>")!=std::string_view::npos) {
      size_t e2=line.find(">>"); out.classes[cidx[cur_class]].stereotype=std::string(tr(line.substr(line.find("<<")+2,e2-line.find("<<")-2)));
      continue;
    }
    // member line: +field type  or  +method() type
    if(!cur_class.empty()&&!line.empty()) {
      char vis='+'; std::string_view ml=line;
      if(!ml.empty()&&(ml[0]=='+'||ml[0]=='-'||ml[0]=='#'||ml[0]=='~')){ vis=ml[0]; ml=tr(ml.substr(1)); }
      bool is_m = ml.find('(')!=std::string_view::npos;
      ClassMember cm; cm.vis=vis; cm.is_method=is_m; cm.name=std::string(ml);
      out.classes[cidx[cur_class]].members.push_back(cm);
      continue;
    }
    // relation: A <|-- B  A *-- B  A --> B  A -- B : label
    {
      std::string_view lhs, rhs; std::string lbl;
      static const char *rels[]={"<|--","<|..","*--","o--","-->","..>","--",nullptr};
      size_t best=std::string_view::npos; size_t best_len=0; int best_ri=-1;
      for(int ri=0; rels[ri]; ++ri) {
        size_t p=line.find(rels[ri]);
        if(p!=std::string_view::npos&&(best==std::string_view::npos||p<best)){best=p;best_len=std::strlen(rels[ri]);best_ri=ri;}
      }
      if(best!=std::string_view::npos) {
        lhs=tr(line.substr(0,best)); rhs=tr(line.substr(best+best_len));
        size_t col=rhs.rfind(':');
        if(col!=std::string_view::npos){ lbl=std::string(tr(rhs.substr(col+1))); rhs=tr(rhs.substr(0,col)); }
        std::string f=std::string(lhs), t=std::string(rhs);
        if(f.empty()||t.empty()) continue;
        ensure_cls(f); ensure_cls(t);
        ClassRel::T rt=ClassRel::T::Link;
        if(best_ri==0||best_ri==1) rt=ClassRel::T::Inheritance;
        else if(best_ri==2) rt=ClassRel::T::Composition;
        else if(best_ri==3) rt=ClassRel::T::Aggregation;
        else if(best_ri==4) rt=ClassRel::T::Association;
        else if(best_ri==5) rt=ClassRel::T::Dependency;
        out.relations.push_back({f,t,lbl,"","",rt});
      }
    }
  }
  return header && !out.classes.empty();
}

void render_class(const ClassDiagram &d, int id)
{
  if(d.classes.empty()) return;
  ImGui::PushID(id);
  const float cw=150.0f, header_h=24.0f, row_h=18.0f, gap=40.0f;
  int nc=(int)d.classes.size();
  int cols=std::max(1,(int)std::ceil(std::sqrt((double)nc)));
  int rows=(nc+cols-1)/cols;
  float canvas_w=cols*(cw+gap)+gap, canvas_h=0;
  // compute heights
  std::vector<float> heights(nc);
  for(int i=0;i<nc;++i){
    heights[i]=header_h;
    auto &c=d.classes[i];
    if(!c.stereotype.empty()) heights[i]+=16;
    heights[i]+=c.members.size()*row_h+4;
    canvas_h=std::max(canvas_h,heights[i]);
  }
  canvas_h=rows*(canvas_h+gap)+gap;

  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##cls",ImVec2(canvas_w,canvas_h));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 fill=ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hfill=ImGui::GetColorU32(ImGuiCol_TitleBg);
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);

  // store center positions and half-heights for relation drawing
  std::vector<ImVec2> centers(nc);
  std::vector<float> half_h(nc);
  for(int i=0;i<nc;++i){
    auto [col,row]=grid_pos(i,cols);
    float x=orig.x+gap+col*(cw+gap), y=orig.y+gap+row*(heights[i]+gap);
    float h=heights[i];
    centers[i]=ImVec2(x+cw*0.5f,y+h*0.5f);
    half_h[i]=h*0.5f;
    // outer border
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+cw,y+h),fill,4);
    dl->AddRect(ImVec2(x,y),ImVec2(x+cw,y+h),bord,4);
    // header
    auto &c=d.classes[i];
    float hy=y;
    if(!c.stereotype.empty()){
      ImVec2 sts=ImGui::CalcTextSize(c.stereotype.c_str());
      dl->AddText(ImVec2(x+(cw-sts.x)*0.5f,hy+2),ImGui::GetColorU32(ImGuiCol_TextDisabled),c.stereotype.c_str());
      hy+=16;
    }
    dl->AddRectFilled(ImVec2(x,hy),ImVec2(x+cw,hy+header_h),hfill,0);
    ImVec2 ns=ImGui::CalcTextSize(c.name.c_str());
    dl->AddText(ImVec2(x+(cw-ns.x)*0.5f,hy+(header_h-ns.y)*0.5f),tcol,c.name.c_str());
    hy+=header_h;
    dl->AddLine(ImVec2(x,hy),ImVec2(x+cw,hy),bord);
    // members
    for(auto &m:c.members){
      char buf[128]; std::snprintf(buf,sizeof(buf),"%c %s",m.vis,m.name.c_str());
      dl->AddText(ImVec2(x+4,hy+2),tcol,buf);
      hy+=row_h;
    }
  }
  // relations — connect to box edges, not centers
  for(auto &r:d.relations){
    int fi=-1,ti=-1;
    for(int i=0;i<nc;++i){ if(d.classes[i].name==r.from) fi=i; if(d.classes[i].name==r.to) ti=i; }
    if(fi<0||ti<0) continue;
    ImVec2 a=rect_edge(centers[fi],cw*0.5f,half_h[fi],centers[ti]);
    ImVec2 b=rect_edge(centers[ti],cw*0.5f,half_h[ti],centers[fi]);
    ImU32 lc2=ImGui::GetColorU32(ImGuiCol_TextDisabled);
    dl->AddLine(a,b,lc2,1.5f);
    float dx=b.x-a.x,dy=b.y-a.y,len=std::sqrt(dx*dx+dy*dy);
    if(len>1){dx/=len;dy/=len;}
    draw_arrow_head(dl,b,ImVec2(dx,dy),8,lc2,r.type==ClassRel::T::Dependency);
    if(!r.label.empty()){
      ImVec2 mp((a.x+b.x)*0.5f,(a.y+b.y)*0.5f);
      ImVec2 ts=ImGui::CalcTextSize(r.label.c_str());
      dl->AddText(ImVec2(mp.x-ts.x*0.5f,mp.y-ts.y-2),tcol,r.label.c_str());
    }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// STATE DIAGRAM
// ═══════════════════════════════════════════════════════════════════════════
bool parse_state(std::string_view src, StateDiagram &out)
{
  out = StateDiagram{};
  std::unordered_map<std::string,int> sidx;
  auto ensure_state = [&](const std::string &id, const std::string &lbl="") {
    auto it=sidx.find(id);
    if(it!=sidx.end()) return it->second;
    StateNode s; s.id=id; s.label=lbl.empty()?id:lbl;
    s.is_start=(id=="[*]"&&out.states.empty());
    s.is_end=(id=="[*]"&&!out.states.empty());
    int n=(int)out.states.size(); out.states.push_back(s); sidx[id]=n; return n;
  };
  Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"statediagram")){header=true;continue;} continue;}
    if(ll=="state {"||ll=="}") continue;
    if(sw(ll,"state ")) {
      std::string_view rest=tr(line.substr(6));
      size_t as=lc(rest).find(" as "); std::string id2=std::string(as!=std::string_view::npos?tr(rest.substr(as+4)):rest);
      std::string lbl=std::string(as!=std::string_view::npos?tr(rest.substr(0,as)):rest);
      if(!id2.empty()) ensure_state(id2,lbl); continue;
    }
    // transition A --> B : label
    std::string_view lhs,rhs; std::string lbl2;
    if(split_arrow(line,lhs,rhs,lbl2)){
      std::string f=std::string(lhs),t=std::string(rhs);
      int fi=ensure_state(f),ti=ensure_state(t);
      // fix end state: second [*] occurrence
      if(t=="[*]"){ out.states[ti].is_end=true; out.states[ti].is_start=false; }
      out.transitions.push_back({f,t,lbl2});
      (void)fi;
    }
  }
  return header && !out.states.empty();
}

void render_state(const StateDiagram &d, int id)
{
  if(d.states.empty()) return;
  ImGui::PushID(id);
  const float sw2=110.0f,sh=30.0f,hgap=60.0f,vgap=20.0f;
  int n=(int)d.states.size();
  // simple linear layout in rows of 3
  int cols=std::min(3,n), rows=(n+cols-1)/cols;
  float cw=cols*(sw2+hgap)+hgap, ch=rows*(sh+vgap)+vgap+40;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##st",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 fill=ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);

  std::vector<ImVec2> centers(n);
  for(int i=0;i<n;++i){
    auto [col,row]=grid_pos(i,cols);
    float x=orig.x+hgap+col*(sw2+hgap), y=orig.y+40+row*(sh+vgap);
    centers[i]=ImVec2(x+sw2*0.5f,y+sh*0.5f);
    auto &s=d.states[i];
    if(s.is_start){
      dl->AddCircleFilled(centers[i],10,tcol);
    } else if(s.is_end){
      dl->AddCircleFilled(centers[i],10,tcol);
      dl->AddCircle(centers[i],14,tcol,0,2);
    } else {
      dl->AddRectFilled(ImVec2(x,y),ImVec2(x+sw2,y+sh),fill,12);
      dl->AddRect(ImVec2(x,y),ImVec2(x+sw2,y+sh),bord,12);
      ImVec2 ts=ImGui::CalcTextSize(s.label.c_str());
      dl->AddText(ImVec2(x+(sw2-ts.x)*0.5f,y+(sh-ts.y)*0.5f),tcol,s.label.c_str());
    }
  }
  // transitions — connect to shape edges
  auto find_idx=[&](const std::string &sid)->int{
    for(int i=0;i<n;++i) if(d.states[i].id==sid) return i;
    return -1;
  };
  auto state_edge=[&](int i, ImVec2 other)->ImVec2{
    if(d.states[i].is_start||d.states[i].is_end) return circ_edge(centers[i],10.0f,other);
    return rect_edge(centers[i],sw2*0.5f,sh*0.5f,other);
  };
  for(auto &t:d.transitions){
    int fi=find_idx(t.from),ti=find_idx(t.to);
    if(fi<0||ti<0) continue;
    ImVec2 a=state_edge(fi,centers[ti]), b=state_edge(ti,centers[fi]);
    float dx=b.x-a.x,dy=b.y-a.y,len=std::sqrt(dx*dx+dy*dy);
    if(len>1){dx/=len;dy/=len;}
    dl->AddLine(a,b,lcol,1.5f);
    draw_arrow_head(dl,b,ImVec2(dx,dy),8,lcol);
    if(!t.label.empty()){
      ImVec2 ts2=ImGui::CalcTextSize(t.label.c_str());
      dl->AddText(ImVec2((a.x+b.x)*0.5f-ts2.x*0.5f,(a.y+b.y)*0.5f-ts2.y-2),tcol,t.label.c_str());
    }
  }
  if(!d.states.empty()){
    // title row
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// ER DIAGRAM
// ═══════════════════════════════════════════════════════════════════════════
bool parse_er(std::string_view src, ERDiagram &out)
{
  out = ERDiagram{};
  std::unordered_map<std::string,int> eidx;
  auto ensure_entity=[&](const std::string &name){
    auto it=eidx.find(name);
    if(it!=eidx.end()) return it->second;
    EREntity e; e.name=name; int n=(int)out.entities.size();
    out.entities.push_back(e); eidx[name]=n; return n;
  };
  Lines L{src}; std::string_view line; bool header=false;
  std::string cur_entity;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"erdiagram")){header=true;continue;} continue;}
    if(line=="{") continue;
    if(line=="}"){cur_entity="";continue;}
    // relation:  ENTITY1 ||--o{ ENTITY2 : label
    // cardinality tokens: ||, |{, }|, o|, |o, o{, }o, |{, }|, ||--|{, ||--o{ etc.
    // simplest: look for spaces between words with -- in middle
    if(!cur_entity.empty()){
      // attribute line: type name [PK|FK]
      std::string_view parts=line;
      size_t sp=parts.find(' ');
      std::string type=sp!=std::string_view::npos?std::string(parts.substr(0,sp)):"field";
      std::string rest2=sp!=std::string_view::npos?std::string(tr(parts.substr(sp))):"";
      bool pk=lc(line).find("pk")!=std::string::npos, fk=lc(line).find("fk")!=std::string::npos;
      // remove PK/FK from name
      std::string name2=rest2;
      size_t pkp=lc(name2).find(" pk"); if(pkp!=std::string::npos)name2=name2.substr(0,pkp);
      size_t fkp=lc(name2).find(" fk"); if(fkp!=std::string::npos)name2=name2.substr(0,fkp);
      name2=std::string(tr(name2));
      // strip quotes
      name2=strip_quotes(name2);
      out.entities[eidx[cur_entity]].attrs.push_back({type,name2,pk,fk});
      continue;
    }
    // relation line: detect -- or ||
    if(line.find("--")!=std::string_view::npos||line.find("||")!=std::string_view::npos){
      // split at cardinality operators
      // find the two entity names (first token, last word before :)
      size_t col=line.rfind(':');
      std::string lbl2=col!=std::string_view::npos?std::string(tr(line.substr(col+1))):"";
      std::string body=col!=std::string_view::npos?std::string(tr(line.substr(0,col))):std::string(line);
      // find card1 end: first space
      size_t s1=body.find(' ');
      if(s1==std::string::npos) goto try_entity;
      std::string e1=body.substr(0,s1);
      // find card2 start: last space
      size_t s2=body.rfind(' ');
      if(s2==s1) goto try_entity;
      std::string e2=body.substr(s2+1);
      std::string card_str=std::string(tr(body.substr(s1,s2-s1)));
      ensure_entity(e1); ensure_entity(e2);
      out.relations.push_back({e1,e2,lbl2,"","",});
      continue;
    }
    try_entity:
    // entity name alone or "ENTITY {"
    {
      size_t brace=line.find('{');
      std::string ename=std::string(brace!=std::string_view::npos?tr(line.substr(0,brace)):line);
      if(!ename.empty()&&ename.find(' ')==std::string::npos){
        ensure_entity(ename); cur_entity=ename;
      }
    }
  }
  return header && !out.entities.empty();
}

void render_er(const ERDiagram &d, int id)
{
  if(d.entities.empty()) return;
  ImGui::PushID(id);
  const float ew=160.0f,header_h=24.0f,row_h=18.0f,hgap=50.0f,vgap=20.0f;
  int n=(int)d.entities.size();
  int cols=std::min(3,n), rows=(n+cols-1)/cols;
  // compute heights
  std::vector<float> heights(n);
  float maxh=0;
  for(int i=0;i<n;++i){
    heights[i]=header_h+4+d.entities[i].attrs.size()*row_h;
    maxh=std::max(maxh,heights[i]);
  }
  float cw=cols*(ew+hgap)+hgap, ch=rows*(maxh+vgap)+vgap;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##er",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 fill=ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hfill=ImGui::GetColorU32(ImGuiCol_TitleBg);
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);

  std::vector<ImVec2> centers(n);
  std::vector<float> er_half_h(n);
  for(int i=0;i<n;++i){
    auto [col,row]=grid_pos(i,cols);
    float x=orig.x+hgap+col*(ew+hgap), y=orig.y+vgap+row*(maxh+vgap);
    float h=heights[i];
    centers[i]=ImVec2(x+ew*0.5f,y+h*0.5f);
    er_half_h[i]=h*0.5f;
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+ew,y+h),fill,3);
    dl->AddRect(ImVec2(x,y),ImVec2(x+ew,y+h),bord,3);
    // header
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+ew,y+header_h),hfill,3);
    auto &e=d.entities[i];
    ImVec2 ns=ImGui::CalcTextSize(e.name.c_str());
    dl->AddText(ImVec2(x+(ew-ns.x)*0.5f,y+(header_h-ns.y)*0.5f),tcol,e.name.c_str());
    float ay=y+header_h+2;
    dl->AddLine(ImVec2(x,ay),ImVec2(x+ew,ay),bord);
    for(auto &a:e.attrs){
      char buf[128]; std::snprintf(buf,sizeof(buf),"%s%s %s",a.pk?"PK ":a.fk?"FK ":"",a.type.c_str(),a.name.c_str());
      ImU32 ac=a.pk?ImGui::GetColorU32(ImVec4(1,0.8f,0.3f,1)):tcol;
      dl->AddText(ImVec2(x+4,ay+2),ac,buf); ay+=row_h;
    }
  }
  // relations — connect to box edges
  auto find_idx=[&](const std::string &name)->int{
    for(int i=0;i<n;++i) if(d.entities[i].name==name) return i; return -1;
  };
  for(auto &r:d.relations){
    int fi=find_idx(r.e1),ti=find_idx(r.e2);
    if(fi<0||ti<0) continue;
    ImVec2 a=rect_edge(centers[fi],ew*0.5f,er_half_h[fi],centers[ti]);
    ImVec2 b=rect_edge(centers[ti],ew*0.5f,er_half_h[ti],centers[fi]);
    dl->AddLine(a,b,lcol,1.5f);
    if(!r.label.empty()){
      ImVec2 ts=ImGui::CalcTextSize(r.label.c_str());
      dl->AddText(ImVec2((a.x+b.x)*0.5f-ts.x*0.5f,(a.y+b.y)*0.5f-ts.y-2),tcol,r.label.c_str());
    }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// USER JOURNEY
// ═══════════════════════════════════════════════════════════════════════════
bool parse_journey(std::string_view src, JourneyDiagram &out)
{
  out=JourneyDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"journey")){header=true;continue;} continue;}
    if(sw(ll,"title "))  { out.title=std::string(tr(line.substr(6)));continue;}
    if(sw(ll,"section ")){ out.sections.push_back({std::string(tr(line.substr(8))),{}});continue;}
    // task line: "Task name: score: actor1, actor2"
    size_t c1=line.find(':');
    if(c1!=std::string_view::npos){
      std::string name=std::string(tr(line.substr(0,c1)));
      std::string_view rest2=tr(line.substr(c1+1));
      size_t c2=rest2.find(':');
      int score=3;
      std::vector<std::string> actors;
      if(c2!=std::string_view::npos){
        score=std::atoi(std::string(tr(rest2.substr(0,c2))).c_str());
        std::string_view ac=tr(rest2.substr(c2+1));
        std::string acs=std::string(ac);
        std::istringstream ss(acs); std::string tok;
        while(std::getline(ss,tok,',')) actors.push_back(std::string(tr(tok)));
      } else { score=std::atoi(std::string(rest2).c_str()); }
      if(out.sections.empty()) out.sections.push_back({"",{}});
      out.sections.back().tasks.push_back({name,score,actors});
    }
  }
  return header;
}

void render_journey(const JourneyDiagram &d, int id)
{
  ImGui::PushID(id);
  const float row_h=28.0f, label_w=120.0f, score_w=16.0f, gap=4.0f;
  int total_tasks=0; for(auto &s:d.sections) total_tasks+=(int)s.tasks.size();
  float cw=label_w+total_tasks*(score_w+gap)+gap+100;
  float ch=(d.sections.size()+1)*row_h+40;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##jrn",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  if(!d.title.empty()){
    ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y+4),tcol,d.title.c_str());
  }
  float y=orig.y+30; float x0=orig.x+label_w;
  int sec_idx=0;
  for(auto &sec:d.sections){
    float x=x0;
    // section label
    ImU32 sc=series_color(sec_idx++,0.7f);
    dl->AddRectFilled(ImVec2(orig.x,y),ImVec2(orig.x+label_w-4,y+row_h),sc,3);
    ImVec2 ls=ImGui::CalcTextSize(sec.name.c_str());
    dl->AddText(ImVec2(orig.x+2,y+(row_h-ls.y)*0.5f),tcol,sec.name.c_str());
    for(auto &t:sec.tasks){
      float bar_h=t.score*4.0f;
      float by=y+row_h-bar_h;
      dl->AddRectFilled(ImVec2(x,by),ImVec2(x+score_w,y+row_h),sc,2);
      ImVec2 ns=ImGui::CalcTextSize(t.name.c_str());
      // name vertical
      dl->AddText(ImVec2(x+(score_w-8)*0.5f,y+row_h+2),ImGui::GetColorU32(ImGuiCol_TextDisabled),std::to_string(t.score).c_str());
      x+=score_w+gap;
    }
    y+=row_h;
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// GANTT
// ═══════════════════════════════════════════════════════════════════════════
bool parse_gantt(std::string_view src, GanttDiagram &out)
{
  out=GanttDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  int day_counter=0;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"gantt")){header=true;continue;} continue;}
    if(sw(ll,"title "))      {out.title=std::string(tr(line.substr(6)));continue;}
    if(sw(ll,"dateformat ")||sw(ll,"axisformat ")||sw(ll,"todaymarker ")||
       sw(ll,"tickinterval ")||sw(ll,"weekday ")||sw(ll,"excludes ")) continue;
    if(sw(ll,"section ")){out.sections.push_back({std::string(tr(line.substr(8))),{}});continue;}
    // task: name :flags, start, end   or   name :flags, after id, dur
    size_t col=line.find(':');
    if(col==std::string_view::npos) continue;
    std::string tname=std::string(tr(line.substr(0,col)));
    std::string_view spec=tr(line.substr(col+1));
    GanttTask task; task.name=tname;
    // parse flags and fields separated by commas
    std::string spec_s=std::string(spec);
    std::istringstream ss2(spec_s); std::string tok; std::vector<std::string> parts2;
    while(std::getline(ss2,tok,',')) parts2.push_back(std::string(tr(tok)));
    int field=0;
    for(auto &p:parts2){
      std::string pl=lc(p);
      if(pl=="crit"){task.is_crit=true;continue;}
      if(pl=="milestone"){task.is_milestone=true;continue;}
      if(pl=="done"||pl=="active") continue;
      if(field==0){
        // id or after
        if(sw(pl,"after ")){task.after=std::string(tr(pl.substr(6)));field++;}
        else {task.id=p; field++;}
        continue;
      }
      // duration: Xd, Xw, Xh
      int val=std::atoi(p.c_str());
      if(val>0){task.dur=val;} else {task.start_day=day_counter;}
      field++;
    }
    if(task.start_day==0&&task.after.empty()) task.start_day=day_counter;
    day_counter+=task.dur;
    if(out.sections.empty()) out.sections.push_back({"",{}});
    out.sections.back().tasks.push_back(task);
  }
  return header;
}

void render_gantt(const GanttDiagram &d, int id)
{
  ImGui::PushID(id);
  // flatten tasks and compute total span
  struct FlatTask { std::string name; int start,dur; bool crit,milestone; int sec_idx; };
  std::vector<FlatTask> flat;
  std::unordered_map<std::string,int> id_end_day; // task_id -> end day
  int max_day=0;
  for(int si=0;si<(int)d.sections.size();++si){
    for(auto &t:d.sections[si].tasks){
      int start=t.start_day;
      if(!t.after.empty()){auto it=id_end_day.find(t.after);if(it!=id_end_day.end())start=it->second;}
      if(!t.id.empty()) id_end_day[t.id]=start+t.dur;
      flat.push_back({t.name,start,t.dur,t.is_crit,t.is_milestone,si});
      max_day=std::max(max_day,start+t.dur);
    }
  }
  if(max_day==0) max_day=10;
  const float label_w=130.0f,row_h=22.0f,axis_h=24.0f;
  const float bar_area=360.0f;
  float px_per_day=bar_area/max_day;
  float cw=label_w+bar_area+8, ch=axis_h+(flat.size()+d.sections.size())*row_h+8;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##gantt",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 bgcol=ImGui::GetColorU32(ImGuiCol_FrameBg);
  // title
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y),tcol,d.title.c_str());}
  float y=orig.y+20;
  // axis
  dl->AddLine(ImVec2(orig.x+label_w,y+axis_h),ImVec2(orig.x+label_w+bar_area,y+axis_h),lcol,1.5f);
  int tick_step=std::max(1,max_day/8);
  for(int t=0;t<=max_day;t+=tick_step){
    float tx=orig.x+label_w+t*px_per_day;
    dl->AddLine(ImVec2(tx,y+axis_h-4),ImVec2(tx,y+axis_h+4),lcol,1);
    char buf[16]; std::snprintf(buf,sizeof(buf),"%d",t);
    ImVec2 ts=ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(tx-ts.x*0.5f,y),lcol,buf);
  }
  y+=axis_h;
  int si=-1;
  for(auto &ft:flat){
    if(ft.sec_idx!=si){
      si=ft.sec_idx;
      const std::string &sname=d.sections[si].name;
      if(!sname.empty()){
        dl->AddRectFilled(ImVec2(orig.x,y),ImVec2(orig.x+cw,y+row_h),series_color(si,0.18f),0);
        dl->AddText(ImVec2(orig.x+2,y+(row_h-ImGui::GetTextLineHeight())*0.5f),tcol,sname.c_str());
        y+=row_h;
      }
    }
    float x0=orig.x+label_w+ft.start*px_per_day;
    float bw=ft.dur*px_per_day;
    ImU32 bc=ft.crit?ImGui::GetColorU32(ImVec4(0.9f,0.3f,0.3f,0.8f)):series_color(ft.sec_idx,0.75f);
    // label
    std::string lbl=ft.name.size()>16?ft.name.substr(0,15)+"…":ft.name;
    ImVec2 ls=ImGui::CalcTextSize(lbl.c_str());
    dl->AddText(ImVec2(orig.x+label_w-ls.x-4,y+(row_h-ls.y)*0.5f),tcol,lbl.c_str());
    if(ft.milestone){
      float mx=x0+bw*0.5f, my=y+row_h*0.5f;
      dl->AddTriangleFilled(ImVec2(mx,my-8),ImVec2(mx+8,my),ImVec2(mx-8,my),bc);
      dl->AddTriangleFilled(ImVec2(mx,my+8),ImVec2(mx+8,my),ImVec2(mx-8,my),bc);
    } else {
      dl->AddRectFilled(ImVec2(x0,y+3),ImVec2(x0+std::max(2.0f,bw),y+row_h-3),bc,3);
    }
    y+=row_h;
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// QUADRANT CHART
// ═══════════════════════════════════════════════════════════════════════════
bool parse_quadrant(std::string_view src, QuadrantDiagram &out)
{
  out=QuadrantDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"quadrantchart")){header=true;continue;} continue;}
    if(sw(ll,"title "))      {out.title=std::string(tr(line.substr(6)));continue;}
    if(sw(ll,"x-axis "))     { std::string_view r=tr(line.substr(8)); size_t ar=r.find("-->"); if(ar!=std::string_view::npos){out.x_low=strip_quotes(r.substr(0,ar));out.x_high=strip_quotes(tr(r.substr(ar+3)));}else out.x_low=std::string(r); continue;}
    if(sw(ll,"y-axis "))     { std::string_view r=tr(line.substr(8)); size_t ar=r.find("-->"); if(ar!=std::string_view::npos){out.y_low=strip_quotes(r.substr(0,ar));out.y_high=strip_quotes(tr(r.substr(ar+3)));}else out.y_low=std::string(r); continue;}
    if(sw(ll,"quadrant-1 ")){ out.q1=std::string(tr(line.substr(11)));continue;}
    if(sw(ll,"quadrant-2 ")){ out.q2=std::string(tr(line.substr(11)));continue;}
    if(sw(ll,"quadrant-3 ")){ out.q3=std::string(tr(line.substr(11)));continue;}
    if(sw(ll,"quadrant-4 ")){ out.q4=std::string(tr(line.substr(11)));continue;}
    // point: Name: [x, y]
    size_t col=line.find(':');
    if(col!=std::string_view::npos){
      std::string name=strip_quotes(line.substr(0,col));
      std::string_view coords=tr(line.substr(col+1));
      if(!coords.empty()&&coords[0]=='['){
        size_t ce=coords.find(']'); if(ce!=std::string_view::npos){
          std::string cs=std::string(coords.substr(1,ce-1));
          size_t comma=cs.find(',');
          if(comma!=std::string::npos){
            float x=std::strtof(cs.substr(0,comma).c_str(),nullptr);
            float y=std::strtof(cs.substr(comma+1).c_str(),nullptr);
            out.points.push_back({name,x,y});
          }
        }
      }
    }
  }
  return header;
}

void render_quadrant(const QuadrantDiagram &d, int id)
{
  ImGui::PushID(id);
  const float sz=260.0f,pad=40.0f;
  float cw=sz+pad*2, ch=sz+pad*2+20;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##quad",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 gc=ImGui::GetColorU32(ImGuiCol_Separator);
  // title
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y+2),tcol,d.title.c_str());}
  ImVec2 tl(orig.x+pad,orig.y+20+pad);
  ImVec2 br(tl.x+sz,tl.y+sz);
  ImVec2 mid(tl.x+sz*0.5f,tl.y+sz*0.5f);
  // quadrant fills
  dl->AddRectFilled(tl,mid,ImGui::GetColorU32(ImVec4(0.2f,0.6f,0.2f,0.12f)));
  dl->AddRectFilled(ImVec2(mid.x,tl.y),ImVec2(br.x,mid.y),ImGui::GetColorU32(ImVec4(0.6f,0.2f,0.2f,0.12f)));
  dl->AddRectFilled(ImVec2(tl.x,mid.y),mid,ImGui::GetColorU32(ImVec4(0.2f,0.2f,0.6f,0.12f)));
  dl->AddRectFilled(mid,br,ImGui::GetColorU32(ImVec4(0.6f,0.6f,0.2f,0.12f)));
  dl->AddRect(tl,br,gc,0,0,1.5f);
  dl->AddLine(ImVec2(mid.x,tl.y),ImVec2(mid.x,br.y),gc,1.0f);
  dl->AddLine(ImVec2(tl.x,mid.y),ImVec2(br.x,mid.y),gc,1.0f);
  // quadrant labels
  auto ql=[&](float x,float y,const std::string &s){ if(!s.empty()){ ImVec2 ts=ImGui::CalcTextSize(s.c_str()); dl->AddText(ImVec2(x-ts.x*0.5f,y-ts.y*0.5f),lcol,s.c_str()); }};
  ql(tl.x+sz*0.25f,tl.y+sz*0.25f,d.q2);
  ql(tl.x+sz*0.75f,tl.y+sz*0.25f,d.q1);
  ql(tl.x+sz*0.25f,tl.y+sz*0.75f,d.q3);
  ql(tl.x+sz*0.75f,tl.y+sz*0.75f,d.q4);
  // axis labels
  if(!d.x_low.empty()){ImVec2 ts=ImGui::CalcTextSize(d.x_low.c_str());dl->AddText(ImVec2(tl.x,br.y+4),lcol,d.x_low.c_str());}
  if(!d.x_high.empty()){ImVec2 ts=ImGui::CalcTextSize(d.x_high.c_str());dl->AddText(ImVec2(br.x-ts.x,br.y+4),lcol,d.x_high.c_str());}
  if(!d.y_low.empty()){ImVec2 ts=ImGui::CalcTextSize(d.y_low.c_str());dl->AddText(ImVec2(tl.x-ts.x-4,br.y-ts.y),lcol,d.y_low.c_str());}
  if(!d.y_high.empty()){ImVec2 ts=ImGui::CalcTextSize(d.y_high.c_str());dl->AddText(ImVec2(tl.x-ts.x-4,tl.y),lcol,d.y_high.c_str());}
  // points
  for(int i=0;i<(int)d.points.size();++i){
    auto &p=d.points[i];
    float px=tl.x+p.x*sz, py=br.y-p.y*sz;
    ImU32 pc=series_color(i);
    dl->AddCircleFilled(ImVec2(px,py),5,pc);
    dl->AddText(ImVec2(px+7,py-8),tcol,p.name.c_str());
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// REQUIREMENT DIAGRAM
// ═══════════════════════════════════════════════════════════════════════════
bool parse_requirement(std::string_view src, RequirementDiagram &out)
{
  out=RequirementDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  std::string cur_block_type, cur_name;
  Requirement cur_req; ReqElement cur_elem; bool in_req=false,in_elem=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"requirementdiagram")){header=true;continue;} continue;}
    if(line=="}"){
      if(in_req){out.reqs.push_back(cur_req); in_req=false;}
      if(in_elem){out.elements.push_back(cur_elem); in_elem=false;}
      continue;
    }
    if(in_req){
      if(sw(ll,"id: "))          cur_req.id=std::string(tr(line.substr(4)));
      else if(sw(ll,"text: "))   cur_req.text=std::string(tr(line.substr(6)));
      else if(sw(ll,"risk: "))   cur_req.risk=std::string(tr(line.substr(6)));
      else if(sw(ll,"verifymethod: ")||sw(ll,"verify: ")) cur_req.method=std::string(tr(line.substr(ll.find(':')+2)));
      continue;
    }
    if(in_elem){
      if(sw(ll,"type: "))   cur_elem.type=std::string(tr(line.substr(6)));
      else if(sw(ll,"docref: ")) cur_elem.docref=std::string(tr(line.substr(8)));
      continue;
    }
    // relation: A - satisfies -> B
    if(line.find(" - ")!=std::string_view::npos&&line.find(" -> ")!=std::string_view::npos){
      size_t d1=line.find(" - "), d2=line.find(" -> ");
      std::string f=std::string(tr(line.substr(0,d1)));
      std::string rel=std::string(tr(line.substr(d1+3,d2-d1-3)));
      std::string t=std::string(tr(line.substr(d2+4)));
      out.relations.push_back({f,t,rel}); continue;
    }
    // block start: requirementType name {
    for(auto kw:{"requirement","functionalrequirement","interfacerequirement","performancerequirement","physicalrequirement","designconstraint"}){
      if(sw(ll,kw)&&(ll.size()==std::strlen(kw)||ll[std::strlen(kw)]==' ')){
        cur_req=Requirement{}; cur_req.type=kw;
        std::string_view rest2=tr(line.substr(std::strlen(kw)));
        size_t brace=rest2.find('{'); cur_req.name=std::string(brace!=std::string_view::npos?tr(rest2.substr(0,brace)):rest2);
        in_req=true; goto next_rq;
      }
    }
    if(sw(ll,"element ")){
      cur_elem=ReqElement{}; std::string_view rest2=tr(line.substr(8));
      size_t brace=rest2.find('{'); cur_elem.name=std::string(brace!=std::string_view::npos?tr(rest2.substr(0,brace)):rest2);
      in_elem=true;
    }
    next_rq:;
  }
  return header && (!out.reqs.empty()||!out.elements.empty());
}

void render_requirement(const RequirementDiagram &d, int id)
{
  ImGui::PushID(id);
  const float bw=180.0f,bh=80.0f,hgap=40.0f,vgap=20.0f;
  int n=(int)d.reqs.size()+(int)d.elements.size();
  if(n==0){ImGui::Text("(empty requirement diagram)");ImGui::PopID();return;}
  int cols=std::min(3,n), rows=(n+cols-1)/cols;
  float cw=cols*(bw+hgap)+hgap, ch=rows*(bh+vgap)+vgap;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##req",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 fill=ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 hfill=ImGui::GetColorU32(ImGuiCol_TitleBg);

  std::vector<ImVec2> centers(n);
  std::vector<std::string> node_names(n);
  int idx=0;
  auto draw_node=[&](int i, const std::string &type, const std::string &name, const std::string &detail){
    auto [col,row]=grid_pos(i,cols);
    float x=orig.x+hgap+col*(bw+hgap), y=orig.y+vgap+row*(bh+vgap);
    centers[i]=ImVec2(x+bw*0.5f,y+bh*0.5f); node_names[i]=name;
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+bw,y+bh),fill,4);
    dl->AddRect(ImVec2(x,y),ImVec2(x+bw,y+bh),bord,4);
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+bw,y+20),hfill,4);
    ImVec2 ts=ImGui::CalcTextSize(type.c_str()); dl->AddText(ImVec2(x+(bw-ts.x)*0.5f,y+2),lcol,type.c_str());
    ImVec2 ns=ImGui::CalcTextSize(name.c_str()); dl->AddText(ImVec2(x+(bw-ns.x)*0.5f,y+22),tcol,name.c_str());
    if(!detail.empty()){ std::string d2=detail.size()>22?detail.substr(0,21)+"…":detail; ImVec2 ds=ImGui::CalcTextSize(d2.c_str()); dl->AddText(ImVec2(x+(bw-ds.x)*0.5f,y+42),lcol,d2.c_str()); }
  };
  for(auto &r:d.reqs) { draw_node(idx,r.type,r.name,r.text); idx++; }
  for(auto &e:d.elements) { draw_node(idx,"element",e.name,e.type); idx++; }
  // relations
  auto find_node=[&](const std::string &name)->int{
    for(int i=0;i<n;++i) if(node_names[i]==name) return i; return -1;
  };
  for(auto &r:d.relations){
    int fi=find_node(r.from),ti=find_node(r.to);
    if(fi<0||ti<0) continue;
    ImVec2 a=rect_edge(centers[fi],bw*0.5f,bh*0.5f,centers[ti]);
    ImVec2 b=rect_edge(centers[ti],bw*0.5f,bh*0.5f,centers[fi]);
    dl->AddLine(a,b,lcol,1.5f);
    float dx=b.x-a.x,dy=b.y-a.y,len=std::sqrt(dx*dx+dy*dy);
    if(len>1){dx/=len;dy/=len;}
    draw_arrow_head(dl,b,ImVec2(dx,dy),8,lcol);
    if(!r.reltype.empty()){
      ImVec2 ts=ImGui::CalcTextSize(r.reltype.c_str());
      dl->AddText(ImVec2((a.x+b.x)*0.5f-ts.x*0.5f,(a.y+b.y)*0.5f-ts.y-2),tcol,r.reltype.c_str());
    }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// GIT GRAPH
// ═══════════════════════════════════════════════════════════════════════════
bool parse_git(std::string_view src, GitDiagram &out)
{
  out=GitDiagram{}; out.main_branch="main";
  Lines L{src}; std::string_view line; bool header=false;
  std::string cur_branch="main";
  out.branches.push_back("main");
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){
      if(sw(ll,"gitgraph")){
        header=true;
        // check for branch name after gitgraph
        std::string_view rest=tr(line.substr(9<line.size()?9:line.size()));
        if(!rest.empty()) { out.main_branch=std::string(rest); out.branches[0]=out.main_branch; cur_branch=out.main_branch; }
        continue;
      }
      continue;
    }
    if(sw(ll,"commit")){
      GitCommit c; c.branch=cur_branch;
      size_t id_pos=ll.find("id:"); if(id_pos!=std::string::npos){ std::string_view rest2=tr(line.substr(id_pos+3)); c.id=strip_quotes(rest2.substr(0,rest2.find_first_of(" ,\"")==std::string_view::npos?rest2.size():rest2.find_first_of(" ,\""))); }
      size_t tag_pos=ll.find("tag:"); if(tag_pos!=std::string::npos){ std::string_view rest2=tr(line.substr(tag_pos+4)); c.tag=strip_quotes(rest2.substr(0,rest2.find_first_of(" ,\"")==std::string_view::npos?rest2.size():rest2.find_first_of(" ,\""))); }
      if(ll.find("type:reverse")!=std::string::npos) c.type=GitCommit::T::Reverse;
      if(ll.find("type:highlight")!=std::string::npos) c.type=GitCommit::T::Highlight;
      out.commits.push_back(c); continue;
    }
    if(sw(ll,"branch ")){
      std::string bn=std::string(tr(line.substr(7)));
      if(std::find(out.branches.begin(),out.branches.end(),bn)==out.branches.end())
        out.branches.push_back(bn);
      cur_branch=bn; continue;
    }
    if(sw(ll,"checkout ")){cur_branch=std::string(tr(line.substr(9)));continue;}
    if(sw(ll,"merge ")){
      std::string from_b=std::string(tr(line.substr(6)));
      // find last commit on from_b and cur_branch, create merge commit
      GitCommit c; c.branch=cur_branch; c.is_merge=true; c.merge_from=from_b;
      out.commits.push_back(c); continue;
    }
    if(sw(ll,"cherry-pick")){
      GitCommit c; c.branch=cur_branch; out.commits.push_back(c); continue;
    }
  }
  return header;
}

void render_git(const GitDiagram &d, int id)
{
  if(d.branches.empty()) return;
  ImGui::PushID(id);
  const float commit_r=8.0f,commit_gap=40.0f,branch_gap=36.0f,pad=20.0f;
  int nb=(int)d.branches.size();
  // count commits per branch for width
  std::unordered_map<std::string,int> b_commit_count;
  for(auto &c:d.commits) b_commit_count[c.branch]++;
  int max_commits=0; for(auto &p:b_commit_count) max_commits=std::max(max_commits,p.second+1);
  max_commits=std::max(max_commits,(int)d.commits.size()/std::max(1,nb)+2);
  float cw=pad*2+max_commits*commit_gap;
  float ch=pad*2+nb*branch_gap;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##git",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  // branch index map
  std::unordered_map<std::string,int> bidx;
  for(int i=0;i<nb;++i) bidx[d.branches[i]]=i;
  auto branch_y=[&](const std::string &b)->float{
    auto it=bidx.find(b); int bi=it!=bidx.end()?it->second:0;
    return orig.y+pad+bi*branch_gap;
  };
  // draw branch lines
  for(int i=0;i<nb;++i){
    float y=orig.y+pad+i*branch_gap;
    ImU32 bc=series_color(i);
    dl->AddLine(ImVec2(orig.x+pad,y),ImVec2(orig.x+pad+max_commits*commit_gap,y),bc,2.5f);
    // branch name
    dl->AddText(ImVec2(orig.x+2,y-8),bc,d.branches[i].c_str());
  }
  // draw commits
  std::unordered_map<std::string,ImVec2> last_pos_on_branch;
  std::unordered_map<std::string,int> b_idx_counter;
  std::unordered_map<std::string,ImVec2> commit_id_pos;
  for(auto &c:d.commits){
    int bi=bidx.count(c.branch)?bidx[c.branch]:0;
    int ci=b_idx_counter[c.branch]++;
    float x=orig.x+pad+(ci+1)*commit_gap;
    float y=branch_y(c.branch);
    ImVec2 pos(x,y);
    if(!c.id.empty()) commit_id_pos[c.id]=pos;
    ImU32 bc=series_color(bi);
    if(c.is_merge&&!c.merge_from.empty()){
      // draw merge line from last pos on merge_from branch
      auto it=last_pos_on_branch.find(c.merge_from);
      if(it!=last_pos_on_branch.end())
        dl->AddLine(it->second,pos,series_color(bidx.count(c.merge_from)?bidx[c.merge_from]:0),2.0f);
    }
    last_pos_on_branch[c.branch]=pos;
    ImU32 fc=c.type==GitCommit::T::Highlight?ImGui::GetColorU32(ImVec4(1,0.8f,0.2f,1)):bc;
    if(c.type==GitCommit::T::Reverse){
      dl->AddCircle(pos,commit_r,bc,0,2.5f);
      dl->AddCircleFilled(pos,commit_r-3,ImGui::GetColorU32(ImGuiCol_WindowBg));
    } else {
      dl->AddCircleFilled(pos,commit_r,fc);
      dl->AddCircle(pos,commit_r,ImGui::GetColorU32(ImGuiCol_Border),0,1.5f);
    }
    if(!c.id.empty()){
      ImVec2 ts=ImGui::CalcTextSize(c.id.c_str()); dl->AddText(ImVec2(x-ts.x*0.5f,y+commit_r+2),tcol,c.id.c_str());
    }
    if(!c.tag.empty()){
      std::string t2="["+c.tag+"]"; ImVec2 ts=ImGui::CalcTextSize(t2.c_str());
      dl->AddText(ImVec2(x-ts.x*0.5f,y-commit_r-ts.y-2),ImGui::GetColorU32(ImVec4(1,0.85f,0.2f,1)),t2.c_str());
    }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// MINDMAP
// ═══════════════════════════════════════════════════════════════════════════
bool parse_mindmap(std::string_view src, MindmapDiagram &out)
{
  out=MindmapDiagram{}; IndentLines L{src}; std::string_view line; bool header=false; int indent=0;
  while(L.next(line,indent)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"mindmap")){header=true;continue;} continue;}
    int level=indent/2;
    std::string_view lbl=tr(line);
    // strip shape markers: ((text)), (text), [text], {{text}}, )text(
    if(lbl.size()>=4&&sw(lbl,"((")&&lbl.back()==')'&&lbl[lbl.size()-2]==')') lbl=lbl.substr(2,lbl.size()-4);
    else if(lbl.size()>=2&&lbl.front()=='('&&lbl.back()==')') lbl=lbl.substr(1,lbl.size()-2);
    else if(lbl.size()>=2&&lbl.front()=='['&&lbl.back()==']') lbl=lbl.substr(1,lbl.size()-2);
    else if(lbl.size()>=4&&sw(lbl,"{{")&&lbl.back()=='}'&&lbl[lbl.size()-2]=='}') lbl=lbl.substr(2,lbl.size()-4);
    // strip ::icon() annotations
    size_t icon=lbl.find("::icon("); if(icon!=std::string_view::npos) lbl=tr(lbl.substr(0,icon));
    // strip :::class
    size_t cls=lbl.find(":::"); if(cls!=std::string_view::npos) lbl=tr(lbl.substr(0,cls));
    MindNode node; node.label=std::string(lbl); node.level=level;
    int ni=(int)out.nodes.size();
    // find parent: last node with level-1
    node.parent=-1;
    for(int i=ni-1;i>=0;--i){ if(out.nodes[i].level==level-1){node.parent=i;out.nodes[i].children.push_back(ni);break;} }
    out.nodes.push_back(node);
  }
  return header && !out.nodes.empty();
}

void render_mindmap(const MindmapDiagram &d, int id)
{
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float node_r=44.0f,gap=14.0f;
  const int n=(int)d.nodes.size();
  // assign positions via recursive radial layout
  std::vector<ImVec2> pos(n,ImVec2(0,0));
  const float canvas_r=std::max(120.0f,(node_r+gap)*(n+1)*0.5f);
  // place root at center
  float cw=canvas_r*2+node_r*2+20, ch=canvas_r*2+node_r*2+20;
  ImVec2 center(cw*0.5f,ch*0.5f);
  pos[0]=center;
  // radial BFS placement
  std::vector<bool> placed(n,false); placed[0]=true;
  // count children of each node
  std::function<int(int)> subtree_size=[&](int ni)->int{
    int s=1; for(int c:d.nodes[ni].children) s+=subtree_size(c); return s;
  };
  std::function<void(int,float,float,float)> place=[&](int ni,float ax,float span,float dist){
    auto &children=d.nodes[ni].children;
    int total=0; for(int c:children) total+=subtree_size(c);
    float a=ax-span*0.5f;
    for(int c:children){
      int sz=subtree_size(c);
      float cspan=(total>0?(float)sz/(float)total:1.0f)*span;
      float ca=a+cspan*0.5f;
      pos[c]=ImVec2(pos[ni].x+std::cos(ca)*dist, pos[ni].y+std::sin(ca)*dist);
      place(c,ca,cspan,dist*0.75f);
      a+=cspan;
    }
  };
  place(0,0,2*kPi,canvas_r*0.7f);

  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##mm",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 fill=ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);

  // draw edges
  for(int i=0;i<n;++i){
    if(d.nodes[i].parent>=0){
      ImVec2 a(orig.x+pos[d.nodes[i].parent].x,orig.y+pos[d.nodes[i].parent].y);
      ImVec2 b(orig.x+pos[i].x,orig.y+pos[i].y);
      dl->AddLine(a,b,ImGui::GetColorU32(ImGuiCol_TextDisabled),1.5f);
    }
  }
  // draw nodes
  for(int i=0;i<n;++i){
    ImVec2 p(orig.x+pos[i].x,orig.y+pos[i].y);
    float r=(i==0)?24.0f:16.0f;
    ImU32 fc=series_color(d.nodes[i].level,0.7f);
    dl->AddCircleFilled(p,r,fc);
    dl->AddCircle(p,r,bord,0,1.5f);
    const std::string &lbl=d.nodes[i].label;
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    // truncate if too long
    std::string disp=lbl;
    if(ts.x>r*1.8f) { disp=lbl.substr(0,std::max(1,(int)(r*1.8f/ImGui::CalcTextSize("a").x)))+"."; ts=ImGui::CalcTextSize(disp.c_str()); }
    dl->AddText(ImVec2(p.x-ts.x*0.5f,p.y-ts.y*0.5f),tcol,disp.c_str());
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// TIMELINE
// ═══════════════════════════════════════════════════════════════════════════
bool parse_timeline(std::string_view src, TimelineDiagram &out)
{
  out=TimelineDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"timeline")){header=true;continue;} continue;}
    if(sw(ll,"title ")) { out.title=std::string(tr(line.substr(6)));continue;}
    if(sw(ll,"section ")) { /* treat section as period label */ out.periods.push_back({std::string(tr(line.substr(8))),{}}); continue; }
    // period lines: "2002 : event1 : event2 : event3"
    size_t first_col=line.find(':');
    if(first_col!=std::string_view::npos){
      std::string period=std::string(tr(line.substr(0,first_col)));
      // everything after first colon = events split by colon
      std::string rest2=std::string(tr(line.substr(first_col+1)));
      TLPeriod p; p.label=period;
      std::istringstream ss2(rest2); std::string tok;
      while(std::getline(ss2,tok,':')) { std::string ev=std::string(NoteCore::trim(tok)); if(!ev.empty()) p.events.push_back(ev); }
      out.periods.push_back(p);
    } else if(!line.empty()) {
      // bare period with no events (will be followed by indented events - not standard but let's handle)
      out.periods.push_back({std::string(line),{}});
    }
  }
  return header;
}

void render_timeline(const TimelineDiagram &d, int id)
{
  ImGui::PushID(id);
  const float period_w=100.0f,event_h=22.0f,period_h=32.0f,hgap=6.0f,pad=12.0f;
  int np=(int)d.periods.size();
  if(np==0){ImGui::Text("(empty timeline)");ImGui::PopID();return;}
  int max_events=0; for(auto &p:d.periods) max_events=std::max(max_events,(int)p.events.size());
  float cw=np*(period_w+hgap)+hgap+pad*2;
  float ch=pad*2+period_h+max_events*event_h+20;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##tl",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y+2),tcol,d.title.c_str());}
  // axis line
  float axis_y=orig.y+pad+20+period_h*0.5f;
  dl->AddLine(ImVec2(orig.x+pad,axis_y),ImVec2(orig.x+pad+np*(period_w+hgap),axis_y),lcol,2.0f);
  for(int i=0;i<np;++i){
    float x=orig.x+pad+i*(period_w+hgap);
    ImU32 pc=series_color(i,0.8f);
    // period box
    draw_box(dl,ImVec2(x,orig.y+pad+20),ImVec2(period_w,period_h),ImGui::GetColorU32(ImGuiCol_FrameBg),pc,d.periods[i].label);
    // connector tick from box bottom to axis
    dl->AddLine(ImVec2(x+period_w*0.5f,orig.y+pad+20+period_h),ImVec2(x+period_w*0.5f,axis_y),pc,1.5f);
    // events below
    float ey=orig.y+pad+20+period_h+4;
    for(auto &ev:d.periods[i].events){
      std::string short_ev=ev.size()>14?ev.substr(0,13)+"…":ev;
      draw_box(dl,ImVec2(x,ey),ImVec2(period_w,event_h-2),ImGui::GetColorU32(ImVec4(0,0,0,0)),pc,short_ev,2);
      ey+=event_h;
    }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// SANKEY
// ═══════════════════════════════════════════════════════════════════════════
bool parse_sankey(std::string_view src, SankeyDiagram &out)
{
  out=SankeyDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"sankey-beta")||sw(ll,"sankey")){header=true;continue;} continue;}
    // CSV: source,target,value
    std::string ls=std::string(line);
    size_t c1=ls.find(','); if(c1==std::string::npos) continue;
    size_t c2=ls.find(',',c1+1); if(c2==std::string::npos) continue;
    std::string src2=std::string(tr(ls.substr(0,c1)));
    std::string tgt=std::string(tr(ls.substr(c1+1,c2-c1-1)));
    float val=std::strtof(ls.substr(c2+1).c_str(),nullptr);
    if(src2.empty()||tgt.empty()) continue;
    out.flows.push_back({src2,tgt,val});
  }
  return header && !out.flows.empty();
}

void render_sankey(const SankeyDiagram &d, int id)
{
  ImGui::PushID(id);
  // collect unique nodes in order
  std::vector<std::string> nodes;
  auto ensure_node=[&](const std::string &n){
    if(std::find(nodes.begin(),nodes.end(),n)==nodes.end()) nodes.push_back(n);
  };
  for(auto &f:d.flows){ ensure_node(f.source); ensure_node(f.target); }
  int nn=(int)nodes.size();
  if(nn==0){ImGui::Text("(empty sankey)");ImGui::PopID();return;}

  // node total flows
  std::vector<float> out_total(nn,0),in_total(nn,0);
  auto ni=[&](const std::string &n)->int{ for(int i=0;i<nn;++i) if(nodes[i]==n) return i; return 0; };
  for(auto &f:d.flows){ out_total[ni(f.source)]+=f.value; in_total[ni(f.target)]+=f.value; }

  // assign nodes to columns: BFS topo
  std::vector<int> col(nn,0);
  bool changed=true;
  while(changed){ changed=false; for(auto &f:d.flows){ int fi=ni(f.source),ti=ni(f.target); if(col[ti]<=col[fi]){col[ti]=col[fi]+1;changed=true;} } }
  int ncols=0; for(int c:col) ncols=std::max(ncols,c+1);

  const float nw=80.0f,nh_base=20.0f,col_gap=100.0f,pad=20.0f,max_nh=140.0f;
  float max_val=1.0f; for(float v:in_total) max_val=std::max(max_val,v); for(float v:out_total) max_val=std::max(max_val,v);
  float cw=ncols*(nw+col_gap)+pad*2;
  float ch=nn*60.0f+pad*2; ch=std::min(ch,400.0f);
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##sk",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);

  // place nodes in their columns
  std::vector<int> col_idx(ncols,0);
  std::vector<float> col_heights(ncols,0);
  std::vector<ImVec2> node_tl(nn), node_sz(nn);
  // sort nodes by column
  std::vector<int> order(nn); std::iota(order.begin(),order.end(),0);
  std::sort(order.begin(),order.end(),[&](int a,int b){return col[a]<col[b];});
  std::vector<float> col_y(ncols,0);
  for(int ci:order){
    float total=std::max(in_total[ci],out_total[ci]);
    float nh=std::max(nh_base,std::min(max_nh,total/max_val*max_nh));
    float x=orig.x+pad+col[ci]*(nw+col_gap);
    float y=orig.y+pad+col_y[col[ci]];
    node_tl[ci]=ImVec2(x,y); node_sz[ci]=ImVec2(nw,nh);
    col_y[col[ci]]+=nh+8;
  }
  // draw flows
  for(auto &f:d.flows){
    int fi=ni(f.source),ti=ni(f.target);
    float frac_s=out_total[fi]>0?f.value/max_val:0;
    float frac_t=in_total[ti]>0?f.value/max_val:0;
    float fw=std::max(1.5f,frac_s*20.0f);
    ImVec2 a(node_tl[fi].x+node_sz[fi].x, node_tl[fi].y+node_sz[fi].y*0.5f);
    ImVec2 b(node_tl[ti].x, node_tl[ti].y+node_sz[ti].y*0.5f);
    ImU32 fc=series_color(fi,0.4f);
    dl->AddBezierCubic(a,ImVec2((a.x+b.x)*0.5f,a.y),ImVec2((a.x+b.x)*0.5f,b.y),b,fc,(float)fw);
  }
  // draw nodes
  for(int i=0;i<nn;++i){
    ImU32 fc=series_color(i,0.8f);
    dl->AddRectFilled(node_tl[i],ImVec2(node_tl[i].x+node_sz[i].x,node_tl[i].y+node_sz[i].y),fc,3);
    dl->AddRect(node_tl[i],ImVec2(node_tl[i].x+node_sz[i].x,node_tl[i].y+node_sz[i].y),bord,3);
    ImVec2 ts=ImGui::CalcTextSize(nodes[i].c_str());
    dl->AddText(ImVec2(node_tl[i].x+(node_sz[i].x-ts.x)*0.5f,node_tl[i].y+(node_sz[i].y-ts.y)*0.5f),tcol,nodes[i].c_str());
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// XY CHART
// ═══════════════════════════════════════════════════════════════════════════
bool parse_xychart(std::string_view src, XYDiagram &out)
{
  out=XYDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"xychart-beta")||sw(ll,"xychart")){header=true;
      if(ll.find("horizontal")!=std::string::npos) out.horizontal=true; continue;} continue;}
    if(sw(ll,"title "))  { out.title=strip_quotes(line.substr(6));continue;}
    if(sw(ll,"x-axis ")){ std::string_view r=tr(line.substr(7));
      if(r.front()=='['){// categorical
        size_t e=r.find(']'); if(e!=std::string_view::npos){
          std::string inner=std::string(r.substr(1,e-1));
          std::istringstream ss2(inner); std::string tok;
          while(std::getline(ss2,tok,',')) out.x_labels.push_back(strip_quotes(tr(tok)));
        }
      } else { // range - just record min/max as labels
        size_t ar=r.find("-->"); if(ar!=std::string_view::npos){ out.x_labels.push_back(strip_quotes(r.substr(0,ar))); out.x_labels.push_back(strip_quotes(tr(r.substr(ar+3)))); }
      }
      continue;
    }
    if(sw(ll,"y-axis ")){ std::string_view r=tr(line.substr(7));
      size_t ar=r.find("-->"); if(ar!=std::string_view::npos){
        out.y_min=std::strtof(std::string(tr(r.substr(0,ar))).c_str(),nullptr);
        out.y_max=std::strtof(std::string(tr(r.substr(ar+3))).c_str(),nullptr);
        out.y_explicit=true;
      }
      continue;
    }
    // bar/line: bar [v1, v2, ...]  or  line [v1, v2, ...]
    bool is_bar=sw(ll,"bar "),is_line=sw(ll,"line ");
    if(is_bar||is_line){
      std::string_view r=tr(line.substr(is_bar?4:5));
      if(!r.empty()&&r.front()=='['){
        size_t e=r.find(']'); if(e!=std::string_view::npos){
          std::string inner=std::string(r.substr(1,e-1));
          XYSeries s; s.is_bar=is_bar;
          // optional label before bracket
          std::istringstream ss2(inner); std::string tok;
          while(std::getline(ss2,tok,',')){ float v=std::strtof(std::string(tr(tok)).c_str(),nullptr); s.data.push_back(v); }
          if(!s.data.empty()){
            if(!out.y_explicit){ for(float v:s.data){out.y_max=std::max(out.y_max,v);} }
            out.series.push_back(s);
          }
        }
      }
    }
  }
  return header && (!out.series.empty()||!out.x_labels.empty());
}

void render_xychart(const XYDiagram &d, int id)
{
  ImGui::PushID(id);
  const float axis_w=40.0f,axis_h=24.0f,pad=8.0f;
  int nc=(int)d.x_labels.size();
  if(nc==0&&!d.series.empty()) nc=(int)d.series[0].data.size();
  nc=std::max(nc,1);
  float bar_w=std::max(24.0f,300.0f/nc);
  float plot_w=nc*bar_w, plot_h=180.0f;
  float cw=axis_w+plot_w+pad*2, ch=plot_h+axis_h+pad*2+20;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##xy",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 gcol=ImGui::GetColorU32(ImGuiCol_Separator);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y),tcol,d.title.c_str());}
  float ox=orig.x+axis_w, oy=orig.y+20+pad;
  float range=d.y_max-d.y_min; if(range<=0) range=1.0f;
  // y grid lines
  for(int g=0;g<=4;++g){
    float gy=oy+plot_h*(1.0f-g*0.25f);
    dl->AddLine(ImVec2(ox,gy),ImVec2(ox+plot_w,gy),gcol,1.0f);
    float val=d.y_min+range*g*0.25f;
    char buf[16]; std::snprintf(buf,sizeof(buf),"%.0f",val);
    ImVec2 ts=ImGui::CalcTextSize(buf); dl->AddText(ImVec2(ox-ts.x-3,gy-ts.y*0.5f),lcol,buf);
  }
  // axes
  dl->AddLine(ImVec2(ox,oy),ImVec2(ox,oy+plot_h),tcol,1.5f);
  dl->AddLine(ImVec2(ox,oy+plot_h),ImVec2(ox+plot_w,oy+plot_h),tcol,1.5f);
  // series
  int ns=(int)d.series.size();
  for(int si=0;si<ns;++si){
    auto &s=d.series[si];
    ImU32 sc=series_color(si);
    std::vector<ImVec2> line_pts;
    for(int xi=0;xi<(int)s.data.size()&&xi<nc;++xi){
      float frac=(s.data[xi]-d.y_min)/range;
      float bx=ox+xi*bar_w;
      float by=oy+plot_h*(1.0f-frac);
      if(s.is_bar){
        float bw2=bar_w/ns-2.0f;
        float bx2=bx+si*bw2+1.0f;
        dl->AddRectFilled(ImVec2(bx2,by),ImVec2(bx2+bw2,oy+plot_h),series_color(si,0.7f),2);
      } else {
        line_pts.push_back(ImVec2(bx+bar_w*0.5f,by));
      }
    }
    if(!line_pts.empty()&&line_pts.size()>1)
      for(int k=0;k<(int)line_pts.size()-1;++k) dl->AddLine(line_pts[k],line_pts[k+1],sc,2.0f);
    // dots
    for(auto &p:line_pts) dl->AddCircleFilled(p,3,sc);
  }
  // x labels
  for(int xi=0;xi<nc&&xi<(int)d.x_labels.size();++xi){
    std::string lbl=d.x_labels[xi];
    if(lbl.size()>8) lbl=lbl.substr(0,7)+"…";
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    dl->AddText(ImVec2(ox+xi*bar_w+(bar_w-ts.x)*0.5f,oy+plot_h+3),lcol,lbl.c_str());
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// BLOCK DIAGRAM
// ═══════════════════════════════════════════════════════════════════════════
bool parse_block(std::string_view src, BlockDiagram &out)
{
  out=BlockDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  std::unordered_map<std::string,int> idx;
  auto ensure_node=[&](const std::string &id,const std::string &lbl,const std::string &shape){
    auto it=idx.find(id); if(it!=idx.end()) return it->second;
    int n=(int)out.nodes.size(); out.nodes.push_back({id,lbl.empty()?id:lbl,shape}); idx[id]=n; return n;
  };
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"block-beta")||sw(ll,"block")){header=true;continue;} continue;}
    if(sw(ll,"columns ")) { out.columns=std::atoi(std::string(tr(line.substr(8))).c_str()); continue; }
    // arrow relation
    std::string_view lhs,rhs; std::string lbl2;
    if(split_arrow(line,lhs,rhs,lbl2)){
      std::string f=std::string(lhs),t=std::string(rhs);
      ensure_node(f,"",""); ensure_node(t,"","");
      out.edges.push_back({f,t,lbl2}); continue;
    }
    // standalone node id["label"] or just id
    std::string_view l=line;
    size_t b1=l.find('['),b2=l.find(']');
    size_t p1=l.find('('),p2=l.find(')');
    if(b1!=std::string_view::npos&&b2!=std::string_view::npos){
      std::string id2=std::string(tr(l.substr(0,b1)));
      std::string lbl2b=strip_quotes(l.substr(b1+1,b2-b1-1));
      ensure_node(id2,lbl2b,"rect");
    } else if(p1!=std::string_view::npos&&p2!=std::string_view::npos){
      std::string id2=std::string(tr(l.substr(0,p1)));
      std::string lbl2b=strip_quotes(l.substr(p1+1,p2-p1-1));
      ensure_node(id2,lbl2b,"round");
    } else if(!line.empty()&&line.find(' ')==std::string_view::npos) {
      ensure_node(std::string(line),"","");
    }
  }
  return header && !out.nodes.empty();
}

void render_block(const BlockDiagram &d, int id)
{
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float nw=100.0f,nh=32.0f,hgap=40.0f,vgap=20.0f;
  int n=(int)d.nodes.size();
  int cols=d.columns>0?d.columns:std::min(4,n);
  int rows=(n+cols-1)/cols;
  float cw=cols*(nw+hgap)+hgap, ch=rows*(nh+vgap)+vgap;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##blk",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 fill=ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  std::vector<ImVec2> centers(n);
  for(int i=0;i<n;++i){
    auto [col,row]=grid_pos(i,cols);
    float x=orig.x+hgap+col*(nw+hgap), y=orig.y+vgap+row*(nh+vgap);
    centers[i]=ImVec2(x+nw*0.5f,y+nh*0.5f);
    float rounding=d.nodes[i].shape=="round"?nh*0.5f:4.0f;
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+nw,y+nh),fill,rounding);
    dl->AddRect(ImVec2(x,y),ImVec2(x+nw,y+nh),bord,rounding);
    ImVec2 ts=ImGui::CalcTextSize(d.nodes[i].label.c_str());
    dl->AddText(ImVec2(x+(nw-ts.x)*0.5f,y+(nh-ts.y)*0.5f),tcol,d.nodes[i].label.c_str());
  }
  auto find_idx=[&](const std::string &sid)->int{
    for(int i=0;i<n;++i) if(d.nodes[i].id==sid) return i; return -1;
  };
  for(auto &e:d.edges){
    int fi=find_idx(e.from),ti=find_idx(e.to);
    if(fi<0||ti<0) continue;
    ImVec2 a=rect_edge(centers[fi],nw*0.5f,nh*0.5f,centers[ti]);
    ImVec2 b=rect_edge(centers[ti],nw*0.5f,nh*0.5f,centers[fi]);
    dl->AddLine(a,b,lcol,1.5f);
    float dx=b.x-a.x,dy=b.y-a.y,len=std::sqrt(dx*dx+dy*dy);
    if(len>1){dx/=len;dy/=len;}
    draw_arrow_head(dl,b,ImVec2(dx,dy),8,lcol);
    if(!e.label.empty()){ImVec2 ts=ImGui::CalcTextSize(e.label.c_str());dl->AddText(ImVec2((a.x+b.x)*0.5f-ts.x*0.5f,(a.y+b.y)*0.5f-ts.y-2),tcol,e.label.c_str());}
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// PACKET
// ═══════════════════════════════════════════════════════════════════════════

// Parses %%{init: {'packet': {'bitWidth':20,'bitsPerRow':16,...}}}%% directives.
static void parse_packet_config(std::string_view src, PacketConfig &cfg)
{
  const std::string s(src);
  size_t pos = 0;
  while ((pos = s.find("%%{", pos)) != std::string::npos) {
    size_t end = s.find("%%", pos + 3);
    if (end == std::string::npos) break;
    const std::string dir = s.substr(pos + 3, end - pos - 3);
    size_t pk = dir.find("packet");
    if (pk != std::string::npos) {
      size_t colon = dir.find(':', pk + 6);
      size_t ob    = (colon != std::string::npos) ? dir.find('{', colon) : std::string::npos;
      size_t cb    = (ob != std::string::npos) ? dir.find('}', ob + 1) : std::string::npos;
      if (cb != std::string::npos) {
        const std::string inner = dir.substr(ob + 1, cb - ob - 1);
        auto gf = [&](const char *k, float &v) {
          size_t kp = inner.find(k); if (kp == std::string::npos) return;
          size_t cp = inner.find(':', kp + std::strlen(k)); if (cp == std::string::npos) return;
          float r = (float)std::atof(inner.c_str() + cp + 1); if (r > 0) v = r;
        };
        auto gi = [&](const char *k, int &v) {
          size_t kp = inner.find(k); if (kp == std::string::npos) return;
          size_t cp = inner.find(':', kp + std::strlen(k)); if (cp == std::string::npos) return;
          int r = std::atoi(inner.c_str() + cp + 1); if (r > 0) v = r;
        };
        auto gb = [&](const char *k, bool &v) {
          size_t kp = inner.find(k); if (kp == std::string::npos) return;
          size_t cp = inner.find(':', kp + std::strlen(k)); if (cp == std::string::npos) return;
          const std::string rest = inner.substr(cp + 1);
          size_t tp = rest.find("true"), fp2 = rest.find("false");
          if (tp != std::string::npos && (fp2 == std::string::npos || tp < fp2)) v = true;
          else if (fp2 != std::string::npos) v = false;
        };
        gf("bitWidth",   cfg.bitWidth);
        gf("rowHeight",  cfg.rowHeight);
        gi("bitsPerRow", cfg.bitsPerRow);
        gb("showBits",   cfg.showBits);
        gf("paddingX",   cfg.paddingX);
        gf("paddingY",   cfg.paddingY);
        gb("showLegend", cfg.showLegend);
      }
    }
    pos = end + 2;
  }
}

bool parse_packet(std::string_view src, PacketDiagram &out)
{
  out = PacketDiagram{};
  parse_packet_config(src, out.config);
  Lines L{src}; std::string_view line; bool header = false;
  int cur = 0;  // next available bit — used by implicit-start "+N" notation
  while (L.next(line)) {
    std::string ll = lc(line);
    if (!header) { if (sw(ll,"packet-beta") || sw(ll,"packet")) { header=true; continue; } continue; }
    if (sw(ll,"%%") || sw(ll,"//")) continue;  // skip comments and directives
    if (sw(ll,"title ")) { out.title = strip_quotes(line.substr(6)); continue; }
    size_t col = line.find(':'); if (col == std::string_view::npos) continue;
    std::string range_s = std::string(tr(line.substr(0, col)));
    std::string name    = strip_quotes(line.substr(col + 1));
    int start = 0, end = 0;
    size_t dash = range_s.find('-'), plus = range_s.find('+');
    if (dash != std::string::npos) {
      // "A-B" — explicit range
      start = std::atoi(range_s.substr(0, dash).c_str());
      end   = std::atoi(range_s.substr(dash + 1).c_str());
    } else if (plus == 0) {
      // "+N" — implicit start: begin at current position, length N
      start = cur;
      end   = cur + std::atoi(range_s.c_str() + 1) - 1;
    } else if (plus != std::string::npos) {
      // "A+N" — explicit start with length
      start = std::atoi(range_s.substr(0, plus).c_str());
      end   = start + std::atoi(range_s.substr(plus + 1).c_str()) - 1;
    } else {
      // "N" — single bit
      start = end = std::atoi(range_s.c_str());
    }
    cur = end + 1;
    out.total_bits = std::max(out.total_bits, end + 1);
    out.fields.push_back({start, end, name});
  }
  return header && !out.fields.empty();
}

// ── Interactive packet diagram helpers ───────────────────────────────────────

PendingEdit g_pending_edit;
bool        g_consumed_right_click = false;

static std::string serialize_packet(const PacketDiagram &d)
{
  const PacketConfig &c = d.config;
  std::ostringstream s;
  s << "%%{init: {'packet': {"
    << "'bitWidth': "    << (int)c.bitWidth
    << ", 'rowHeight': " << (int)c.rowHeight
    << ", 'bitsPerRow': "<< c.bitsPerRow
    << ", 'showBits': "  << (c.showBits   ? "true" : "false")
    << ", 'paddingX': "  << (int)c.paddingX
    << ", 'paddingY': "  << (int)c.paddingY
    << ", 'showLegend': "<< (c.showLegend ? "true" : "false")
    << "}}}%%\n"
    << "packet-beta\n";
  if (!d.title.empty()) s << "  title \"" << d.title << "\"\n";
  int cur = 0;
  for (auto &f : d.fields) {
    int bits = std::max(1, f.end - f.start + 1);
    s << "  " << cur << "-" << (cur + bits - 1) << ": \"" << f.name << "\"\n";
    cur += bits;
  }
  return s.str();
}

// Rebuild sequential start/end positions from each field's current bit count.
static void rebuild_bits(std::vector<PacketField> &fields)
{
  int cur = 0;
  for (auto &f : fields) {
    int bits = std::max(1, f.end - f.start + 1);
    f.start = cur; f.end = cur + bits - 1; cur = f.end + 1;
  }
}

struct PacketEditState {
  PacketConfig cfg_edit;                 // config being edited in the config popup
  bool         cfg_open     = false;
  int          ctx_fi       = -1;        // field right-clicked
  int          rename_fi    = -1;
  char         rename_buf[256] = {};
  bool         rename_focus = false;
  int          add_after    = -2;        // -2=closed, -1=prepend, >=0=insert after fi
  char         add_name[256] = {};
  int          add_bits     = 8;
  bool         add_focus    = false;
  bool         drag_active  = false;
  int          drag_fi      = -1;
  std::vector<PacketField> drag_fields;
  std::vector<int>         drag_colors; // original index per slot — keeps colors stable
};
static std::unordered_map<int, PacketEditState> s_pkt_states;

void render_packet(const PacketDiagram &d, int id)
{
  ImGui::PushID(id);
  auto &es = s_pkt_states[id];

  // Working fields — during drag we show the live-swapped order
  const std::vector<PacketField> &wf = es.drag_active ? es.drag_fields : d.fields;
  const int NF = (int)wf.size();

  const PacketConfig &cfg = d.config;
  const float bit_w = cfg.bitWidth, fh = cfg.rowHeight;
  const float px = cfg.paddingX, py = cfg.paddingY;
  const float hdr_h = cfg.showBits ? 16.0f : 0.0f;
  const float sq = 10.0f, lgap = 5.0f, igap = 10.0f;
  const float outer = 4.0f;

  int total  = std::max(d.total_bits, 1);
  int bpr    = std::max(1, cfg.bitsPerRow);
  int n_rows = (total + bpr - 1) / bpr;
  float cw   = (float)bpr * bit_w + outer * 2.0f;

  // ── Per-field metadata ───────────────────────────────────────────────────
  std::vector<bool> ext(NF, false);
  std::vector<int>  label_row(NF, 0);
  for (int fi = 0; fi < NF; ++fi) {
    auto &f = wf[fi];
    int best_seg = 0, best_r = 0;
    for (int row = 0; row < n_rows; ++row) {
      int rs = row * bpr, re = std::min(rs + bpr, total) - 1;
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if (fs <= fe) { int seg = fe - fs + 1; if (seg > best_seg) { best_seg = seg; best_r = row; } }
    }
    label_row[fi] = best_r;
    ext[fi] = ImGui::CalcTextSize(f.name.c_str()).x > (float)best_seg * bit_w - px - 4.0f;
  }
  bool any_ext = false;
  for (bool b : ext) if (b) { any_ext = true; break; }

  float legend_h = 0.0f;
  if (any_ext && cfg.showLegend) {
    float avail_w = cw - outer * 2.0f, lx_s = 0.0f; int lrows = 1;
    for (int fi = 0; fi < NF; ++fi) {
      if (!ext[fi]) continue;
      float iw = sq + lgap + ImGui::CalcTextSize(wf[fi].name.c_str()).x + igap;
      if (lx_s + iw > avail_w && lx_s > 0.0f) { lx_s = 0.0f; lrows++; }
      lx_s += iw;
    }
    legend_h = py + (float)lrows * 18.0f;
  }

  const float title_h  = d.title.empty() ? 0.0f : (py * 0.5f + 16.0f);
  const float row_step = hdr_h + fh + py;
  const float ch = py * 0.5f + title_h + (float)n_rows * row_step + legend_h;

  // ── Origin + vis_rect (defined before InvisibleButton so frects can be built) ─
  const ImVec2 orig = ImGui::GetCursorScreenPos();

  // vis_rect: (x, width) of the visual (inset) rect for a field segment [fs,fe]
  // within row [rs,re]. paddingX/2 inset on each non-edge side.
  auto vis_rect = [&](int rs, int re, int fs, int fe) -> std::pair<float,float> {
    float li = (fs == rs) ? 0.0f : px * 0.5f;
    float ri = (fe == re) ? 0.0f : px * 0.5f;
    float vfx = orig.x + outer + (float)(fs - rs) * bit_w + li;
    float vfw = (float)(fe - fs + 1) * bit_w - li - ri;
    if (vfw < 1.0f) { vfx -= li; vfw += li + ri; }
    return {vfx, vfw};
  };

  // Pre-compute field rects for hit-testing (hover + drag)
  struct FR { int fi; float x, y, w; };
  std::vector<FR> frects;
  frects.reserve(NF * n_rows);
  {
    const float yb0 = orig.y + py * 0.5f + title_h;
    for (int row = 0; row < n_rows; ++row) {
      int rs = row * bpr, re = std::min(rs + bpr, total) - 1;
      float fy0 = yb0 + (float)row * row_step + hdr_h;
      for (int fi = 0; fi < NF; ++fi) {
        int fs = std::max(wf[fi].start, rs), fe = std::min(wf[fi].end, re);
        if (fs > fe) continue;
        auto [vfx, vfw] = vis_rect(rs, re, fs, fe);
        frects.push_back({fi, vfx, fy0, vfw});
      }
    }
  }

  // ── InvisibleButton ───────────────────────────────────────────────────────
  ImGui::InvisibleButton("##pkt", ImVec2(cw, ch));
  const bool pkt_hovered = ImGui::IsItemHovered();
  const bool pkt_active  = ImGui::IsItemActive();

  // ── Hover + drag detection ────────────────────────────────────────────────
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  int hovered_fi = -1;
  for (auto &r : frects)
    if (mouse.x >= r.x && mouse.x < r.x + r.w && mouse.y >= r.y && mouse.y < r.y + fh)
      { hovered_fi = r.fi; break; }
  if (!pkt_hovered && !es.drag_active) hovered_fi = -1;

  // Drag start (require 5 px movement to distinguish from click)
  if (!es.drag_active && pkt_active && ImGui::IsMouseDragging(0, 5.0f)) {
    ImVec2 dp = ImGui::GetMouseDragDelta(0);
    ImVec2 pp = {mouse.x - dp.x, mouse.y - dp.y};
    for (auto &r : frects)
      if (pp.x >= r.x && pp.x < r.x + r.w && pp.y >= r.y && pp.y < r.y + fh)
        { es.drag_active = true; es.drag_fi = r.fi; es.drag_fields = d.fields;
          es.drag_colors.resize(d.fields.size()); std::iota(es.drag_colors.begin(), es.drag_colors.end(), 0); break; }
  }

  // Drag update / end
  if (es.drag_active) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (!ImGui::IsMouseDown(0)) {
      // Commit if any field name or bit count changed
      bool changed = false;
      for (int i = 0; i < (int)d.fields.size() && i < (int)es.drag_fields.size(); ++i)
        if (d.fields[i].name != es.drag_fields[i].name ||
            (d.fields[i].end - d.fields[i].start) != (es.drag_fields[i].end - es.drag_fields[i].start))
          { changed = true; break; }
      if (changed) {
        PacketDiagram nd = d; nd.fields = es.drag_fields;
        nd.total_bits = 0; for (auto &f : nd.fields) nd.total_bits = std::max(nd.total_bits, f.end + 1);
        g_pending_edit = {id, serialize_packet(nd)};
      }
      es.drag_active = false; es.drag_fi = -1; es.drag_fields.clear(); es.drag_colors.clear();
    } else if (hovered_fi >= 0 && hovered_fi != es.drag_fi &&
               hovered_fi < (int)es.drag_fields.size()) {
      // Swap entire field (name + bit count) and its color slot, then recompute positions
      std::swap(es.drag_fields[es.drag_fi], es.drag_fields[hovered_fi]);
      if (es.drag_fi < (int)es.drag_colors.size() && hovered_fi < (int)es.drag_colors.size())
        std::swap(es.drag_colors[es.drag_fi], es.drag_colors[hovered_fi]);
      rebuild_bits(es.drag_fields);
      es.drag_fi = hovered_fi;
    }
  } else if (hovered_fi >= 0) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }

  // Stable color index: during drag, map current slot back to original index
  // so colors travel with the field, not with the slot.
  auto ci = [&](int fi) -> int {
    return (es.drag_active && fi < (int)es.drag_colors.size()) ? es.drag_colors[fi] : fi;
  };

  // ── Draw list + colors ────────────────────────────────────────────────────
  ImDrawList *dl   = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hcol = ImGui::GetColorU32(ImGuiCol_Text, 0.55f);
  ImVec4 lbg4 = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); lbg4.w = 0.88f;
  const ImU32 lbg = ImGui::GetColorU32(lbg4);

  float y_base = orig.y + py * 0.5f;

  // ── Title ─────────────────────────────────────────────────────────────────
  if (!d.title.empty()) {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, y_base), tcol, d.title.c_str());
    y_base += title_h;
  }

  // ── Rows ──────────────────────────────────────────────────────────────────
  for (int row = 0; row < n_rows; ++row) {
    int rs = row * bpr, re = std::min(rs + bpr, total) - 1;
    float ry = y_base + (float)row * row_step;
    float fy = ry + hdr_h;

    auto vr = [&](int fs, int fe) { return vis_rect(rs, re, fs, fe); };

    // Bit-number header — x aligned with the evenly-spaced tick grid
    if (cfg.showBits) {
      auto bit_hdr_x = [&](int bit) -> float {
        for (int fi2 = 0; fi2 < NF; ++fi2) {
          int fs2 = std::max(wf[fi2].start, rs), fe2 = std::min(wf[fi2].end, re);
          if (bit >= fs2 && bit <= fe2) {
            auto [vfx2, vfw2] = vr(fs2, fe2);
            return vfx2 + (float)(bit - fs2) / (float)(fe2 - fs2 + 1) * vfw2;
          }
        }
        return orig.x + outer + (float)(bit - rs) * bit_w;
      };
      for (int i = rs; i <= re; i += 8) {
        char buf[8]; std::snprintf(buf, sizeof(buf), "%d", i);
        dl->AddText(ImVec2(bit_hdr_x(i), ry), lcol, buf);
      }
    }

    // Pass 1 — fills + borders + hover/drag highlight
    for (int fi = 0; fi < NF; ++fi) {
      auto &f = wf[fi];
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if (fs > fe) continue;
      auto [vfx, vfw] = vr(fs, fe);
      dl->AddRectFilled(ImVec2(vfx, fy), ImVec2(vfx+vfw, fy+fh), series_color(ci(fi), 0.42f), 3.0f);
      dl->AddRect(ImVec2(vfx, fy), ImVec2(vfx+vfw, fy+fh), series_color(ci(fi), 0.80f), 3.0f, 0, 1.5f);
      if (fi == hovered_fi)
        dl->AddRect(ImVec2(vfx-1,fy-1), ImVec2(vfx+vfw+1,fy+fh+1), hcol, 4.0f, 0, 2.0f);
    }

    // Bit tick marks — evenly spaced within visual width so all bits are equal size
    for (int fi = 0; fi < NF; ++fi) {
      auto &f = wf[fi];
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if (fs > fe) continue;
      auto [vfx, vfw] = vr(fs, fe);
      int nbits = fe - fs + 1;
      for (int j = 0; j <= nbits; ++j) {
        float tx = vfx + (float)j / (float)nbits * vfw;
        dl->AddLine(ImVec2(tx, fy), ImVec2(tx, fy+fh), bord, 0.5f);
      }
    }

    // Pass 2 — labels (top layer, drawn over ticks)
    for (int fi = 0; fi < NF; ++fi) {
      auto &f = wf[fi];
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if (fs > fe) continue;
      auto [vfx, vfw] = vr(fs, fe);
      if (!ext[fi] && row == label_row[fi]) {
        ImVec2 ts = ImGui::CalcTextSize(f.name.c_str());
        float lx2 = vfx + (vfw - ts.x) * 0.5f, ly2 = fy + (fh - ts.y) * 0.5f;
        const float lp = 3.0f;
        dl->AddRectFilled(ImVec2(lx2-lp, ly2-lp), ImVec2(lx2+ts.x+lp, ly2+ts.y+lp), lbg, 2.5f);
        dl->AddText(ImVec2(lx2, ly2), tcol, f.name.c_str());
      } else if (ext[fi]) {
        dl->AddCircleFilled(ImVec2(vfx+vfw*0.5f, fy+fh-5.0f), 2.5f, series_color(ci(fi), 0.9f));
      }
    }
  }

  // ── Legend ────────────────────────────────────────────────────────────────
  if (any_ext && cfg.showLegend) {
    float lx = orig.x + outer, ly = y_base + (float)n_rows * row_step + py;
    float right_edge = orig.x + cw - outer;
    for (int fi = 0; fi < NF; ++fi) {
      if (!ext[fi]) continue;
      float iw = sq + lgap + ImGui::CalcTextSize(wf[fi].name.c_str()).x + igap;
      if (lx + iw > right_edge && lx > orig.x + outer) { lx = orig.x + outer; ly += 18.0f; }
      dl->AddRectFilled(ImVec2(lx, ly+2), ImVec2(lx+sq, ly+sq+2), series_color(ci(fi), 0.85f), 2.0f);
      dl->AddText(ImVec2(lx+sq+lgap, ly), tcol, wf[fi].name.c_str());
      lx += iw;
    }
  }

  // ── Hover tooltip ─────────────────────────────────────────────────────────
  if (pkt_hovered && hovered_fi >= 0 && !es.drag_active) {
    const auto &hf = d.fields[hovered_fi];
    int nbits = hf.end - hf.start + 1;
    ImGui::SetTooltip("%s\n[%d-%d]  %db", hf.name.c_str(), hf.start, hf.end, nbits);
  }

  // ── Context menu trigger (right-click) ────────────────────────────────────
  bool open_rename = false, open_add = false;
  if (pkt_hovered && !es.drag_active && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    g_consumed_right_click = true;
    if (hovered_fi >= 0) {
      es.ctx_fi = hovered_fi;
      ImGui::OpenPopup("##pkt_ctx");
    } else {
      es.cfg_edit = d.config;
      ImGui::OpenPopup("##pkt_cfg");
    }
  }

  // ── Field context menu ────────────────────────────────────────────────────
  if (ImGui::BeginPopup("##pkt_ctx")) {
    const int fi = es.ctx_fi;
    if (fi >= 0 && fi < (int)d.fields.size()) {
      char hdr[128];
      std::snprintf(hdr, sizeof(hdr), "%s  [%d-%d]  %db",
                    d.fields[fi].name.c_str(), d.fields[fi].start, d.fields[fi].end,
                    d.fields[fi].end - d.fields[fi].start + 1);
      ImGui::TextDisabled("%s", hdr);
      ImGui::Separator();
      if (ImGui::MenuItem("Rename...")) {
        es.rename_fi = fi;
        std::strncpy(es.rename_buf, d.fields[fi].name.c_str(), 255);
        es.rename_buf[255] = '\0';
        es.rename_focus = true;
        open_rename = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Expand +1 bit")) {
        PacketDiagram nd = d;
        nd.fields[fi].end++;
        for (int k = fi+1; k < (int)nd.fields.size(); ++k) { nd.fields[k].start++; nd.fields[k].end++; }
        nd.total_bits++;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      bool can_shrink = d.fields[fi].end > d.fields[fi].start;
      if (ImGui::MenuItem("Shrink -1 bit", nullptr, false, can_shrink)) {
        PacketDiagram nd = d;
        nd.fields[fi].end--;
        for (int k = fi+1; k < (int)nd.fields.size(); ++k) { nd.fields[k].start--; nd.fields[k].end--; }
        nd.total_bits--;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Add field before")) {
        es.add_after = fi - 1; es.add_bits = 8; es.add_name[0] = '\0'; es.add_focus = true;
        open_add = true;
      }
      if (ImGui::MenuItem("Add field after")) {
        es.add_after = fi; es.add_bits = 8; es.add_name[0] = '\0'; es.add_focus = true;
        open_add = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Delete field")) {
        PacketDiagram nd = d;
        nd.fields.erase(nd.fields.begin() + fi);
        rebuild_bits(nd.fields);
        nd.total_bits = nd.fields.empty() ? 0 : nd.fields.back().end + 1;
        g_pending_edit = {id, serialize_packet(nd)};
      }
    }
    ImGui::EndPopup();
  }
  // Open these AFTER EndPopup so they don't nest inside the context menu
  if (open_rename) ImGui::OpenPopup("##pkt_rename");
  if (open_add)    ImGui::OpenPopup("##pkt_add");

  // ── Config popup ──────────────────────────────────────────────────────────
  if (ImGui::BeginPopup("##pkt_cfg")) {
    ImGui::Text("Packet Config");
    ImGui::Separator();
    ImGui::SliderFloat("Bit Width",       &es.cfg_edit.bitWidth,   8.f, 80.f,  "%.0fpx");
    ImGui::SliderFloat("Row Height",      &es.cfg_edit.rowHeight,  16.f,120.f, "%.0fpx");
    ImGui::SliderInt  ("Bits / Row",      &es.cfg_edit.bitsPerRow, 4, 128);
    ImGui::SliderFloat("Padding X",       &es.cfg_edit.paddingX,   0.f, 40.f,  "%.0fpx");
    ImGui::SliderFloat("Padding Y",       &es.cfg_edit.paddingY,   0.f, 40.f,  "%.0fpx");
    ImGui::Checkbox   ("Show bit numbers",&es.cfg_edit.showBits);
    ImGui::Checkbox   ("Show legend",     &es.cfg_edit.showLegend);
    ImGui::Separator();
    if (ImGui::Button("Apply")) {
      PacketDiagram nd = d; nd.config = es.cfg_edit;
      g_pending_edit = {id, serialize_packet(nd)};
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ── Rename modal ──────────────────────────────────────────────────────────
  if (ImGui::BeginPopupModal("##pkt_rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Rename field:");
    if (es.rename_focus) { ImGui::SetKeyboardFocusHere(); es.rename_focus = false; }
    bool ok = ImGui::InputText("##rn", es.rename_buf, sizeof(es.rename_buf),
                               ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    if (ImGui::Button("OK") || ok) {
      if (es.rename_fi >= 0 && es.rename_fi < (int)d.fields.size() && es.rename_buf[0]) {
        PacketDiagram nd = d;
        nd.fields[es.rename_fi].name = es.rename_buf;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ── Add field modal ───────────────────────────────────────────────────────
  if (ImGui::BeginPopupModal("##pkt_add", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("New field:");
    if (es.add_focus) { ImGui::SetKeyboardFocusHere(); es.add_focus = false; }
    ImGui::InputText("Name##an", es.add_name, sizeof(es.add_name));
    ImGui::InputInt ("Bits##ab", &es.add_bits);
    if (es.add_bits < 1) es.add_bits = 1;
    ImGui::Spacing();
    if (ImGui::Button("Add")) {
      if (es.add_name[0]) {
        PacketDiagram nd = d;
        PacketField nf; nf.name = es.add_name; nf.start = 0; nf.end = es.add_bits - 1;
        int ins = std::max(0, std::min(es.add_after + 1, (int)nd.fields.size()));
        nd.fields.insert(nd.fields.begin() + ins, nf);
        rebuild_bits(nd.fields);
        nd.total_bits = nd.fields.empty() ? 0 : nd.fields.back().end + 1;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// KANBAN
// ═══════════════════════════════════════════════════════════════════════════
bool parse_kanban(std::string_view src, KanbanDiagram &out)
{
  out=KanbanDiagram{}; IndentLines L{src}; std::string_view line; bool header=false;
  int indent=0, col_indent=-1;
  while(L.next(line,indent)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"kanban")){header=true;continue;} continue;}
    if(line=="{"||line=="}") continue;
    if(!line.empty()&&line.front()=='@') continue;
    // Parse optional ": description" suffix after the closing bracket
    std::string_view item_part = line;
    std::string desc;
    size_t rb = line.rfind(']');
    if(rb != std::string_view::npos) {
      size_t col = line.find(':', rb+1);
      if(col != std::string_view::npos) {
        desc = std::string(tr(line.substr(col+1)));
        item_part = line.substr(0, rb+1);
      }
    }
    size_t b1=item_part.find('['),b2=item_part.find(']');
    std::string id2,lbl;
    if(b1!=std::string_view::npos&&b2!=std::string_view::npos){
      id2=std::string(tr(item_part.substr(0,b1))); lbl=std::string(tr(item_part.substr(b1+1,b2-b1-1)));
    } else {
      id2=std::string(item_part); lbl=id2;
    }
    if(id2.empty()) continue;
    // First item establishes the column indent level
    if(col_indent<0) col_indent=indent;
    if(indent<=col_indent) {
      out.columns.push_back({id2,lbl,{}});
    } else {
      if(!out.columns.empty()) out.columns.back().cards.push_back({id2,lbl,desc});
    }
  }
  return header && !out.columns.empty();
}

// ── Kanban interactive state ──────────────────────────────────────────────────
struct KanbanEditState {
  // drag
  bool drag_active = false;
  int  drag_ci     = -1;   // source column
  int  drag_ri     = -1;   // source card index
  std::vector<KanbanCol> work_cols; // mutable working copy
  int  drop_ci     = -1;   // hovered column during drag
  int  drop_ri     = -1;   // insertion index in drop column
  // right-click / edit popup
  int  ctx_ci      = -1;
  int  ctx_ri      = -1;
  char edit_label[256] = {};
  char edit_desc[512]  = {};
  bool edit_focus  = false;
};
static std::unordered_map<int,KanbanEditState> s_kb_states;

static std::string serialize_kanban(const KanbanDiagram &d)
{
  std::ostringstream s;
  s << "kanban\n";
  for(auto &col:d.columns){
    s << "  " << col.id << "[" << col.label << "]\n";
    for(auto &card:col.cards){
      s << "    " << card.id << "[" << card.label << "]";
      if(!card.description.empty()) s << ": " << card.description;
      s << "\n";
    }
  }
  return s.str();
}

void render_kanban(const KanbanDiagram &d, int id)
{
  ImGui::PushID(id);
  auto &es = s_kb_states[id];

  const float col_w=160.0f,card_h=32.0f,col_header_h=28.0f,hgap=10.0f,vgap=6.0f,pad=10.0f;

  // Use working copy during drag, else the immutable diagram
  const std::vector<KanbanCol> &cols = es.drag_active ? es.work_cols : d.columns;
  int nc=(int)cols.size();
  int max_cards=0; for(auto &c:cols) max_cards=std::max(max_cards,(int)c.cards.size());
  // height must accommodate drop indicator slot
  float cw=nc*(col_w+hgap)+hgap+pad*2;
  float col_body_h=(max_cards+2)*(card_h+vgap)+vgap;
  float ch=col_header_h+col_body_h+pad*2;

  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##kb",ImVec2(cw,ch));
  const bool kb_hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  const bool kb_active =ImGui::IsItemActive();
  ImDrawList *dl=ImGui::GetWindowDrawList();

  const ImU32 tcol  = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 fill  = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord  = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hcol  = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
  const ImVec2 mouse = ImGui::GetIO().MousePos;

  // ── Pre-compute card rects for hit testing ────────────────────────────────
  struct CardRect { int ci,ri; float x,y; };
  std::vector<CardRect> crects;
  for(int i=0;i<nc;++i){
    float x=orig.x+pad+i*(col_w+hgap);
    float cy=orig.y+pad+col_header_h+vgap;
    for(int j=0;j<(int)cols[i].cards.size();++j){
      crects.push_back({i,j,x+4,cy});
      cy+=card_h+vgap;
    }
  }

  // ── Hit test: which card is under mouse ───────────────────────────────────
  int hov_ci=-1,hov_ri=-1;
  if(kb_hovered||es.drag_active){
    for(auto &r:crects){
      if(mouse.x>=r.x&&mouse.x<r.x+(col_w-8)&&mouse.y>=r.y&&mouse.y<r.y+card_h){
        hov_ci=r.ci; hov_ri=r.ri; break;
      }
    }
  }

  // ── Drag start ────────────────────────────────────────────────────────────
  if(!es.drag_active && kb_active && ImGui::IsMouseDragging(0,5.0f)){
    ImVec2 dp=ImGui::GetMouseDragDelta(0);
    ImVec2 pp={mouse.x-dp.x,mouse.y-dp.y};
    for(auto &r:crects){
      if(pp.x>=r.x&&pp.x<r.x+(col_w-8)&&pp.y>=r.y&&pp.y<r.y+card_h){
        es.drag_active=true; es.drag_ci=r.ci; es.drag_ri=r.ri;
        es.work_cols=d.columns;
        es.drop_ci=r.ci; es.drop_ri=r.ri;
        break;
      }
    }
  }

  // ── Drag update: find drop target ─────────────────────────────────────────
  if(es.drag_active){
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    int new_drop_ci=-1, new_drop_ri=0;
    for(int i=0;i<nc;++i){
      float cx=orig.x+pad+i*(col_w+hgap);
      if(mouse.x>=cx&&mouse.x<cx+col_w){
        new_drop_ci=i;
        float body_top=orig.y+pad+col_header_h+vgap;
        int nj=(int)cols[i].cards.size();
        new_drop_ri=nj; // default: append at end
        for(int j=0;j<nj;++j){
          float cy=body_top+j*(card_h+vgap);
          if(mouse.y<cy+card_h*0.5f){ new_drop_ri=j; break; }
        }
        break;
      }
    }
    if(new_drop_ci>=0){ es.drop_ci=new_drop_ci; es.drop_ri=new_drop_ri; }

    // Drag end: commit
    if(!ImGui::IsMouseDown(0)){
      if(es.drop_ci>=0){
        // Build new diagram from work_cols with card moved
        KanbanDiagram nd = d;
        KanbanCard moved = nd.columns[es.drag_ci].cards[es.drag_ri];
        nd.columns[es.drag_ci].cards.erase(nd.columns[es.drag_ci].cards.begin()+es.drag_ri);
        int ins=es.drop_ri;
        if(es.drop_ci==es.drag_ci && es.drop_ri>es.drag_ri) --ins;
        ins=std::max(0,std::min(ins,(int)nd.columns[es.drop_ci].cards.size()));
        nd.columns[es.drop_ci].cards.insert(nd.columns[es.drop_ci].cards.begin()+ins,moved);
        g_pending_edit={id,serialize_kanban(nd)};
      }
      es.drag_active=false; es.drag_ci=-1; es.drag_ri=-1;
      es.drop_ci=-1; es.drop_ri=-1; es.work_cols.clear();
    }
  } else if(hov_ci>=0){
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }

  // ── Draw columns ─────────────────────────────────────────────────────────
  for(int i=0;i<nc;++i){
    float x=orig.x+pad+i*(col_w+hgap);
    float y=orig.y+pad;
    float body_top=y+col_header_h;
    ImU32 hc=series_color(i,0.6f);
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+col_w,y+col_header_h),hc,4);
    ImVec2 ls=ImGui::CalcTextSize(cols[i].label.c_str());
    dl->AddText(ImVec2(x+(col_w-ls.x)*0.5f,y+(col_header_h-ls.y)*0.5f),tcol,cols[i].label.c_str());
    dl->AddRectFilled(ImVec2(x,body_top),ImVec2(x+col_w,y+ch-pad*2),series_color(i,0.08f),0);
    dl->AddRect(ImVec2(x,y),ImVec2(x+col_w,y+ch-pad*2),bord,4);

    float cy=body_top+vgap;
    int nj=(int)cols[i].cards.size();
    for(int j=0;j<nj;++j){
      // Drop indicator: horizontal bar before this card
      if(es.drag_active && es.drop_ci==i && es.drop_ri==j){
        dl->AddRectFilled(ImVec2(x+4,cy-vgap*0.5f-1),ImVec2(x+col_w-4,cy-vgap*0.5f+1),series_color(i,0.9f),2);
      }
      bool is_dragging=(es.drag_active && i==es.drag_ci && j==es.drag_ri);
      bool is_hovered=(hov_ci==i && hov_ri==j && !es.drag_active);
      ImU32 card_fill = is_hovered ? hcol : fill;
      ImU32 card_bord = is_hovered ? series_color(i,0.8f) : bord;
      float alpha = is_dragging ? 0.35f : 1.0f;
      // Faded original slot while dragging
      if(is_dragging){
        dl->AddRectFilled(ImVec2(x+4,cy),ImVec2(x+col_w-4,cy+card_h),
          ImGui::ColorConvertFloat4ToU32({0.5f,0.5f,0.5f,0.15f}),3);
        dl->AddRect(ImVec2(x+4,cy),ImVec2(x+col_w-4,cy+card_h),bord,3,0,1.0f);
      } else {
        dl->AddRectFilled(ImVec2(x+4,cy),ImVec2(x+col_w-4,cy+card_h),card_fill,3);
        dl->AddRect(ImVec2(x+4,cy),ImVec2(x+col_w-4,cy+card_h),card_bord,3);
        const auto &card=cols[i].cards[j];
        std::string lbl=card.label.size()>18?card.label.substr(0,17)+"…":card.label;
        ImVec2 cs=ImGui::CalcTextSize(lbl.c_str());
        dl->AddText(ImVec2(x+4+(col_w-8-cs.x)*0.5f,cy+(card_h-cs.y)*0.5f),tcol,lbl.c_str());
        // description dot indicator
        if(!card.description.empty()){
          dl->AddCircleFilled(ImVec2(x+col_w-10,cy+card_h-8),3.0f,series_color(i,0.7f));
        }
      }
      cy+=card_h+vgap;
    }
    // Drop indicator at end of column
    if(es.drag_active && es.drop_ci==i && es.drop_ri>=nj){
      dl->AddRectFilled(ImVec2(x+4,cy-vgap*0.5f-1),ImVec2(x+col_w-4,cy-vgap*0.5f+1),series_color(i,0.9f),2);
    }
  }

  // ── Floating drag card ────────────────────────────────────────────────────
  if(es.drag_active && es.drag_ci>=0 && es.drag_ci<(int)d.columns.size()
     && es.drag_ri>=0 && es.drag_ri<(int)d.columns[es.drag_ci].cards.size()){
    const auto &dc=d.columns[es.drag_ci].cards[es.drag_ri];
    float fx=mouse.x-col_w*0.5f, fy=mouse.y-card_h*0.5f;
    dl->AddRectFilled(ImVec2(fx,fy),ImVec2(fx+col_w-8,fy+card_h),
      ImGui::ColorConvertFloat4ToU32({0.2f,0.2f,0.2f,0.85f}),3);
    dl->AddRect(ImVec2(fx,fy),ImVec2(fx+col_w-8,fy+card_h),series_color(es.drag_ci,0.9f),3,0,1.5f);
    std::string lbl=dc.label.size()>18?dc.label.substr(0,17)+"…":dc.label;
    ImVec2 cs=ImGui::CalcTextSize(lbl.c_str());
    dl->AddText(ImVec2(fx+(col_w-8-cs.x)*0.5f,fy+(card_h-cs.y)*0.5f),tcol,lbl.c_str());
  }

  // ── Hover tooltip ─────────────────────────────────────────────────────────
  if(hov_ci>=0 && hov_ri>=0 && !es.drag_active){
    const auto &card=cols[hov_ci].cards[hov_ri];
    if(!card.description.empty()){
      ImGui::SetTooltip("%s", card.description.c_str());
    } else {
      ImGui::SetTooltip("%s", card.label.c_str());
    }
  }

  // ── Right-click context menu ──────────────────────────────────────────────
  if(kb_hovered && !es.drag_active && ImGui::IsMouseClicked(ImGuiMouseButton_Right)){
    g_consumed_right_click=true;
    if(hov_ci>=0 && hov_ri>=0){
      es.ctx_ci=hov_ci; es.ctx_ri=hov_ri;
      const auto &card=cols[hov_ci].cards[hov_ri];
      std::strncpy(es.edit_label,card.label.c_str(),255); es.edit_label[255]='\0';
      std::strncpy(es.edit_desc, card.description.c_str(),511); es.edit_desc[511]='\0';
      es.edit_focus=true;
      ImGui::OpenPopup("##kb_edit");
    }
  }

  if(ImGui::BeginPopup("##kb_edit")){
    if(es.ctx_ci>=0 && es.ctx_ci<(int)d.columns.size()
       && es.ctx_ri>=0 && es.ctx_ri<(int)d.columns[es.ctx_ci].cards.size()){
      ImGui::TextDisabled("Edit card");
      ImGui::Separator();
      ImGui::Text("Label:");
      if(es.edit_focus){ ImGui::SetKeyboardFocusHere(); es.edit_focus=false; }
      ImGui::SetNextItemWidth(220);
      ImGui::InputText("##kb_lbl",es.edit_label,256);
      ImGui::Text("Description:");
      ImGui::SetNextItemWidth(220);
      ImGui::InputTextMultiline("##kb_desc",es.edit_desc,512,ImVec2(220,60));
      ImGui::Separator();
      bool confirm = ImGui::Button("OK") ||
                     (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsItemActive());
      if(confirm){
        KanbanDiagram nd=d;
        nd.columns[es.ctx_ci].cards[es.ctx_ri].label      = es.edit_label;
        nd.columns[es.ctx_ci].cards[es.ctx_ri].description = es.edit_desc;
        g_pending_edit={id,serialize_kanban(nd)};
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// ARCHITECTURE
// ═══════════════════════════════════════════════════════════════════════════
bool parse_architecture(std::string_view src, ArchDiagram &out)
{
  out=ArchDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  std::unordered_map<std::string,int> sidx,gidx;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"architecture-beta")||sw(ll,"architecture")){header=true;continue;} continue;}
    // group id(icon)[label]  or  group id(icon)[label] in parent
    if(sw(ll,"group ")){
      std::string_view rest=tr(line.substr(6));
      size_t p1=rest.find('('),p2=rest.find(')'),b1=rest.find('['),b2=rest.find(']');
      std::string gid=std::string(p1!=std::string_view::npos?tr(rest.substr(0,p1)):rest);
      std::string icon=p1!=std::string_view::npos&&p2!=std::string_view::npos?std::string(rest.substr(p1+1,p2-p1-1)):"";
      std::string lbl=b1!=std::string_view::npos&&b2!=std::string_view::npos?std::string(rest.substr(b1+1,b2-b1-1)):gid;
      int n=(int)out.groups.size(); out.groups.push_back({gid,icon,lbl}); gidx[gid]=n; continue;
    }
    // service id(icon)[label] in group
    if(sw(ll,"service ")){
      std::string_view rest=tr(line.substr(8));
      size_t p1=rest.find('('),p2=rest.find(')'),b1=rest.find('['),b2=rest.find(']');
      std::string sid=std::string(p1!=std::string_view::npos?tr(rest.substr(0,p1)):rest);
      std::string icon=p1!=std::string_view::npos&&p2!=std::string_view::npos?std::string(rest.substr(p1+1,p2-p1-1)):"";
      std::string lbl=b1!=std::string_view::npos&&b2!=std::string_view::npos?std::string(rest.substr(b1+1,b2-b1-1)):sid;
      // find "in group_id"
      std::string grp="";
      size_t in_pos=ll.find(" in "); if(in_pos!=std::string::npos) grp=std::string(tr(line.substr(in_pos+4)));
      int n=(int)out.services.size(); out.services.push_back({sid,icon,lbl,grp}); sidx[sid]=n; continue;
    }
    // edge: A:L -- R:B  or  A --> B
    std::string_view lhs,rhs; std::string lbl2;
    if(split_arrow(line,lhs,rhs,lbl2)||line.find(":L -- ")!=std::string_view::npos||line.find(":R -- ")!=std::string_view::npos){
      // strip direction qualifiers L/R/T/B
      auto strip_dir=[](std::string s)->std::string{ size_t col=s.rfind(':'); if(col!=std::string::npos){std::string dir=lc(s.substr(col+1)); if(dir=="l"||dir=="r"||dir=="t"||dir=="b") return s.substr(0,col);} return s; };
      std::string f=strip_dir(std::string(lhs)),t=strip_dir(std::string(rhs));
      if(!f.empty()&&!t.empty()) out.edges.push_back({f,t});
    }
  }
  return header && (!out.services.empty()||!out.groups.empty());
}

void render_architecture(const ArchDiagram &d, int id)
{
  ImGui::PushID(id);
  const float sw2=110.0f,sh=36.0f,hgap=40.0f,vgap=20.0f,gpad=12.0f;
  // lay out services in a grid
  int ns=(int)d.services.size(); if(ns==0) ns=1;
  int cols=std::min(4,ns), rows=(ns+cols-1)/cols;
  float cw=cols*(sw2+hgap)+hgap+gpad*2;
  float ch=rows*(sh+vgap)+vgap+gpad*2+20;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##arch",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 fill=ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);

  // draw groups as large faint rectangles
  for(int i=0;i<(int)d.groups.size();++i){
    float gx=orig.x+gpad+i*(sw2+hgap)*0.5f, gy=orig.y+20;
    float gw=std::min(cw-gpad*2,(float)(cols*(sw2+hgap)));
    float gh=ch-40.0f;
    dl->AddRectFilled(ImVec2(gx,gy),ImVec2(gx+gw,gy+gh),series_color(i+4,0.08f),6);
    dl->AddRect(ImVec2(gx,gy),ImVec2(gx+gw,gy+gh),series_color(i+4,0.4f),6,0,1.5f);
    dl->AddText(ImVec2(gx+4,gy+2),series_color(i+4,1.0f),d.groups[i].label.c_str());
  }
  // draw services
  std::unordered_map<std::string,ImVec2> scenters;
  for(int i=0;i<(int)d.services.size();++i){
    auto [col,row]=grid_pos(i,cols);
    float x=orig.x+gpad+hgap+col*(sw2+hgap), y=orig.y+20+gpad+vgap+row*(sh+vgap);
    scenters[d.services[i].id]=ImVec2(x+sw2*0.5f,y+sh*0.5f);
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+sw2,y+sh),fill,5);
    dl->AddRect(ImVec2(x,y),ImVec2(x+sw2,y+sh),bord,5);
    // icon as text
    std::string disp=d.services[i].label;
    if(!d.services[i].icon.empty()) disp="["+d.services[i].icon+"] "+disp;
    std::string short_d=disp.size()>14?disp.substr(0,13)+"…":disp;
    ImVec2 ts=ImGui::CalcTextSize(short_d.c_str());
    dl->AddText(ImVec2(x+(sw2-ts.x)*0.5f,y+(sh-ts.y)*0.5f),tcol,short_d.c_str());
  }
  // edges
  for(auto &e:d.edges){
    auto ai=scenters.find(e.from),bi=scenters.find(e.to);
    if(ai==scenters.end()||bi==scenters.end()) continue;
    dl->AddLine(ai->second,bi->second,lcol,1.5f);
    float dx=bi->second.x-ai->second.x,dy=bi->second.y-ai->second.y,len=std::sqrt(dx*dx+dy*dy);
    if(len>1){dx/=len;dy/=len;} draw_arrow_head(dl,bi->second,ImVec2(dx,dy),7,lcol);
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// RADAR CHART
// ═══════════════════════════════════════════════════════════════════════════
bool parse_radar(std::string_view src, RadarDiagram &out)
{
  out=RadarDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  RadarCurve cur_curve;
  bool in_curve=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"radar-beta")||sw(ll,"radar")){header=true;continue;} continue;}
    if(sw(ll,"title ")) { out.title=strip_quotes(line.substr(6));continue;}
    if(sw(ll,"max ")||sw(ll,"accmax ")) { out.max_val=std::strtof(std::string(tr(line.substr(ll.find(' ')+1))).c_str(),nullptr); continue;}
    // axis line: "A, B, C, D" or axis [A, B, C]
    if(sw(ll,"axis ")){ std::string_view r=tr(line.substr(5));
      if(!r.empty()&&r.front()=='['){size_t e2=r.find(']');if(e2!=std::string_view::npos)r=r.substr(1,e2-1);}
      std::string r_s(r); std::istringstream ss2(r_s); std::string tok;
      while(std::getline(ss2,tok,',')) out.axes.push_back(strip_quotes(tr(tok)));
      continue;
    }
    // curve block: "Name {" then "data [v1,v2,...]" then "}"
    if(line=="}"){ if(in_curve){out.curves.push_back(cur_curve);} in_curve=false; cur_curve=RadarCurve{}; continue;}
    if(line.back()=='{'){
      in_curve=true; cur_curve.name=std::string(tr(line.substr(0,line.size()-1))); continue;
    }
    if(in_curve&&sw(ll,"data [")){
      size_t b=line.find('['),e2=line.find(']');
      if(b!=std::string_view::npos&&e2!=std::string_view::npos){
        std::string inner=std::string(line.substr(b+1,e2-b-1));
        std::istringstream ss2(inner); std::string tok;
        while(std::getline(ss2,tok,',')) cur_curve.values.push_back(std::strtof(std::string(tr(tok)).c_str(),nullptr));
      }
      continue;
    }
    // simple "Label: value" format (alternative syntax)
    size_t col=line.find(':');
    if(col!=std::string_view::npos&&!in_curve){
      std::string axis_name=strip_quotes(line.substr(0,col));
      float val=std::strtof(std::string(tr(line.substr(col+1))).c_str(),nullptr);
      out.axes.push_back(axis_name);
      if(out.curves.empty()) out.curves.push_back({"Values",{}});
      out.curves[0].values.push_back(val);
      out.max_val=std::max(out.max_val,val);
    }
  }
  if(in_curve) out.curves.push_back(cur_curve);
  return header && (!out.axes.empty()||!out.curves.empty());
}

void render_radar(const RadarDiagram &d, int id)
{
  ImGui::PushID(id);
  const float r=110.0f,pad=60.0f;
  float cw=r*2+pad*2, ch=r*2+pad*2+20;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##radar",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 gcol=ImGui::GetColorU32(ImGuiCol_Separator);
  ImVec2 center(orig.x+pad+r,orig.y+20+pad+r);
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y+2),tcol,d.title.c_str());}
  int na=(int)d.axes.size(); if(na<3){ImGui::Text("Need >= 3 axes");ImGui::PopID();return;}
  // web lines
  for(int level=1;level<=4;++level){
    float lr=r*level*0.25f;
    std::vector<ImVec2> pts;
    for(int a=0;a<=na;++a){
      float angle=-kPi*0.5f+a*(2*kPi/na);
      pts.push_back(ImVec2(center.x+std::cos(angle)*lr,center.y+std::sin(angle)*lr));
    }
    for(int a=0;a<na;++a) dl->AddLine(pts[a],pts[a+1],gcol,1.0f);
  }
  // spokes
  for(int a=0;a<na;++a){
    float angle=-kPi*0.5f+a*(2*kPi/na);
    ImVec2 tip(center.x+std::cos(angle)*r,center.y+std::sin(angle)*r);
    dl->AddLine(center,tip,gcol,1.0f);
    // axis label
    const std::string &lbl=d.axes[a];
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    float lx=center.x+std::cos(angle)*(r+14)-ts.x*0.5f;
    float ly=center.y+std::sin(angle)*(r+14)-ts.y*0.5f;
    dl->AddText(ImVec2(lx,ly),tcol,lbl.c_str());
  }
  // curves
  float max_v=d.max_val>0?d.max_val:100.0f;
  for(int ci=0;ci<(int)d.curves.size();++ci){
    auto &c=d.curves[ci];
    if(c.values.empty()) continue;
    std::vector<ImVec2> pts;
    for(int a=0;a<na;++a){
      float v=a<(int)c.values.size()?c.values[a]:0.0f;
      float frac=v/max_v; float angle=-kPi*0.5f+a*(2*kPi/na);
      pts.push_back(ImVec2(center.x+std::cos(angle)*r*frac,center.y+std::sin(angle)*r*frac));
    }
    ImU32 cc=series_color(ci,0.35f);
    ImU32 cc2=series_color(ci,0.85f);
    dl->AddConvexPolyFilled(pts.data(),(int)pts.size(),cc);
    pts.push_back(pts[0]);
    for(int k=0;k<(int)pts.size()-1;++k) dl->AddLine(pts[k],pts[k+1],cc2,2.0f);
    if(!c.name.empty()){ ImVec2 ts=ImGui::CalcTextSize(c.name.c_str()); dl->AddText(ImVec2(orig.x+pad+(float)ci*60.0f,orig.y+cw-20),cc2,c.name.c_str()); }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// TREEMAP
// ═══════════════════════════════════════════════════════════════════════════
bool parse_treemap(std::string_view src, TreemapDiagram &out)
{
  out=TreemapDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  // track indent to build tree
  std::vector<int> level_stack; level_stack.push_back(-1);
  std::vector<int> parent_at_level(20,-1);
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"treemap-beta")||sw(ll,"treemap")){header=true;continue;} continue;}
    if(sw(ll,"title ")) continue;
    int indent=0; for(char c:line){if(c==' ')indent++;else if(c=='\t')indent+=2;else break;}
    int level=indent/2;
    std::string_view l=tr(line);
    if(l.empty()) continue;
    size_t col=l.find(':');
    std::string name=col!=std::string_view::npos?strip_quotes(l.substr(0,col)):std::string(l);
    float val=col!=std::string_view::npos?std::strtof(std::string(tr(l.substr(col+1))).c_str(),nullptr):0.0f;
    int par=level>0?parent_at_level[level-1]:-1;
    TreemapNode node; node.name=name; node.value=val; node.parent=par;
    int ni=(int)out.nodes.size();
    if(par>=0) out.nodes[par].children.push_back(ni);
    parent_at_level[level]=ni;
    out.nodes.push_back(node);
  }
  return header && !out.nodes.empty();
}

void render_treemap(const TreemapDiagram &d, int id)
{
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float cw=340.0f, ch=200.0f;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##tm",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);

  // compute total value for root children
  std::function<float(int)> total_val=[&](int ni)->float{
    if(!d.nodes[ni].children.empty()){
      float s=0; for(int c:d.nodes[ni].children) s+=total_val(c); return s;
    }
    return d.nodes[ni].value>0?d.nodes[ni].value:1.0f;
  };

  // squarified treemap recursive layout
  std::function<void(int,ImVec2,ImVec2,int)> layout=[&](int ni,ImVec2 tl,ImVec2 br,int depth){
    auto &node=d.nodes[ni];
    ImU32 fc=series_color(ni+depth*3,0.6f-depth*0.1f);
    dl->AddRectFilled(tl,br,fc,2);
    dl->AddRect(tl,br,bord,2,0,1.5f);
    float fw=br.x-tl.x, fh=br.y-tl.y;
    std::string lbl=node.name.size()>(size_t)(fw/7)?node.name.substr(0,(size_t)fw/7)+"…":node.name;
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    if(ts.x<=fw-4&&ts.y<=fh-4) dl->AddText(ImVec2(tl.x+(fw-ts.x)*0.5f,tl.y+(fh-ts.y)*0.5f),tcol,lbl.c_str());
    if(node.children.empty()) return;
    float tot=total_val(ni); if(tot<=0) return;
    // horizontal split
    float x=tl.x;
    for(int ci:node.children){
      float frac=total_val(ci)/tot;
      float nx=x+fw*frac;
      if(depth%2==0) layout(ci,ImVec2(x,tl.y+16),ImVec2(nx,br.y),depth+1);
      else {
        float y=tl.y+16+(br.y-tl.y-16)*0;
        layout(ci,ImVec2(tl.x,y),ImVec2(br.x,tl.y+16+(br.y-tl.y-16)*frac),depth+1);
      }
      x=nx;
    }
  };

  // find roots (parent==-1)
  std::vector<int> roots;
  for(int i=0;i<(int)d.nodes.size();++i) if(d.nodes[i].parent<0) roots.push_back(i);
  if(roots.empty()){ ImGui::Text("(empty treemap)"); ImGui::PopID(); return; }
  if(roots.size()==1){ layout(roots[0],orig,ImVec2(orig.x+cw,orig.y+ch),0); }
  else {
    float tot=0; for(int r:roots) tot+=total_val(r); if(tot<=0) tot=1;
    float x=orig.x;
    for(int r:roots){ float fw2=cw*total_val(r)/tot; layout(r,ImVec2(x,orig.y),ImVec2(x+fw2,orig.y+ch),0); x+=fw2; }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// ZENUML  (rendered as sequence diagram)
// ═══════════════════════════════════════════════════════════════════════════
bool parse_zenuml(std::string_view src, SequenceDiagram &out)
{
  // ZenUML uses "A.method(B)" syntax — convert to sequence diagram events
  out=SequenceDiagram{};
  std::unordered_map<std::string,int> pidx;
  auto ensure_part=[&](const std::string &id){
    auto it=pidx.find(id); if(it!=pidx.end()) return it->second;
    SeqParticipant p; p.id=id; p.label=id;
    int n=(int)out.participants.size(); out.participants.push_back(p); pidx[id]=n; return n;
  };
  Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"zenuml")){header=true;continue;} continue;}
    if(sw(ll,"title ")){ out.title=std::string(tr(line.substr(6)));continue;}
    // participant/actor declarations
    if(sw(ll,"@")){ std::string_view rest=tr(line.substr(1)); std::string name=std::string(rest.substr(0,rest.find(' '))); ensure_part(name); continue; }
    // A.method(B) → message from A to B
    size_t dot=line.find('.'); size_t p1=line.find('('); size_t p2=line.find(')');
    if(dot!=std::string_view::npos&&p1!=std::string_view::npos&&p1>dot){
      std::string from=std::string(tr(line.substr(0,dot)));
      std::string method=std::string(tr(line.substr(dot+1,p1-dot-1)));
      std::string to=p2!=std::string_view::npos?strip_quotes(line.substr(p1+1,p2-p1-1)):from;
      if(to.empty()) to=from;
      ensure_part(from); ensure_part(to);
      SeqMessage msg{from,to,method,false,true};
      int mi=(int)out.messages.size(); out.messages.push_back(msg);
      out.events.push_back({SequenceDiagram::Event::T::Message,mi,"","",""});
    }
  }
  return header && !out.participants.empty();
}

void render_zenuml(const SequenceDiagram &d, int id){ render_sequence(d,id); }

// ═══════════════════════════════════════════════════════════════════════════
// EVENT MODELING
// ═══════════════════════════════════════════════════════════════════════════
bool parse_eventmodeling(std::string_view src, EventModelingDiagram &out)
{
  out=EventModelingDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"eventmodeling")){header=true;continue;} continue;}
    if(sw(ll,"title ")) { out.title=strip_quotes(line.substr(6));continue;}
    // command/event/readmodel/policy/processor keywords
    if(sw(ll,"command "))   { out.items.push_back({EMItem::T::Command,   strip_quotes(line.substr(8))});  continue;}
    if(sw(ll,"event "))     { out.items.push_back({EMItem::T::Event,     strip_quotes(line.substr(6))});  continue;}
    if(sw(ll,"readmodel ")  ||sw(ll,"read_model ")) { out.items.push_back({EMItem::T::ReadModel,strip_quotes(line.substr(ll.find(' ')+1))}); continue;}
    if(sw(ll,"policy "))    { out.items.push_back({EMItem::T::Policy,    strip_quotes(line.substr(7))});  continue;}
    if(sw(ll,"processor ")) { out.items.push_back({EMItem::T::Processor, strip_quotes(line.substr(10))}); continue;}
    // arrow: A --> B
    std::string_view lhs,rhs; std::string lbl;
    if(split_arrow(line,lhs,rhs,lbl)) out.links.push_back({std::string(lhs),std::string(rhs)});
  }
  return header && !out.items.empty();
}

void render_eventmodeling(const EventModelingDiagram &d, int id)
{
  ImGui::PushID(id);
  const float iw=120.0f,ih=40.0f,hgap=14.0f,pad=12.0f;
  int n=(int)d.items.size(); if(n==0){ImGui::Text("(empty event model)");ImGui::PopID();return;}
  float cw=n*(iw+hgap)+hgap+pad*2, ch=ih+pad*2+20;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##em",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y),tcol,d.title.c_str());}
  // type → color hue
  static const float hues[]={0.6f,0.1f,0.35f,0.75f,0.5f}; // command=blue,event=orange,readmodel=green,policy=purple,processor=teal
  std::unordered_map<std::string,int> item_idx;
  for(int i=0;i<n;++i){
    float x=orig.x+pad+i*(iw+hgap), y=orig.y+20+pad;
    int ti=(int)d.items[i].type;
    float rr,gg,bb; ImGui::ColorConvertHSVtoRGB(hues[ti],0.6f,0.88f,rr,gg,bb);
    ImU32 fc=ImGui::GetColorU32(ImVec4(rr,gg,bb,0.75f));
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+iw,y+ih),fc,5);
    dl->AddRect(ImVec2(x,y),ImVec2(x+iw,y+ih),bord,5);
    std::string lbl=d.items[i].name.size()>14?d.items[i].name.substr(0,13)+"…":d.items[i].name;
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    dl->AddText(ImVec2(x+(iw-ts.x)*0.5f,y+(ih-ts.y)*0.5f),tcol,lbl.c_str());
    item_idx[d.items[i].name]=i;
    // draw arrows to next item in sequence
    if(i<n-1){
      ImVec2 a(x+iw,y+ih*0.5f), b(x+iw+hgap,y+ih*0.5f);
      dl->AddLine(a,b,ImGui::GetColorU32(ImGuiCol_TextDisabled),1.5f);
      draw_arrow_head(dl,b,ImVec2(1,0),7,ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// VENN DIAGRAM
// ═══════════════════════════════════════════════════════════════════════════
bool parse_venn(std::string_view src, VennDiagram &out)
{
  out=VennDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"venn")){header=true;continue;} continue;}
    if(sw(ll,"title ")) { out.title=strip_quotes(line.substr(6));continue;}
    // set: id "Label"  or  id  "Label"
    // intersection: A&B "label"  or  A+B "label"
    size_t amp=line.find('&'), plus2=line.find('+');
    size_t sep=(amp!=std::string_view::npos)?amp:(plus2!=std::string_view::npos?plus2:std::string_view::npos);
    if(sep!=std::string_view::npos){
      std::string ids=std::string(tr(line));
      // find label in quotes
      size_t q1=ids.find('"'),q2=ids.rfind('"');
      std::string lbl=q1!=std::string::npos&&q2>q1?ids.substr(q1+1,q2-q1-1):"";
      // split ids by & or +
      std::string id_part=ids.substr(0,q1!=std::string::npos?q1:ids.size());
      VennIntersection vi; vi.label=lbl;
      std::istringstream ss2(id_part); std::string tok;
      while(std::getline(ss2,tok,amp!=std::string::npos?'&':'+')) vi.set_ids.push_back(std::string(tr(tok)));
      out.intersections.push_back(vi);
    } else {
      // bare set: id ["label"]
      std::string ls=std::string(line);
      size_t q1=ls.find('"'),q2=ls.rfind('"');
      std::string set_id=q1!=std::string::npos?std::string(tr(ls.substr(0,q1))):ls;
      std::string lbl2=q1!=std::string::npos&&q2>q1?ls.substr(q1+1,q2-q1-1):set_id;
      set_id=std::string(tr(set_id));
      if(!set_id.empty()) out.sets.push_back({set_id,lbl2});
    }
  }
  return header && !out.sets.empty();
}

void render_venn(const VennDiagram &d, int id)
{
  ImGui::PushID(id);
  int ns=(int)d.sets.size(); if(ns<2){ ImGui::Text("Need >= 2 sets for Venn"); ImGui::PopID(); return; }
  const float r=58.0f, pad=20.0f, title_h=20.0f;

  // Compute canvas size
  float cw, ch;
  if(ns==3){
    cw = r*3.8f + pad*2;
    ch = r*3.2f + pad*2 + title_h;
  } else {
    float overlap=r*0.38f;
    cw = ns*r*2-(ns-1)*overlap + pad*2;
    ch = r*2 + pad*2 + title_h + 20.0f;
  }

  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##venn",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);

  // Compute circle centers relative to orig
  std::vector<ImVec2> set_centers(ns);
  if(ns==3){
    // Triangular layout: A top-center, B bottom-left, C bottom-right
    float cx=orig.x+cw*0.5f, cy=orig.y+title_h+pad+r*1.15f;
    float ox=r*0.78f, oy=r*0.45f;
    set_centers[0]=ImVec2(cx,    cy-oy);   // A: top
    set_centers[1]=ImVec2(cx-ox, cy+oy);   // B: bottom-left
    set_centers[2]=ImVec2(cx+ox, cy+oy);   // C: bottom-right
  } else {
    float overlap=r*0.38f;
    float start_x=orig.x+pad+r;
    float cy=orig.y+title_h+pad+r;
    for(int i=0;i<ns;++i) set_centers[i]=ImVec2(start_x+i*(r*2-overlap),cy);
  }

  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y),tcol,d.title.c_str());}

  // Draw circles
  for(int i=0;i<ns;++i){
    float rr,gg,bb; ImGui::ColorConvertHSVtoRGB((float)i/ns,0.55f,0.9f,rr,gg,bb);
    dl->AddCircleFilled(set_centers[i],r,ImGui::GetColorU32(ImVec4(rr,gg,bb,0.22f)));
    dl->AddCircle(set_centers[i],r,ImGui::GetColorU32(ImVec4(rr,gg,bb,0.8f)),0,2.0f);
  }

  // Set labels — placed outside each circle's overlap direction
  for(int i=0;i<ns;++i){
    std::string lbl=d.sets[i].label;
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    ImVec2 lp;
    if(ns==3){
      if(i==0) lp=ImVec2(set_centers[0].x-ts.x*0.5f, set_centers[0].y-r-ts.y-2); // above A
      else if(i==1) lp=ImVec2(set_centers[1].x-ts.x-r*0.15f, set_centers[1].y+r*0.55f);  // below-left of B
      else          lp=ImVec2(set_centers[2].x+r*0.15f,       set_centers[2].y+r*0.55f);  // below-right of C
    } else {
      lp=ImVec2(set_centers[i].x-ts.x*0.5f, set_centers[i].y+r+4);
    }
    dl->AddText(lp, tcol, lbl.c_str());
  }

  // Intersection labels at the centroid of the referenced set centers
  for(auto &vi:d.intersections){
    if(vi.label.empty()||vi.set_ids.size()<2) continue;
    float ix=0,iy=0; int cnt=0;
    for(auto &sid:vi.set_ids){
      for(int i=0;i<ns;++i){ if(d.sets[i].id==sid){ix+=set_centers[i].x;iy+=set_centers[i].y;cnt++;break;} }
    }
    if(cnt>0){ ix/=cnt; iy/=cnt; ImVec2 ts=ImGui::CalcTextSize(vi.label.c_str()); dl->AddText(ImVec2(ix-ts.x*0.5f,iy-ts.y*0.5f),tcol,vi.label.c_str()); }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// ISHIKAWA (FISHBONE)
// ═══════════════════════════════════════════════════════════════════════════
bool parse_ishikawa(std::string_view src, IshikawaDiagram &out)
{
  out=IshikawaDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"ishikawa")){header=true;continue;} continue;}
    if(sw(ll,"effect ")){ out.effect=strip_quotes(line.substr(7));continue;}
    if(sw(ll,"category ")){ out.categories.push_back({strip_quotes(line.substr(9)),{}});continue;}
    // cause: bare text indented
    int indent=0; for(char c:line){if(c==' ')indent++;else if(c=='\t')indent+=2;else break;}
    std::string_view l=tr(line);
    if(!l.empty()&&!sw(lc(l),"cause ")&&!sw(lc(l),"sub ")&&!out.categories.empty()){
      IshikawaCause c; c.text=std::string(l);
      out.categories.back().causes.push_back(c);
    }
    if(sw(lc(l),"cause ")||sw(lc(l),"sub ")){
      size_t sp=l.find(' ');
      if(!out.categories.empty()){ IshikawaCause c; c.text=strip_quotes(l.substr(sp)); out.categories.back().causes.push_back(c); }
    }
  }
  return header && !out.effect.empty();
}

void render_ishikawa(const IshikawaDiagram &d, int id)
{
  ImGui::PushID(id);
  const float cw=420.0f,ch=200.0f,effect_w=80.0f;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##ish",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  // spine
  float sy=orig.y+ch*0.5f;
  dl->AddLine(ImVec2(orig.x+20,sy),ImVec2(orig.x+cw-effect_w-10,sy),tcol,2.5f);
  // effect box
  float ex=orig.x+cw-effect_w;
  dl->AddRectFilled(ImVec2(ex,sy-18),ImVec2(ex+effect_w,sy+18),ImGui::GetColorU32(ImVec4(0.8f,0.3f,0.3f,0.7f)),4);
  std::string eff=d.effect.size()>10?d.effect.substr(0,9)+"…":d.effect;
  ImVec2 es=ImGui::CalcTextSize(eff.c_str()); dl->AddText(ImVec2(ex+(effect_w-es.x)*0.5f,sy-es.y*0.5f),tcol,eff.c_str());
  // categories as bones
  int nc=(int)d.categories.size(); if(nc==0){ImGui::PopID();return;}
  float bone_spacing=(cw-effect_w-40.0f)/std::max(1,nc);
  for(int i=0;i<nc;++i){
    float bx=orig.x+20.0f+i*bone_spacing+bone_spacing*0.5f;
    bool top=(i%2==0);
    float ey2=top?orig.y+20:orig.y+ch-20;
    // bone
    dl->AddLine(ImVec2(bx,ey2),ImVec2(bx+(top?20:-20),sy),lcol,1.5f);
    // category label
    const std::string &cat=d.categories[i].name;
    ImVec2 cs=ImGui::CalcTextSize(cat.c_str());
    dl->AddText(ImVec2(bx-cs.x*0.5f,top?ey2-cs.y-2:ey2+2),tcol,cat.c_str());
    // causes as sub-bones
    for(int j=0;j<(int)d.categories[i].causes.size()&&j<4;++j){
      float cx2=bx+(j+1)*-12.0f*(top?-1:1);
      float cy2=top?sy-(sy-ey2)*((j+1)*0.25f):sy+(ey2-sy)*((j+1)*0.25f);
      dl->AddLine(ImVec2(cx2,cy2-8),ImVec2(cx2,cy2+8),lcol,1.0f);
      const std::string &cause=d.categories[i].causes[j].text;
      std::string sc=cause.size()>10?cause.substr(0,9)+"…":cause;
      ImVec2 ls=ImGui::CalcTextSize(sc.c_str());
      dl->AddText(ImVec2(cx2-ls.x*0.5f,top?cy2-ls.y-10:cy2+10),lcol,sc.c_str());
    }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// WARDLEY MAP
// ═══════════════════════════════════════════════════════════════════════════
bool parse_wardley(std::string_view src, WardleyDiagram &out)
{
  out=WardleyDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"wardley")){header=true;continue;} continue;}
    if(sw(ll,"title ")) { out.title=strip_quotes(line.substr(6));continue;}
    // component name [visibility, evolution]
    if(sw(ll,"component ")||sw(ll,"note ")){
      std::string_view rest=tr(line.substr(sw(ll,"component ")?10:5));
      size_t b1=rest.find('['),b2=rest.find(']');
      std::string name=std::string(b1!=std::string_view::npos?tr(rest.substr(0,b1)):rest);
      float vis=0.5f,evo=0.5f;
      if(b1!=std::string_view::npos&&b2!=std::string_view::npos){
        std::string coords=std::string(rest.substr(b1+1,b2-b1-1));
        size_t comma=coords.find(',');
        if(comma!=std::string::npos){ vis=std::strtof(coords.substr(0,comma).c_str(),nullptr); evo=std::strtof(coords.substr(comma+1).c_str(),nullptr); }
      }
      out.components.push_back({name,vis,evo}); continue;
    }
    // link: A -> B
    std::string_view lhs,rhs; std::string lbl;
    if(split_arrow(line,lhs,rhs,lbl)) out.links.push_back({std::string(lhs),std::string(rhs)});
  }
  return header && !out.components.empty();
}

void render_wardley(const WardleyDiagram &d, int id)
{
  ImGui::PushID(id);
  const float cw=320.0f,ch=240.0f,pad=40.0f;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##wd",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 gcol=ImGui::GetColorU32(ImGuiCol_Separator);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  float pw=cw-pad*2, ph=ch-pad*2-20;
  ImVec2 tl(orig.x+pad,orig.y+20+pad);
  // axes
  dl->AddLine(ImVec2(tl.x,tl.y),ImVec2(tl.x,tl.y+ph),tcol,1.5f);
  dl->AddLine(ImVec2(tl.x,tl.y+ph),ImVec2(tl.x+pw,tl.y+ph),tcol,1.5f);
  // axis labels
  dl->AddText(ImVec2(tl.x-2,tl.y-14),lcol,"Visible");
  dl->AddText(ImVec2(tl.x-2,tl.y+ph+2),lcol,"Invisible");
  const char *stages[]={"Genesis","Custom","Product","Commodity"};
  for(int s=0;s<4;++s){ImVec2 ts=ImGui::CalcTextSize(stages[s]);dl->AddText(ImVec2(tl.x+pw*s/3.0f-ts.x*0.5f,tl.y+ph+14),lcol,stages[s]);}
  // grid
  for(int s=1;s<4;++s){float gx=tl.x+pw*s/4.0f;dl->AddLine(ImVec2(gx,tl.y),ImVec2(gx,tl.y+ph),gcol,1.0f);}
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y),tcol,d.title.c_str());}
  // components
  std::unordered_map<std::string,ImVec2> comp_pos;
  for(int i=0;i<(int)d.components.size();++i){
    auto &c=d.components[i];
    float px=tl.x+c.evolution*pw, py=tl.y+(1.0f-c.visibility)*ph;
    comp_pos[c.name]=ImVec2(px,py);
    ImU32 cc=series_color(i);
    dl->AddCircleFilled(ImVec2(px,py),6,cc);
    ImVec2 ts=ImGui::CalcTextSize(c.name.c_str());
    dl->AddText(ImVec2(px-ts.x*0.5f,py-ts.y-4),tcol,c.name.c_str());
  }
  // links
  for(auto &l:d.links){
    auto ai=comp_pos.find(l.from),bi=comp_pos.find(l.to);
    if(ai==comp_pos.end()||bi==comp_pos.end()) continue;
    dl->AddLine(ai->second,bi->second,lcol,1.5f);
    float dx=bi->second.x-ai->second.x,dy=bi->second.y-ai->second.y,len=std::sqrt(dx*dx+dy*dy);
    if(len>1){dx/=len;dy/=len;} draw_arrow_head(dl,bi->second,ImVec2(dx,dy),7,lcol);
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// TREEVIEW
// ═══════════════════════════════════════════════════════════════════════════
bool parse_treeview(std::string_view src, TreeViewDiagram &out)
{
  out=TreeViewDiagram{}; bool header=false;
  std::vector<int> parent_at_level(20,-1);
  IndentLines L{src}; std::string_view line; int indent=0;
  while(L.next(line,indent)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"treeview")){header=true;continue;} continue;}
    int level=indent/2;
    std::string_view l=tr(line); if(l.empty()) continue;
    // strip tree drawing chars: ├─, └─, │, etc.
    while(!l.empty()&&(l[0]==static_cast<char>(0xE2)||l[0]=='|'||l[0]=='-'||l[0]=='+'||l[0]==' '||l[0]=='`'||l[0]=='\\')){
      if(l.size()>=3&&(unsigned char)l[0]==0xE2) l=l.substr(3); else l=l.substr(1);
    }
    l=tr(l); if(l.empty()) continue;
    std::string lbl=strip_quotes(l);
    int par=level>0?parent_at_level[level-1]:-1;
    TVNode node; node.label=lbl; node.parent=par;
    int ni=(int)out.nodes.size();
    if(par>=0) out.nodes[par].children.push_back(ni);
    parent_at_level[std::min(level,19)]=ni;
    out.nodes.push_back(node);
  }
  return header && !out.nodes.empty();
}

void render_treeview(const TreeViewDiagram &d, int id)
{
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float row_h=22.0f,indent_w=18.0f,pad=8.0f;
  int n=(int)d.nodes.size();
  float max_depth=0; for(auto &nd:d.nodes){ int dep=0; int p=nd.parent; while(p>=0){dep++;p=d.nodes[p].parent;} max_depth=std::max(max_depth,(float)dep); }
  float cw=pad+max_depth*indent_w+200.0f, ch=n*row_h+pad*2;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##tv",ImVec2(cw,ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);

  // DFS traversal to compute depths and draw
  std::function<void(int,int,int&)> draw_node=[&](int ni,int depth,int &row){
    float x=orig.x+pad+depth*indent_w;
    float y=orig.y+pad+row*row_h;
    // connector lines
    if(depth>0){
      dl->AddLine(ImVec2(x-indent_w+6,y+row_h*0.5f),ImVec2(x,y+row_h*0.5f),lcol,1.0f);
      dl->AddLine(ImVec2(x-indent_w+6,y-row_h*0.5f),ImVec2(x-indent_w+6,y+row_h*0.5f),lcol,1.0f);
    }
    // bullet
    bool has_children=!d.nodes[ni].children.empty();
    if(has_children) dl->AddTriangleFilled(ImVec2(x,y+row_h*0.5f-4),ImVec2(x,y+row_h*0.5f+4),ImVec2(x+6,y+row_h*0.5f),series_color(depth));
    else dl->AddCircleFilled(ImVec2(x+3,y+row_h*0.5f),3,series_color(depth,0.7f));
    dl->AddText(ImVec2(x+10,y+(row_h-ImGui::GetTextLineHeight())*0.5f),tcol,d.nodes[ni].label.c_str());
    row++;
    for(int c:d.nodes[ni].children) draw_node(c,depth+1,row);
  };
  int row=0;
  for(int i=0;i<n;++i) if(d.nodes[i].parent<0) draw_node(i,0,row);
  ImGui::PopID();
}

} // namespace MermaidDiagrams

