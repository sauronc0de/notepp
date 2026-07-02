// Icon shader: default depth/contrast pass + hover glow/rim/scale via GLSL.
// Uses ImDrawList::AddCallback to inject a standalone render pass between
// normal ImGui draw commands.

#include "icon_shader.hpp"
#include "log.hpp"

// GLEW must come before any other GL headers.
#include <GL/glew.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace NoteUi
{

static ImVec2 nonzero_invisible_button_size(ImVec2 size)
{
  return ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y));
}

// ---------------------------------------------------------------------------
// GLSL sources
// ---------------------------------------------------------------------------

static const char *kVertSrc = R"GLSL(
#version 150
uniform mat4 ProjMtx;
in vec2 Position;
in vec2 UV;
in vec4 Color;
out vec2 Frag_UV;
out vec4 Frag_Color;
void main() {
    Frag_UV    = UV;
    Frag_Color = Color;
    gl_Position = ProjMtx * vec4(Position.xy, 0.0, 1.0);
}
)GLSL";

// Two-state fragment shader:
//   u_hover == 0  →  default: small brightness lift for readability.
//   u_hover  > 0  →  hover:   brightness breathing pulse + soft rim glow.
static const char *kFragSrc = R"GLSL(
#version 150
uniform sampler2D Texture;
uniform float u_hover;
uniform float u_time;
in vec2  Frag_UV;
in vec4  Frag_Color;
out vec4 Out_Color;

void main() {
    vec4 col = texture(Texture, Frag_UV);

    if (u_hover < 0.001) {
        // --- Default: small brightness lift so icons read well on dark toolbar ---
        col.rgb = min(col.rgb * 1.18, vec3(1.0));
        Out_Color = col * Frag_Color;
        return;
    }

    // --- Hover: static brightness lift + softly breathing rim glow ---

    // Brightness: fixed lift when hovered, no pulse.
    col.rgb = min(col.rgb * (1.18 + u_hover * 0.14), vec3(1.0));

    // 12-tap silhouette rim — GL_CLAMP_TO_BORDER keeps glow on silhouette only.
    float ring_a = 0.0;
    for (int i = 0; i < 12; i++) {
        float ang = 6.28318 * float(i) / 12.0;
        ring_a += texture(Texture, Frag_UV + vec2(cos(ang), sin(ang)) * 0.11).a;
    }
    ring_a = clamp(ring_a / 12.0, 0.0, 1.0);
    float edge = ring_a * (1.0 - col.a);

    // Rim strength breathes with a very small amplitude — just a subtle pulse.
    float breath  = 0.5 + 0.5 * sin(u_time * 3.0);
    float rim_str = u_hover * (0.38 + breath * 0.14);

    vec3  rgb = mix(col.rgb, vec3(0.78, 0.90, 1.00), edge * rim_str);
    float a   = max(col.a,   edge * rim_str);

    Out_Color = vec4(rgb, a) * Frag_Color;
}
)GLSL";

// ---------------------------------------------------------------------------
// GL objects
// ---------------------------------------------------------------------------

struct GlState
{
  GLuint prog = 0;
  GLuint vao  = 0;
  GLuint vbo  = 0;
  GLuint ebo  = 0;

  GLint loc_proj  = -1;
  GLint loc_tex   = -1;
  GLint loc_hover = -1;
  GLint loc_time  = -1;

  bool ready = false;
};
static GlState g_gl;

// Per-button hover animation state (keyed by ImGuiID).
static std::unordered_map<ImGuiID, float> g_hover;

// ---------------------------------------------------------------------------
// Per-frame callback data pool (avoids allocations; reset each frame)
// ---------------------------------------------------------------------------

struct CbData
{
  float x0, y0, x1, y1;   // screen rect (possibly scaled for hover)
  GLuint  tex;
  float   hover_t;
  float   time;
  // Projection: display size + pos
  float   disp_x, disp_y, disp_w, disp_h;
  // Framebuffer
  float   fb_scale_x, fb_scale_y;
  int     fb_h;
  // Clip rect (display coords)
  float   cr_x, cr_y, cr_z, cr_w;
};

static constexpr int kPoolCap = 128;
static CbData g_pool[kPoolCap];
static int    g_pool_n     = 0;
static int    g_pool_frame = -1;

static CbData *alloc_cb()
{
  int f = ImGui::GetFrameCount();
  if(f != g_pool_frame) { g_pool_n = 0; g_pool_frame = f; }
  if(g_pool_n >= kPoolCap) return nullptr;
  return &g_pool[g_pool_n++];
}

// ---------------------------------------------------------------------------
// Shader helpers
// ---------------------------------------------------------------------------

static GLuint compile_shader(GLenum type, const char *src)
{
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if(!ok)
  {
    char buf[512];
    glGetShaderInfoLog(s, 512, nullptr, buf);
    LOG_ERROR("[icon_shader] compile error: ", buf);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

// ---------------------------------------------------------------------------
// Public init / destroy
// ---------------------------------------------------------------------------

void init_icon_shader()
{
  // Initialize GLEW (idempotent; needs a current GL context).
  GLenum ge = glewInit();
  if(ge != GLEW_OK && ge != GLEW_ERROR_NO_GLX_DISPLAY)
  {
    LOG_ERROR("[icon_shader] glewInit failed: ", glewGetErrorString(ge));
    return;
  }

  GLuint vert = compile_shader(GL_VERTEX_SHADER,   kVertSrc);
  GLuint frag = compile_shader(GL_FRAGMENT_SHADER, kFragSrc);
  if(!vert || !frag)
  {
    if(vert) glDeleteShader(vert);
    if(frag) glDeleteShader(frag);
    return;
  }

  GLuint prog = glCreateProgram();
  glAttachShader(prog, vert);
  glAttachShader(prog, frag);

  // Fix attribute locations to match our VAO layout (must be before link).
  glBindAttribLocation(prog, 0, "Position");
  glBindAttribLocation(prog, 1, "UV");
  glBindAttribLocation(prog, 2, "Color");

  glLinkProgram(prog);
  glDeleteShader(vert);
  glDeleteShader(frag);

  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if(!ok)
  {
    char buf[512];
    glGetProgramInfoLog(prog, 512, nullptr, buf);
    LOG_ERROR("[icon_shader] link error: ", buf);
    glDeleteProgram(prog);
    return;
  }

  g_gl.prog     = prog;
  g_gl.loc_proj  = glGetUniformLocation(prog, "ProjMtx");
  g_gl.loc_tex   = glGetUniformLocation(prog, "Texture");
  g_gl.loc_hover = glGetUniformLocation(prog, "u_hover");
  g_gl.loc_time  = glGetUniformLocation(prog, "u_time");

  // Build a self-contained VAO/VBO/EBO.
  // Layout per vertex: pos(2f) + uv(2f) + color(4f) = 8 floats
  glGenVertexArrays(1, &g_gl.vao);
  glGenBuffers(1, &g_gl.vbo);
  glGenBuffers(1, &g_gl.ebo);

  glBindVertexArray(g_gl.vao);
  glBindBuffer(GL_ARRAY_BUFFER,         g_gl.vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.ebo);

  constexpr GLsizei stride = 8 * sizeof(float);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(4 * sizeof(float)));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  g_gl.ready = true;
}

void destroy_icon_shader()
{
  if(g_gl.vao)  glDeleteVertexArrays(1, &g_gl.vao);
  if(g_gl.vbo)  glDeleteBuffers(1, &g_gl.vbo);
  if(g_gl.ebo)  glDeleteBuffers(1, &g_gl.ebo);
  if(g_gl.prog) glDeleteProgram(g_gl.prog);
  g_gl = {};
  g_hover.clear();
}

// ---------------------------------------------------------------------------
// Render callback (executes during ImGui_ImplOpenGL3_RenderDrawData)
// ---------------------------------------------------------------------------

static void icon_render_cb(const ImDrawList *, const ImDrawCmd *cmd)
{
  const CbData *d = static_cast<const CbData *>(cmd->UserCallbackData);
  if(!d || !g_gl.ready) return;

  // --- Orthographic projection matching ImGui's own ---
  float L = d->disp_x,             R = d->disp_x + d->disp_w;
  float T = d->disp_y,             B = d->disp_y + d->disp_h;
  const float proj[4][4] = {
      {2.0f / (R - L),    0.0f,             0.0f, 0.0f},
      {0.0f,              2.0f / (T - B),   0.0f, 0.0f},
      {0.0f,              0.0f,            -1.0f, 0.0f},
      {(R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f},
  };

  // --- Quad geometry ---
  struct Vert { float x, y, u, v, r, g, b, a; };
  const Vert verts[4] = {
      {d->x0, d->y0, 0.f, 0.f, 1, 1, 1, 1},
      {d->x1, d->y0, 1.f, 0.f, 1, 1, 1, 1},
      {d->x1, d->y1, 1.f, 1.f, 1, 1, 1, 1},
      {d->x0, d->y1, 0.f, 1.f, 1, 1, 1, 1},
  };
  const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};

  // --- Upload & draw ---
  glBindVertexArray(g_gl.vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_gl.vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_DYNAMIC_DRAW);

  glUseProgram(g_gl.prog);
  glUniformMatrix4fv(g_gl.loc_proj,  1, GL_FALSE, &proj[0][0]);
  glUniform1i(g_gl.loc_tex,   0);
  glUniform1f(g_gl.loc_hover, d->hover_t);
  glUniform1f(g_gl.loc_time,  d->time);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, d->tex);

  // Apply the clip rect that was active when AddCallback was called.
  const float sx = d->fb_scale_x, sy = d->fb_scale_y;
  const float ox = d->disp_x,     oy = d->disp_y;
  int scx = (int)((d->cr_x - ox) * sx);
  int scy = (int)((d->cr_y - oy) * sy);
  int scw = (int)((d->cr_z - ox) * sx) - scx;
  int sch = (int)((d->cr_w - oy) * sy) - scy;
  glScissor(scx, d->fb_h - scy - sch, scw, sch);

  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);

  glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Public widget
// ---------------------------------------------------------------------------

bool shaded_icon_button(const char *id,
                        ImTextureID tex,
                        ImVec2      size,
                        const char *fallback,
                        bool        active)
{
  // Shader not ready yet: plain fallback (ImageButton or text Button).
  if(!g_gl.ready)
  {
    if(active)
    {
      ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.47f, 0.49f, 0.53f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.56f, 0.58f, 0.62f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.63f, 0.65f, 0.69f, 1.0f));
    }
    bool p = tex
      ? ImGui::ImageButton(id, tex, size, ImVec2(0,0), ImVec2(1,1),
                           ImVec4(0,0,0,0), ImVec4(1,1,1,1))
      : ImGui::Button(fallback);
    if(active) ImGui::PopStyleColor(3);
    return p;
  }

  // Text-only button (tex == 0): hover animation without a shader pass.
  if(!tex)
  {
    const bool pressed = ImGui::InvisibleButton(id, nonzero_invisible_button_size(size));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 p_min = ImGui::GetItemRectMin();
    const ImVec2 p_max = ImGui::GetItemRectMax();

    const ImGuiID wid = ImGui::GetID(id);
    float &ht = g_hover[wid];
    const float dt = ImGui::GetIO().DeltaTime;
    ht = std::clamp(ht + (hovered ? dt * 6.0f : -dt * 5.0f), 0.0f, 1.0f);

    const float cx = (p_min.x + p_max.x) * 0.5f;
    const float cy = (p_min.y + p_max.y) * 0.5f;
    const float hw = (p_max.x - p_min.x) * 0.5f;
    const float hh = (p_max.y - p_min.y) * 0.5f;

    ImDrawList *dl = ImGui::GetWindowDrawList();

    if(active)
    {
      const float pad = 2.0f;
      dl->AddRectFilled(
          ImVec2(p_min.x - pad, p_min.y - pad),
          ImVec2(p_max.x + pad, p_max.y + pad),
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.47f, 0.49f, 0.53f, 1.0f)), 3.0f);
    }

    if(ht > 0.01f)
    {
      const float bg_r = std::max(hw, hh) * 1.4f;
      dl->AddCircleFilled(
          ImVec2(cx, cy), bg_r,
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.42f, 0.72f, 1.0f, 0.10f * ht)));
    }

    // Draw text centered; brighten slightly on hover.
    const ImVec2 text_sz = ImGui::CalcTextSize(fallback);
    const float  text_a  = 0.82f + ht * 0.18f;
    dl->AddText(
        ImVec2(cx - text_sz.x * 0.5f, cy - text_sz.y * 0.5f),
        ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, text_a)),
        fallback);

    return pressed;
  }

  // --- Interaction ---
  const bool pressed = ImGui::InvisibleButton(id, nonzero_invisible_button_size(size));
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 p_min = ImGui::GetItemRectMin();
  const ImVec2 p_max = ImGui::GetItemRectMax();

  // --- Hover animation (per-widget lerp) ---
  const ImGuiID wid = ImGui::GetID(id);
  float &ht = g_hover[wid];
  const float dt = ImGui::GetIO().DeltaTime;
  ht = std::clamp(ht + (hovered ? dt * 6.0f : -dt * 5.0f), 0.0f, 1.0f);

  // --- Geometry ---
  const float cx = (p_min.x + p_max.x) * 0.5f;
  const float cy = (p_min.y + p_max.y) * 0.5f;
  const float hw = (p_max.x - p_min.x) * 0.5f;
  const float hh = (p_max.y - p_min.y) * 0.5f;
  const float scale = 1.0f + ht * 0.08f;
  const ImVec2 r_min{cx - hw * scale, cy - hh * scale};
  const ImVec2 r_max{cx + hw * scale, cy + hh * scale};

  ImDrawList *dl = ImGui::GetWindowDrawList();

  // --- Active-state background ---
  if(active)
  {
    const float pad = 2.0f;
    dl->AddRectFilled(
        ImVec2(p_min.x - pad, p_min.y - pad),
        ImVec2(p_max.x + pad, p_max.y + pad),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.47f, 0.49f, 0.53f, 1.0f)),
        3.0f);
  }

  // --- Background illumination (hover) ---
  if(ht > 0.01f)
  {
    const float bg_r = std::max(hw, hh) * 1.4f;
    dl->AddCircleFilled(
        ImVec2(cx, cy),
        bg_r,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.42f, 0.72f, 1.0f, 0.10f * ht)));
  }

  // --- Soft drop shadow (default; fades on hover) ---
  const float shadow_a = 0.28f * (1.0f - ht * 0.75f);
  if(shadow_a > 0.01f)
  {
    dl->AddImage(
        tex,
        ImVec2(r_min.x + 1.5f, r_min.y + 2.0f),
        ImVec2(r_max.x + 1.5f, r_max.y + 2.0f),
        ImVec2(0, 0), ImVec2(1, 1),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, shadow_a)));
  }

  // --- Custom shader pass ---
  CbData *d = alloc_cb();
  if(d)
  {
    d->x0 = r_min.x; d->y0 = r_min.y;
    d->x1 = r_max.x; d->y1 = r_max.y;
    d->tex     = (GLuint)(uintptr_t)tex;
    d->hover_t = ht;
    d->time    = (float)ImGui::GetTime();

    const ImGuiIO &io = ImGui::GetIO();
    const ImVec2   vp = ImGui::GetMainViewport()->Pos;
    d->disp_x    = vp.x;
    d->disp_y    = vp.y;
    d->disp_w    = io.DisplaySize.x;
    d->disp_h    = io.DisplaySize.y;
    d->fb_scale_x = io.DisplayFramebufferScale.x;
    d->fb_scale_y = io.DisplayFramebufferScale.y;
    d->fb_h       = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);

    // Capture the current clip rect from the draw list.
    const ImVec2 cr0 = dl->GetClipRectMin();
    const ImVec2 cr1 = dl->GetClipRectMax();
    d->cr_x = cr0.x; d->cr_y = cr0.y;
    d->cr_z = cr1.x; d->cr_w = cr1.y;

    dl->AddCallback(icon_render_cb, d);
    // Restore ImGui's full render state for subsequent draw commands.
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
  }
  else
  {
    // Pool full fallback: plain image.
    dl->AddImage(tex, r_min, r_max);
  }

  return pressed;
}

} // namespace NoteUi
