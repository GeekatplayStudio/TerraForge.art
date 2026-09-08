// Geekatplay TerraForge — the height painter. See paint_canvas.hpp.
#include "paint_canvas.hpp"
#include "paint_canvas_internal.hpp"
#include "app.hpp"
#include "graph_lease.hpp"
#include "icons.hpp"
#include "panel_float.hpp"
#include "sculpt.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>

namespace studio {

PaintCanvasState &paint_canvas() {
  static PaintCanvasState s;
  return s;
}

namespace {

CanvasTex &g_tex = canvas_tex();

// Note the ground a stroke covered, so only that is rebuilt next frame.
void mark_brushed(PaintCanvasState &, float u0, float v0, float u1, float v1,
                  float radius) {
  if (g_tex.w <= 0 || g_tex.h <= 0) return;
  const float pad = radius + 2.f / g_tex.w;
  const float lo_u = std::min(u0, u1) - pad, hi_u = std::max(u0, u1) + pad;
  const float lo_v = std::min(v0, v1) - pad, hi_v = std::max(v0, v1) + pad;
  const int x0 = std::clamp((int)(lo_u * g_tex.w), 0, g_tex.w - 1);
  const int x1 = std::clamp((int)(hi_u * g_tex.w) + 1, 0, g_tex.w - 1);
  const int y0 = std::clamp((int)(lo_v * g_tex.h), 0, g_tex.h - 1);
  const int y1 = std::clamp((int)(hi_v * g_tex.h) + 1, 0, g_tex.h - 1);
  if (!g_tex.has_rect()) {
    g_tex.rx0 = x0; g_tex.ry0 = y0; g_tex.rx1 = x1; g_tex.ry1 = y1;
  } else {
    g_tex.rx0 = std::min(g_tex.rx0, x0);
    g_tex.ry0 = std::min(g_tex.ry0, y0);
    g_tex.rx1 = std::max(g_tex.rx1, x1);
    g_tex.ry1 = std::max(g_tex.ry1, y1);
  }
}

// What the panel needs after it has let go of the graph. Everything below the
// lease works from this: a gpx::Node* would be a pointer into a structure the
// evaluation thread is allowed to change the moment the lock is released.
struct LayerInfo {
  uint64_t node = 0;
  std::string name;
  int fw = 0, fh = 0;
  float lo = -1.f, hi = 1.f;
  bool valid = false;
};



void upload(const std::vector<uint8_t> &rgba, int w, int h) {
  if (!g_tex.tex) glGenTextures(1, &g_tex.tex);
  glBindTexture(GL_TEXTURE_2D, g_tex.tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba.data());
  g_tex.w = w;
  g_tex.h = h;
}

// The layer as a picture: the field's range mapped to black..white, so the
// value that does nothing lands on mid grey for a sculpt (-1..1) and on black
// for a mask (0..1) - in both cases exactly what the number means.
//
// `terrain` tints it, so a river being drawn can be seen against the hills it
// has to run between. It is a tint and not a blend: the grey has to stay
// readable as a value, because that is the whole point of painting one.
// Repaint only the texels a stroke touched. The colour of a texel depends on
// nothing but itself, so a rectangle can be redone on its own - which is what
// keeps a stroke costing a few thousand pixels instead of a megabyte a frame.
void rebuild_rect(const gpx::Attribute *fa, const gpx::Heightmap *terrain,
                  bool show_terrain, int x0, int y0, int x1, int y1);

void rebuild(gpx::Node *n, const gpx::Attribute *fa, const gpx::Heightmap *terrain,
             bool show_terrain) {
  const int w = fa->fw, h = fa->fh;
  const float lo = fa->fmin, hi = fa->fmax;
  const float span = (hi - lo) > 1e-9f ? (hi - lo) : 1.f;
  float tmn = 0.f, tmx = 1.f;
  if (terrain && !terrain->empty()) terrain->minmax(tmn, tmx);
  const float tspan = (tmx - tmn) > 1e-9f ? (tmx - tmn) : 1.f;

  std::vector<uint8_t> rgba((size_t)w * h * 4, 255);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = (size_t)y * w + x;
      float g = fa->field.empty() ? (0.f - lo) / span
                                  : (fa->field[i] - lo) / span;
      g = std::clamp(g, 0.f, 1.f);
      float r = g, gg = g, b = g;
      if (show_terrain && terrain && !terrain->empty()) {
        const float t = std::clamp(
            (terrain->sample(x / float(w - 1), y / float(h - 1)) - tmn) / tspan,
            0.f, 1.f);
        // A cool cast where the ground is low, warm where it is high, kept
        // faint on purpose: the grey has to stay judgeable as a value, which
        // is the whole reason for painting one, so the terrain is a hint of
        // where the hills are and never a picture in its own right.
        r = std::clamp(g * (0.94f + 0.11f * t), 0.f, 1.f);
        gg = std::clamp(g * (0.95f + 0.06f * t), 0.f, 1.f);
        b = std::clamp(g * (1.02f - 0.09f * t), 0.f, 1.f);
      }
      rgba[i * 4 + 0] = (uint8_t)(r * 255.f);
      rgba[i * 4 + 1] = (uint8_t)(gg * 255.f);
      rgba[i * 4 + 2] = (uint8_t)(b * 255.f);
    }
  upload(rgba, w, h);
  g_tex.value = fa->field;
  g_tex.node = n->id;
  g_tex.dirty = false;
  g_tex.clear_rect();
}

// One texel's colour, shared by the whole rebuild and the rectangle one so
// the two can never drift apart and leave a seam where a stroke was.
inline void shade_texel(const gpx::Attribute *fa, const gpx::Heightmap *terrain,
                        bool show_terrain, int x, int y, int w, int h, float lo,
                        float span, float tmn, float tspan, uint8_t *out) {
  const size_t i = (size_t)y * w + x;
  float g = fa->field.empty() ? (0.f - lo) / span : (fa->field[i] - lo) / span;
  g = std::clamp(g, 0.f, 1.f);
  float r = g, gg = g, b = g;
  if (show_terrain && terrain && !terrain->empty()) {
    const float t = std::clamp(
        (terrain->sample(x / float(w - 1), y / float(h - 1)) - tmn) / tspan, 0.f,
        1.f);
    r = std::clamp(g * (0.94f + 0.11f * t), 0.f, 1.f);
    gg = std::clamp(g * (0.95f + 0.06f * t), 0.f, 1.f);
    b = std::clamp(g * (1.02f - 0.09f * t), 0.f, 1.f);
  }
  out[0] = (uint8_t)(r * 255.f);
  out[1] = (uint8_t)(gg * 255.f);
  out[2] = (uint8_t)(b * 255.f);
  out[3] = 255;
}

void rebuild_rect(const gpx::Attribute *fa, const gpx::Heightmap *terrain,
                  bool show_terrain, int x0, int y0, int x1, int y1) {
  const int w = fa->fw, h = fa->fh;
  if (g_tex.w != w || g_tex.h != h || !g_tex.tex) return; // caller falls back
  x0 = std::clamp(x0, 0, w - 1);
  x1 = std::clamp(x1, 0, w - 1);
  y0 = std::clamp(y0, 0, h - 1);
  y1 = std::clamp(y1, 0, h - 1);
  if (x1 < x0 || y1 < y0) return;
  const int rw = x1 - x0 + 1, rh = y1 - y0 + 1;
  const float lo = fa->fmin;
  const float span = (fa->fmax - lo) > 1e-9f ? (fa->fmax - lo) : 1.f;
  float tmn = 0.f, tmx = 1.f;
  if (terrain && !terrain->empty()) terrain->minmax(tmn, tmx);
  const float tspan = (tmx - tmn) > 1e-9f ? (tmx - tmn) : 1.f;

  std::vector<uint8_t> rgba((size_t)rw * rh * 4, 255);
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
      shade_texel(fa, terrain, show_terrain, x, y, w, h, lo, span, tmn, tspan,
                  &rgba[((size_t)(y - y0) * rw + (x - x0)) * 4]);
  glBindTexture(GL_TEXTURE_2D, g_tex.tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE,
                  rgba.data());
  // keep the readout's copy in step with what is on screen
  if (g_tex.value.size() == fa->field.size())
    for (int y = y0; y <= y1; ++y)
      std::copy(fa->field.begin() + (size_t)y * w + x0,
                fa->field.begin() + (size_t)y * w + x1 + 1,
                g_tex.value.begin() + (size_t)y * w + x0);
  g_tex.clear_rect();
}

} // namespace

CanvasTex &canvas_tex() {
  static CanvasTex t;
  return t;
}

gpx::Attribute *layer_field(gpx::Node *n) {
  if (!n) return nullptr;
  return n->attrs.find(n->type == "MaskPaint" ? "strokes" : "delta");
}

void paint_canvas_invalidate() { canvas_tex().dirty = true; }


void draw_panel_paint_canvas(App &a) {
  if (!a.show_paint_canvas) {
    paint_canvas().was_shown = false;
    return;
  }
  PaintCanvasState &C = paint_canvas();
  ImGui::SetNextWindowSize(ImVec2(720, 640), ImGuiCond_FirstUseEver);
  // Opening it has to show it. Docked into a tab beside other panels, a
  // window that is merely "visible" can be behind one of them, so switching
  // it on from the menu appears to do nothing at all.
  if (!C.was_shown) ImGui::SetNextWindowFocus();
  C.was_shown = true;
  panel_float_prepare(a, "Height Paint");
  if (!ImGui::Begin("Height Paint", &a.show_paint_canvas)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Height Paint");

  // [ and ] resize the brush, as in every painting application. Photoshop's
  // direction: [ smaller, ] larger. Geometric steps, so the key does the same
  // proportional thing at a hairline and at half the tile.
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    SculptState &S = sculpt_state();
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true))
      S.radius = std::max(S.radius / 1.15f, 0.002f);
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true))
      S.radius = std::min(S.radius * 1.15f, 1.f);
  }

  // The painter opens on Shade. The brush state is shared with viewport
  // sculpting, where Raise is the right default and must stay it - so this is
  // done once, when the window is first opened, rather than every frame,
  // which would take the tool away from somebody who had just changed it.
  static bool first_open = true;
  if (first_open) {
    first_open = false;
    sculpt_state().tool = SculptTool::Shade;
  }

  // Everything that reads the graph happens here, under the lease, and the
  // lease is given back before anything else runs.
  //
  // It has to be: the brush takes the graph lock itself, and so do Clear and
  // Open image. Holding it across them means a thread asking for a lock it
  // already owns, which try_lock answers with "no" - so every stroke was
  // quietly dropped and the canvas never changed. Nothing below this block
  // may touch a.graph or a gpx::Node.
  LayerInfo L;
  {
    GraphLease lk(a);
    if (lk.owns_lock()) {
      // The node the brush would paint, without creating one: a canvas that
      // spliced a sculpt layer into the graph merely by being opened would be
      // an edit nobody asked for. The first stroke makes it (sculpt_apply
      // does), which is the moment the user has actually asked.
      gpx::Node *n = a.graph.find_node(C.node);
      if (!n || !layer_field(n)) {
        n = a.graph.find_node(a.selected_node);
        if (!n || (n->type != "MaskPaint" && n->type != "TerrainSculpt")) {
          n = nullptr;
          for (auto &c : a.graph.nodes)
            if (c->type == "TerrainSculpt") n = c.get();
        }
      }
      const gpx::Attribute *fa = layer_field(n);
      if (n && fa && fa->fw > 0 && fa->fh > 0) {
        C.node = n->id;
        L = {n->id, gpx::node_display_name(n->type), fa->fw, fa->fh, fa->fmin,
             fa->fmax, true};
        const gpx::Heightmap *under =
            C.show_terrain ? n->in_hmap("input") : nullptr;
        const bool whole = g_tex.dirty || g_tex.node != n->id ||
                           g_tex.w != fa->fw || g_tex.h != fa->fh;
        if (whole) {
          g_tex.seen_eval = a.eval_serial;
          rebuild(n, fa, under, C.show_terrain);
        } else if (g_tex.has_rect()) {
          // mid-stroke: only the ground the brush covered
          rebuild_rect(fa, under, C.show_terrain, g_tex.rx0, g_tex.ry0,
                       g_tex.rx1, g_tex.ry1);
          g_tex.seen_eval = a.eval_serial;
        } else if (g_tex.seen_eval != a.eval_serial) {
          // something else changed the layer - an undo, a load, a script
          g_tex.seen_eval = a.eval_serial;
          rebuild(n, fa, under, C.show_terrain);
        }
      }
    } else if (g_tex.node) {
      // A busy frame keeps the last picture rather than emptying the panel.
      L = {g_tex.node, "Terrain sculpt", g_tex.w, g_tex.h, -1.f, 1.f, true};
    }
  }

  paint_toolbar(a, C, L.valid ? L.name : std::string(), L.node);
  ImGui::Separator();

  if (!L.valid) {
    ImGui::TextWrapped(
        "Nothing painted yet. Draw here and a sculpt layer is added to the "
        "graph, before the terrain output - so everything downstream of it, "
        "erosion included, keeps working on what you paint.");
    ImGui::End();
    return;
  }

  // ------------------------------------------------------------- the canvas
  //
  // The quad always fills the panel; zoom and pan move the *picture* inside
  // it by changing which part of the texture is shown. Growing the quad
  // instead makes the window's content bigger than the window, which ImGui
  // answers with scrollbars and a layout that shifts under the cursor - and
  // a canvas whose contents move while you paint on them is unusable.
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float side = std::max(32.f, std::min(avail.x, std::max(64.f, avail.y - 24.f)));
  const ImVec2 origin(ImGui::GetCursorScreenPos().x +
                          std::max(0.f, (avail.x - side) * 0.5f),
                      ImGui::GetCursorScreenPos().y);
  ImGui::SetCursorScreenPos(origin);
  ImGui::InvisibleButton("##canvas", ImVec2(side, side),
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();

  // the visible window into the layer, in 0..1 canvas coordinates
  const float half = 0.5f / std::max(C.zoom, 1e-3f);
  C.pan_x = std::clamp(C.pan_x, half, 1.f - half);
  C.pan_y = std::clamp(C.pan_y, half, 1.f - half);
  const ImVec2 uv0(C.pan_x - half, C.pan_y - half);
  const ImVec2 uv1(C.pan_x + half, C.pan_y + half);

  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddImage((ImTextureID)(intptr_t)g_tex.tex, origin,
               ImVec2(origin.x + side, origin.y + side), uv0, uv1);
  dl->AddRect(origin, ImVec2(origin.x + side, origin.y + side),
              IM_COL32(90, 90, 90, 255));

  const ImVec2 m = ImGui::GetIO().MousePos;
  const float u = uv0.x + (m.x - origin.x) / side * (uv1.x - uv0.x);
  const float v = uv0.y + (m.y - origin.y) / side * (uv1.y - uv0.y);
  const bool on_canvas = m.x >= origin.x && m.x <= origin.x + side &&
                         m.y >= origin.y && m.y <= origin.y + side;

  // the brush, drawn where it will land and at the size it will cover
  if (hovered && on_canvas) {
    const float rr = sculpt_state().radius * side * C.zoom;
    dl->AddCircle(m, rr, IM_COL32(255, 255, 255, 180), 48, 1.5f);
    dl->AddCircle(m, rr * 0.5f, IM_COL32(255, 255, 255, 60), 32, 1.f);
  }

  // painting: the same brush the viewport uses, given canvas coordinates
  // instead of a ray hit, and the same interactive evaluation - so a stroke
  // is a low-resolution pass per frame and one full pass on release.
  static bool painting = false;
  static float last_u = 0.f, last_v = 0.f;
  if (hovered && on_canvas && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const float cu = std::clamp(u, 0.f, 1.f), cv = std::clamp(v, 0.f, 1.f);
    // The whole segment the pointer covered, in one call: the brush stamps
    // along it under a single lock rather than one lock, dirty walk and
    // worker wake-up per dab.
    if (!painting) { last_u = cu; last_v = cv; }
    sculpt_apply_segment(a, last_u, last_v, cu, cv, ImGui::GetIO().DeltaTime);
    // Only the ground the brush covered needs re-uploading; rebuilding the
    // whole canvas is a megabyte a frame for a stroke that touched a circle.
    mark_brushed(C, last_u, last_v, cu, cv, sculpt_state().radius);
    last_u = cu;
    last_v = cv;
    painting = true;
  } else if (painting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    sculpt_end_stroke(a);
    painting = false;
  }

  // zoom on the wheel, pan on the middle button - the two things a canvas
  // has to do before it is usable at all
  if (hovered) {
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.f) {
      ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
      C.zoom = std::clamp(C.zoom * (1.f + wheel * 0.15f), 1.f, 16.f);
      if (C.zoom <= 1.f) { C.pan_x = 0.5f; C.pan_y = 0.5f; }
    }
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    C.pan_x -= ImGui::GetIO().MouseDelta.x / side / C.zoom;
    C.pan_y -= ImGui::GetIO().MouseDelta.y / side / C.zoom;
  }

  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + side + 4));
  if (hovered && on_canvas) {
    // from the copy taken with the picture, not from the graph: the lease was
    // handed back before any of this ran
    const int px = std::clamp((int)(u * (L.fw - 1)), 0, L.fw - 1);
    const int py = std::clamp((int)(v * (L.fh - 1)), 0, L.fh - 1);
    const size_t i = (size_t)py * L.fw + px;
    const float d = i < g_tex.value.size() ? g_tex.value[i] : 0.f;
    const float span = (L.hi - L.lo) > 1e-9f ? L.hi - L.lo : 1.f;
    ImGui::TextDisabled("%d, %d   value %.3f   grey %.2f   %dx%d   zoom %.0f%%",
                        px, py, d, (d - L.lo) / span, L.fw, L.fh,
                        C.zoom * 100.f);
  } else {
    ImGui::TextDisabled("%dx%d   zoom %.0f%%   wheel zooms, middle drag pans",
                        L.fw, L.fh, C.zoom * 100.f);
  }
  ImGui::End();
}

} // namespace studio
