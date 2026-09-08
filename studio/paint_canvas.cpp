// Geekatplay TerraForge — the height painter. See paint_canvas.hpp.
#include "paint_canvas.hpp"
#include "app.hpp"
#include "graph_lease.hpp"
#include "icons.hpp"
#include "panel_float.hpp"
#include "sculpt.hpp"
#include "undo.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>

#define STB_IMAGE_STATIC
#include <stb_image.h>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

PaintCanvasState &paint_canvas() {
  static PaintCanvasState s;
  return s;
}

namespace {

// The canvas texture. Rebuilt only when the painted layer can have changed -
// a stroke, a different node, or an evaluation (which is what undo and an
// image load both end in) - because a 512 canvas is a megabyte a time.
struct CanvasTex {
  unsigned tex = 0;
  int w = 0, h = 0;
  uint64_t node = 0;
  uint64_t seen_eval = ~0ull;
  bool dirty = true;
  // A copy of the layer, so the readout under the canvas can name the value
  // beneath the pointer without holding the graph while it does it. Taken
  // when the picture is, which is the only time it can have changed.
  std::vector<float> value;
} g_tex;

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

// The layer's own field, whichever kind of node it belongs to.
gpx::Attribute *layer_field(gpx::Node *n) {
  if (!n) return nullptr;
  return n->attrs.find(n->type == "MaskPaint" ? "strokes" : "delta");
}

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
}

// ------------------------------------------------------------------ toolbar
struct ToolDef {
  SculptTool tool;
  Icon icon;
  const char *id;   // "##" prefixed: IconButton draws its id as a label
  const char *tip;
};
const ToolDef TOOLS[] = {
    {SculptTool::Shade, Icon::Brush, "##t_shade",
     "Paint a chosen grey.\nMid grey leaves the terrain alone, darker carves,\n"
     "lighter raises. Going over the same ground again deepens\nthe stroke up "
     "to the chosen value and then stops."},
    {SculptTool::Raise, Icon::Raise, "##t_raise",
     "Push the layer up under the brush, and down with Invert.\nUnlike Shade "
     "this keeps going the longer you hold it."},
    {SculptTool::Smooth, Icon::Smooth, "##t_smooth",
     "Relax the painted layer toward its surroundings - the way\nto soften a "
     "bank or blend a stroke into what it meets."},
    {SculptTool::Erase, Icon::Erase, "##t_erase",
     "Take the paint back out, down to the layer doing nothing.\nThe terrain "
     "underneath is untouched, so this undoes\npainting rather than terrain."},
};

// The grey being painted, as a ramp of swatches plus the exact value. The
// ramp is the control people reach for; the number is there because "0.5 is
// neutral" is a fact worth being able to type.
void shade_picker(SculptState &S) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Shade");
  ImGui::SameLine(54.f);
  const float hgt = ImGui::GetFrameHeight();
  const int N = 11;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  for (int i = 0; i < N; ++i) {
    const float g = i / float(N - 1);
    ImGui::PushID(i);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##sw", ImVec2(hgt, hgt))) S.shade = g;
    const bool hot = std::fabs(S.shade - g) < 0.5f / (N - 1);
    dl->AddRectFilled(p, ImVec2(p.x + hgt, p.y + hgt),
                      IM_COL32((int)(g * 255), (int)(g * 255), (int)(g * 255), 255));
    // mid grey is the one that means "no change", so it is marked
    if (i == N / 2)
      dl->AddRect(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + hgt - 2, p.y + hgt - 2),
                  IM_COL32(217, 140, 51, 200));
    if (hot)
      dl->AddRect(p, ImVec2(p.x + hgt, p.y + hgt), IM_COL32(255, 255, 255, 255), 0, 0, 2.f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(i == N / 2 ? "%.2f - mid grey: the layer does nothing here"
                                   : "%.2f", g);
    ImGui::PopID();
    ImGui::SameLine(0, 2);
  }
  ImGui::SetNextItemWidth(90);
  float mn = 0.f, mx = 1.f;
  scalar_float("##shadev", &S.shade, mn, mx, false);
  S.shade = std::clamp(S.shade, 0.f, 1.f);
}

void toolbar(App &a, PaintCanvasState &C, const std::string &layer_name,
             uint64_t layer_id) {
  SculptState &S = sculpt_state();
  const float bw = ImGui::GetFrameHeight() + 4.f;
  for (const ToolDef &t : TOOLS) {
    if (IconButton(t.icon, t.id, t.tip, S.tool == t.tool, bw))
      S.tool = t.tool;
    ImGui::SameLine();
  }
  Checkbox("Invert", &S.invert);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Raise digs instead. Shade is a value, so it ignores this.");

  // One number per row with the labels on a common column: scalar_float sets
  // its own width (it reserves room for its - and + buttons), so it cannot be
  // squeezed into a shared row from outside - three of them on one line put
  // two of them past the right edge of the window.
  const float label_col = 54.f;
  auto num = [&](const char *label, const char *id, float *v, float mn, float mx,
                 const char *tip) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(label_col);
    scalar_float(id, v, mn, mx, false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  };
  float r0 = 0.005f, r1 = 0.5f, f0 = 0.3f, f1 = 8.f, w0 = 0.02f, w1 = 2.f;
  num("Size", "##rad", &S.radius, r0, r1,
      "The brush's width, as a fraction of the tile.");
  // falloff is the brush profile's exponent: low is a wide soft shoulder,
  // high is a nearly hard rim. Named for what it does, not for the maths.
  num("Edge", "##fall", &S.falloff, f0, f1,
      "Soft airbrush at the left, hard pen at the right.");
  num("Flow", "##flow", &S.flow, w0, w1,
      "How fast the stroke builds while the button is held.");

  if (S.tool == SculptTool::Shade) shade_picker(S);

  // second row: the layer, the picture, and what the canvas shows
  ImGui::TextDisabled("Layer");
  ImGui::SameLine();
  if (layer_id) {
    ImGui::TextUnformatted(layer_name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("#%llu", (unsigned long long)layer_id);
  } else {
    ImGui::TextDisabled("none yet - the first stroke makes one");
  }
  ImGui::SameLine();
  if (ImGui::Button("Open image...")) {
    std::string p = dialog_open_file(
        "Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0", "png");
    if (!p.empty()) {
      std::string err;
      if (!paint_canvas_load_image(a, p, err)) C.last_error = err;
      else C.last_error.clear();
      g_tex.dirty = true;
    }
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Load a greyscale picture into this layer, resampled to\n"
                      "its resolution. Mid grey stays neutral, so a photo of a\n"
                      "height map drops straight in.");
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    GraphLease lk(a);
    gpx::Node *n = lk.owns_lock() ? a.graph.find_node(layer_id) : nullptr;
    if (gpx::Attribute *fa = layer_field(n)) {
      undo_push_locked(a, "clear painted layer");
      fa->field.assign((size_t)fa->fw * fa->fh, 0.f);
      a.graph.mark_dirty(n->id);
      a.request_eval();
      g_tex.dirty = true;
    }
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Back to a layer that does nothing. The terrain under it\n"
                      "is not touched.");
  ImGui::SameLine();
  if (Checkbox("Terrain under", &C.show_terrain)) g_tex.dirty = true;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Tint the canvas with the shape coming in, so a river can\n"
                      "be drawn between the hills it has to run through.");
  if (!C.last_error.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.85f, 0.31f, 0.24f, 1.f), "%s", C.last_error.c_str());
  }
}

} // namespace

bool paint_canvas_load_image(App &a, const std::string &path, std::string &err) {
  int w = 0, h = 0, comp = 0;
  unsigned char *px = stbi_load(path.c_str(), &w, &h, &comp, 1);
  if (!px || w <= 0 || h <= 0) {
    err = "could not read " + path;
    if (px) stbi_image_free(px);
    return false;
  }
  GraphLease lk(a);
  if (!lk.owns_lock()) {
    stbi_image_free(px);
    err = "busy - try again";
    return false;
  }
  gpx::Node *n = a.graph.find_node(sculpt_target_node(a));
  gpx::Attribute *fa = layer_field(n);
  if (!fa || fa->fw <= 0 || fa->fh <= 0) {
    stbi_image_free(px);
    err = "no layer to paint into";
    return false;
  }
  undo_push_locked(a, "load painted layer");
  fa->field.assign((size_t)fa->fw * fa->fh, 0.f);
  const float lo = fa->fmin, hi = fa->fmax;
  for (int y = 0; y < fa->fh; ++y)
    for (int x = 0; x < fa->fw; ++x) {
      // nearest is enough and keeps a hand-drawn mask's hard edges hard
      const int sx = std::clamp(x * w / fa->fw, 0, w - 1);
      const int sy = std::clamp(y * h / fa->fh, 0, h - 1);
      const float g = px[(size_t)sy * w + sx] / 255.f;
      fa->field[(size_t)y * fa->fw + x] = lo + g * (hi - lo);
    }
  stbi_image_free(px);
  a.graph.mark_dirty(n->id);
  a.request_eval();
  return true;
}

void draw_panel_paint_canvas(App &a) {
  if (!a.show_paint_canvas) return;
  PaintCanvasState &C = paint_canvas();
  ImGui::SetNextWindowSize(ImVec2(720, 640), ImGuiCond_FirstUseEver);
  panel_float_prepare(a, "Height Paint");
  if (!ImGui::Begin("Height Paint", &a.show_paint_canvas)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Height Paint");

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
        if (g_tex.dirty || g_tex.node != n->id || g_tex.w != fa->fw ||
            g_tex.seen_eval != a.eval_serial) {
          g_tex.seen_eval = a.eval_serial;
          rebuild(n, fa, C.show_terrain ? n->in_hmap("input") : nullptr,
                  C.show_terrain);
        }
      }
    } else if (g_tex.node) {
      // A busy frame keeps the last picture rather than emptying the panel.
      L = {g_tex.node, "Terrain sculpt", g_tex.w, g_tex.h, -1.f, 1.f, true};
    }
  }

  toolbar(a, C, L.valid ? L.name : std::string(), L.node);
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
    const float dt = ImGui::GetIO().DeltaTime;
    // Stamp along the way, not just where the pointer ended up. The brush
    // lands once per frame, so a hand moving at any speed leaves a row of
    // separate dabs with gaps between them instead of a stroke. Stepping at a
    // third of the brush's width is close enough that the dabs overlap into a
    // line, and the frame's worth of flow is divided between them so drawing
    // fast does not also draw harder.
    const float step = std::max(sculpt_state().radius / 3.f, 1.f / 512.f);
    const float dist = painting ? std::sqrt((cu - last_u) * (cu - last_u) +
                                            (cv - last_v) * (cv - last_v))
                                : 0.f;
    const int n = std::clamp((int)(dist / step), 1, 64);
    for (int i = 1; i <= n; ++i) {
      const float t = painting ? (float)i / n : 1.f;
      sculpt_apply(a, last_u + (cu - last_u) * t, last_v + (cv - last_v) * t,
                   dt / n);
    }
    last_u = cu;
    last_v = cv;
    painting = true;
    g_tex.dirty = true;
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
