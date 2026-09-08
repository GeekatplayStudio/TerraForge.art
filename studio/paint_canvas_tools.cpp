// Geekatplay TerraForge - the height painter's toolbar, and the layer as a
// file. Split from paint_canvas.cpp, which was over the module size once the
// painter grew save, load and the brush keys.
#include "paint_canvas.hpp"
#include "paint_canvas_internal.hpp"
#include "app.hpp"
#include "graph_lease.hpp"
#include "icons.hpp"
#include "png16.hpp"
#include "sculpt.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>

#define STB_IMAGE_STATIC
#include <stb_image.h>

namespace studio {

// file_dialogs.cpp
std::string dialog_open_file(const char *filter, const char *def_ext);
std::string dialog_save_file(const char *filter, const char *def_ext,
                             const char *suggested);

namespace {

CanvasTex &g_tex = canvas_tex();

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
     "Rub the paint back out, to mid grey - the value that leaves\nthe terrain "
     "alone. The shape underneath is untouched, so\nthis undoes painting, not "
     "terrain.\n\n[ and ] resize the brush."},
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

void toolbar_impl(App &a, PaintCanvasState &C, const std::string &layer_name,
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
  if (ImGui::Button("Save image...")) {
    std::string p = dialog_save_file("PNG\0*.png\0", "png", "height.png");
    if (!p.empty()) {
      std::string e;
      C.last_error = paint_canvas_save_image(a, p, e) ? std::string() : e;
      if (C.last_error.empty()) a.status = "wrote " + p;
    }
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Write the layer out as a 16-bit greyscale PNG - mid grey\n"
                      "where it does nothing. Sixteen bits because eight is 256\n"
                      "steps of height, and the terracing shows.");
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

void paint_toolbar(App &a, PaintCanvasState &C, const std::string &layer_name,
                   uint64_t layer_id) {
  toolbar_impl(a, C, layer_name, layer_id);
}

bool paint_canvas_save_image(App &a, const std::string &path, std::string &err) {
  // 16-bit, because 8 bits of grey is 256 steps of terrain height and the
  // banding shows the moment the layer is scaled up. Written straight from
  // the field, not from the canvas texture: the texture carries the terrain
  // tint and is a picture of the layer, not the layer.
  std::vector<uint16_t> grey;
  int w = 0, h = 0;
  {
    GraphLease lk(a);
    if (!lk.owns_lock()) {
      err = "busy - try again";
      return false;
    }
    gpx::Node *n = a.graph.find_node(paint_canvas().node);
    const gpx::Attribute *fa = layer_field(n);
    if (!fa || fa->fw <= 0 || fa->fh <= 0 || fa->field.empty()) {
      err = "nothing painted yet";
      return false;
    }
    w = fa->fw;
    h = fa->fh;
    const float lo = fa->fmin;
    const float span = (fa->fmax - lo) > 1e-9f ? (fa->fmax - lo) : 1.f;
    grey.resize((size_t)w * h);
    for (size_t i = 0; i < grey.size(); ++i)
      grey[i] = (uint16_t)(std::clamp((fa->field[i] - lo) / span, 0.f, 1.f) *
                           65535.f + 0.5f);
  }
  // big-endian samples: that is what the PNG format stores, and what every
  // reader expects to find
  std::vector<uint8_t> bytes((size_t)w * h * 2);
  for (size_t i = 0; i < grey.size(); ++i) {
    bytes[i * 2 + 0] = (uint8_t)(grey[i] >> 8);
    bytes[i * 2 + 1] = (uint8_t)(grey[i] & 0xff);
  }
  if (!png_write_gray16(path, w, h, bytes)) {
    err = "could not write " + path;
    return false;
  }
  return true;
}

bool paint_canvas_load_image(App &a, const std::string &path, std::string &err) {
  // 16-bit, so a height map saved from here (or from anywhere else) comes
  // back at the precision it was written. stb promotes an 8-bit file for us,
  // so one path reads both.
  int w = 0, h = 0, comp = 0;
  uint16_t *px = stbi_load_16(path.c_str(), &w, &h, &comp, 1);
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
      const float g = px[(size_t)sy * w + sx] / 65535.f;
      fa->field[(size_t)y * fa->fw + x] = lo + g * (hi - lo);
    }
  stbi_image_free(px);
  a.graph.mark_dirty(n->id);
  a.request_eval();
  return true;
}

} // namespace studio
