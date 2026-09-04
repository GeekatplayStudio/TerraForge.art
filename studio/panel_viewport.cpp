// Geekatplay TerraForge — viewport windows. Each view is its own dockable,
// resizable, floatable window with a Blender-style header toolbar.
#include "app.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gizmo.hpp"
#include "icons.hpp"
#include "panel_float.hpp"
#include "sculpt.hpp"
#include "theme_colors.hpp"
#include <imgui.h>
#include <string>
#include <algorithm>
#include <cstdio>

namespace studio {

static const char *CAMERA_NAMES[] = {"Perspective", "Top", "Front", "Right"};
static const char *DISPLAY_NAMES[] = {"Wireframe", "Solid", "Textured"};
static const char *ENGINE_NAMES[] = {"Rasterized PBR", "Cinematic raymarch"};

const char *view_window_name(int slot) {
  static const char *names[RenderSettings::MAX_VIEWS] = {
      "View 1", "View 2", "View 3", "View 4",
      "View 5", "View 6", "View 7", "View 8"};
  return names[slot < 0 || slot >= RenderSettings::MAX_VIEWS ? 0 : slot];
}

static void draw_scale_bar(ImDrawList *dl, ImVec2 corner, float view_px_w,
                           const RenderSettings::ViewConfig &vc) {
  RenderSettings &rs = render_settings();
  float view_m = renderer_view_width_m(vc);
  if (view_m <= 0) return;
  bool imperial = rs.units == 1;
  float unit_per_m = imperial ? 3.28084f : 1.f;
  float view_units = view_m * unit_per_m;
  float target = view_units / 5.f;
  float mag = std::pow(10.f, std::floor(std::log10(std::max(target, 1e-6f))));
  float bar_units = mag;
  if (target / mag >= 5) bar_units = 5 * mag;
  else if (target / mag >= 2) bar_units = 2 * mag;
  float bar_px = bar_units / view_units * view_px_w;
  if (bar_px < 8 || bar_px > view_px_w) return;
  char label[48];
  if (imperial) {
    if (bar_units >= 5280) snprintf(label, sizeof label, "%.4g mi", bar_units / 5280.f);
    else snprintf(label, sizeof label, "%.4g ft", bar_units);
  } else {
    if (bar_units >= 1000) snprintf(label, sizeof label, "%.4g km", bar_units / 1000.f);
    else snprintf(label, sizeof label, "%.4g m", bar_units);
  }
  ImVec2 p0(corner.x - bar_px - 14, corner.y - 16);
  ImVec2 p1(corner.x - 14, corner.y - 16);
  ImU32 col = IM_COL32(235, 233, 228, 230);
  ImU32 sh = IM_COL32(0, 0, 0, 140);
  dl->AddLine(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x + 1, p1.y + 1), sh, 3.f);
  dl->AddLine(p0, p1, col, 2.f);
  dl->AddLine(ImVec2(p0.x, p0.y - 4), ImVec2(p0.x, p0.y + 4), col, 2.f);
  dl->AddLine(ImVec2(p1.x, p1.y - 4), ImVec2(p1.x, p1.y + 4), col, 2.f);
  ImVec2 ts = ImGui::CalcTextSize(label);
  ImVec2 tp((p0.x + p1.x - ts.x) * 0.5f, p0.y - ts.y - 3);
  dl->AddText(ImVec2(tp.x + 1, tp.y + 1), sh, label);
  dl->AddText(tp, col, label);
}

// shared options menu — used by the header button and by right-click
static void view_options_menu(App &a, int slot, RenderSettings::ViewConfig &vc) {
  RenderSettings &rs = render_settings();
  const float W = 250.f;
  ImGui::SeparatorText("This view");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Camera", &vc.camera, "Perspective\0Top\0Front\0Right\0");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Shading", &vc.display, "Wireframe\0Solid\0Textured\0");
  studio::Checkbox("Atmosphere", &vc.atmosphere);
  studio::Checkbox("Water", &vc.show_water_view);
  studio::Checkbox("Grid", &vc.grid);
  studio::Checkbox("Selection outline", &vc.outlines);

  ImGui::SeparatorText("Viewport windows");
  // Arranging the viewport area, without disturbing anything else on screen.
  unsigned mask = prefs().view_mask;
  int open_count = 0;
  for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i)
    if (mask & (1u << i)) ++open_count;
  ImGui::TextDisabled("Arrange:");
  for (int n = 1; n <= RenderSettings::MAX_VIEWS; ++n) {
    char lbl[8];
    snprintf(lbl, sizeof lbl, "%d", n);
    bool active = open_count == n;
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
    if (ImGui::Button(lbl, ImVec2(30, 0))) views_arrange(a, n);
    if (active) ImGui::PopStyleColor();
    if (n < RenderSettings::MAX_VIEWS) ImGui::SameLine();
  }
  if (ImGui::Button("Split right", ImVec2(96, 0)))
    view_split(a, a.view_focus, false);
  ImGui::SameLine();
  if (ImGui::Button("Split down", ImVec2(96, 0)))
    view_split(a, a.view_focus, true);
  ImGui::TextDisabled("Each view is a normal window: drag its tab to\n"
                      "move, split, float or re-dock it. The\n"
                      "arrangement is remembered; View > Layouts\n"
                      "saves it by name.");

  ImGui::SeparatorText("Real-time engine");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Engine", &rs.viewport_engine,
               "Rasterized PBR\0Cinematic raymarch\0");
  ImGui::TextDisabled("Cinematic adds raymarched AO, softer shadows,\n"
                      "cloud shadows and sharper reflections.");

  ImGui::SeparatorText("Background (all views)");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Mode", &rs.background_mode, "Sky\0Gradient\0Solid color\0");
  if (rs.background_mode != 0) {
    ImGui::ColorEdit3("Color", rs.bg_color);
    if (rs.background_mode == 1) ImGui::ColorEdit3("Bottom", rs.bg_color2);
  }

  ImGui::SeparatorText("Units & scale");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Units", &rs.units, "Metric\0Imperial\0");
  ImGui::SetNextItemWidth(W);
  ImGui::DragFloat("Terrain size (m)", &rs.terrain_size_m, 50.f, 100.f, 100000.f,
                   "%.0f");
}

static void view_options_popup(App &a, int slot, RenderSettings::ViewConfig &vc,
                               const char *id) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 9));
  if (ImGui::BeginPopup(id)) {
    view_options_menu(a, slot, vc);
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
}

// The view's own controls: right-aligned, and grouped by what they answer.
// The old strip put "which way am I looking" next to "what am I drawing" in
// one row of look-alike text buttons, and clipped the moment the font grew.
// Here projection, shading and overlays are three groups of icons, the strip
// is measured before it is drawn, and it steps down to a compact form rather
// than running off the edge of a narrow view.
static void view_header(App &a, int slot, RenderSettings::ViewConfig &vc) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 3));
  // float-out / dock-back sits in the far corner; everything else stops
  // short of it
  panel_float_controls(a, view_window_name(slot));
  sculpt_toolbar(a); // the tools live left, the view controls right
  ImGui::SameLine();
  const float used = ImGui::GetCursorPosX();

  const ImGuiStyle &st = ImGui::GetStyle();
  const float bw = ImGui::GetFontSize() + st.FramePadding.y * 2.f + 6.f;
  const float gap = 2.f, sep = 10.f;
  const float full = bw * 12.f + gap * 8.f + sep * 3.f;
  const float right = ImGui::GetContentRegionMax().x - bw - 6.f;

  auto pick = [&](Icon ic, const char *id, const char *tip, int *value, int on) {
    if (IconButton(ic, id, tip, *value == on)) *value = on;
    ImGui::SameLine(0, gap);
  };
  auto flag = [&](Icon ic, const char *id, const char *tip, bool *v) {
    if (IconButton(ic, id, tip, *v)) *v = !*v;
    ImGui::SameLine(0, gap);
  };
  auto divider = [&] {
    ImGui::SameLine(0, sep * 0.5f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 2.f),
                                        ImVec2(p.x, p.y + bw - 2.f),
                                        theme::fade(theme::text_dim(), 0.7f));
    ImGui::SameLine(0, sep * 0.5f);
  };

  if (right - used >= full) {
    ImGui::SetCursorPosX(right - full);
    pick(Icon::ViewPersp, "##vp", "Perspective", &vc.camera, 0);
    pick(Icon::ViewTop, "##vt", "Top (orthographic, looking down)", &vc.camera, 1);
    pick(Icon::ViewFront, "##vf", "Front (orthographic)", &vc.camera, 2);
    pick(Icon::ViewRight, "##vr", "Right (orthographic)", &vc.camera, 3);
    divider();
    pick(Icon::Wireframe, "##sw", "Wireframe", &vc.display, 0);
    pick(Icon::Shaded, "##ss", "Solid", &vc.display, 1);
    pick(Icon::Textured, "##sx", "Textured", &vc.display, 2);
    divider();
    flag(Icon::Sky, "##oa", "Sky, fog and clouds in this view", &vc.atmosphere);
    flag(Icon::Water, "##ow", "Show the water surface", &vc.show_water_view);
    flag(Icon::Grid, "##og", "Ground reference grid", &vc.grid);
    flag(Icon::Outline, "##oo", "Outline the selected object", &vc.outlines);
    divider();
  } else {
    const float compact = 96.f + 84.f + bw + gap * 2.f;
    if (right - used >= compact) {
      ImGui::SetCursorPosX(right - compact);
      ImGui::SetNextItemWidth(96);
      ImGui::Combo("##cam", &vc.camera, "Perspective\0Top\0Front\0Right\0");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Projection");
      ImGui::SameLine(0, gap);
      ImGui::SetNextItemWidth(84);
      ImGui::Combo("##disp", &vc.display, "Wireframe\0Solid\0Textured\0");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shading");
      ImGui::SameLine(0, gap);
    } else {
      ImGui::SetCursorPosX(right - bw); // room for the gear and nothing else
    }
  }
  if (IconButton(Icon::Gear, "##vopt",
                 "View options: engine, background, units and layout"))
    ImGui::OpenPopup("view_more");
  view_options_popup(a, slot, vc, "view_more");
  ImGui::PopStyleVar();
}

// ---------------------------------------------------------- orientation gizmo
// A tripod of the world axes in the corner: the fastest way to know which way
// a view is facing, and — clicked — the fastest way to face somewhere else.
// Positive axes are solid balls with a letter, negative ones hollow, and the
// six are painted back to front so the near axis reads as nearer.
namespace {
struct AxisBall {
  ImVec2 p;
  float depth;
  ImU32 col;
  char lbl;
  int axis;
  bool neg;
};

void orient_balls(const RenderSettings::ViewConfig &vc, ImVec2 c, float R,
                 AxisBall out[6]) {
  float rt[3], up[3], fw[3];
  renderer_view_basis(vc, rt, up, fw);
  const ImU32 AX[3] = {IM_COL32(226, 92, 84, 255), IM_COL32(150, 200, 92, 255),
                       IM_COL32(88, 150, 235, 255)};
  const char N[3] = {'X', 'Y', 'Z'};
  int n = 0;
  for (int ax = 0; ax < 3; ++ax)
    for (int s = 0; s < 2; ++s) {
      float d[3] = {0, 0, 0};
      d[ax] = s ? -1.f : 1.f;
      float sx = d[0] * rt[0] + d[1] * rt[1] + d[2] * rt[2];
      float sy = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
      float sz = d[0] * fw[0] + d[1] * fw[1] + d[2] * fw[2];
      out[n++] = {ImVec2(c.x + sx * R, c.y - sy * R), sz, AX[ax], N[ax], ax,
                  s == 1};
    }
  // painter's order: largest depth (furthest along the view direction) first
  for (int i = 0; i < 6; ++i)
    for (int j = i + 1; j < 6; ++j)
      if (out[j].depth > out[i].depth) std::swap(out[i], out[j]);
}

// index of the ball under `m`, or -1
int orient_hit(const AxisBall b[6], ImVec2 m, float br) {
  int best = -1;
  float bestd = br * br;
  for (int i = 0; i < 6; ++i) {
    float dx = b[i].p.x - m.x, dy = b[i].p.y - m.y;
    float d2 = dx * dx + dy * dy;
    if (d2 <= bestd) { bestd = d2; best = i; }
  }
  return best;
}

void orient_draw(ImDrawList *dl, const AxisBall b[6], ImVec2 c, float br,
                int hot) {
  for (int i = 0; i < 6; ++i) {
    // depth in [-1,1]; nearer axes are drawn brighter
    float t = 0.55f + 0.45f * (0.5f - b[i].depth * 0.5f);
    ImU32 col = theme::fade(b[i].col, t);
    if (!b[i].neg) dl->AddLine(c, b[i].p, col, 2.f);
    float r = b[i].neg ? br * 0.82f : br;
    if (b[i].neg) {
      dl->AddCircleFilled(b[i].p, r, IM_COL32(28, 28, 28, 190), 16);
      dl->AddCircle(b[i].p, r, col, 16, 1.6f);
    } else {
      dl->AddCircleFilled(b[i].p, r, col, 16);
      char s[2] = {b[i].lbl, 0};
      ImVec2 ts = ImGui::CalcTextSize(s);
      dl->AddText(ImVec2(b[i].p.x - ts.x * 0.5f, b[i].p.y - ts.y * 0.5f),
                  IM_COL32(22, 22, 22, 255), s);
    }
    if (i == hot) dl->AddCircle(b[i].p, r + 2.5f, IM_COL32(255, 255, 255, 210),
                                20, 1.6f);
  }
}
} // namespace

static void view_body(App &a, int slot, RenderSettings::ViewConfig &vc) {
  ImVec2 avail = ImGui::GetContentRegionAvail();
  int w = (int)avail.x, h = (int)avail.y;
  if (w < 16 || h < 16) return;
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  unsigned tex = renderer_draw_view(slot, vc, w, h, ImGui::GetIO().DeltaTime);
  ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2((float)w, (float)h), ImVec2(0, 1),
               ImVec2(1, 0));

  // The orientation gizmo is laid out before input is read: a drag that starts
  // on it must swing the view, not orbit the camera underneath it.
  const float gizR = std::clamp(std::min(w, h) * 0.075f, 20.f, 36.f);
  const float gizBR = gizR * 0.30f;
  ImVec2 gizC(p0.x + w - gizR - 16.f, p0.y + gizR + 16.f);
  AxisBall balls[6];
  orient_balls(vc, gizC, gizR, balls);
  int giz_hot = -1;

  // The transform gizmo gets first refusal on the mouse: a drag that starts
  // on a handle must move the object, not orbit past it.
  bool hovered_img = ImGui::IsItemHovered();
  bool xform_owns =
      gizmo_update(a, slot, vc, p0, w, h, hovered_img);

  if (hovered_img && !xform_owns) {
    ImGuiIO &io = ImGui::GetIO();
    giz_hot = orient_hit(balls, io.MousePos, gizBR + 3.f);
    if (giz_hot >= 0) {
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const AxisBall &b = balls[giz_hot];
        if (vc.camera == 0) renderer_camera_snap_axis(b.axis, b.neg);
        else vc.camera = b.axis == 1 ? 1 : (b.axis == 2 ? 2 : 3);
      }
      ImGui::SetTooltip("Look down %s%c", balls[giz_hot].neg ? "-" : "+",
                        balls[giz_hot].lbl);
    }
  }
  if (hovered_img && !xform_owns && giz_hot < 0) {
    ImGuiIO &io = ImGui::GetIO();
    SculptState &SC = sculpt_state();
    float u = (io.MousePos.x - p0.x) / (float)w;
    float v = (io.MousePos.y - p0.y) / (float)h;

    // sculpt mode: the left button brushes instead of orbiting the camera
    bool sculpting = SC.active;
    if (sculpting) {
      float tx, tz;
      bool on_terrain = renderer_pick_terrain(slot, vc, u, v, w, h, tx, tz);
      bool erase_look = SC.tool == SculptTool::Erase ||
                        (SC.invert != io.KeyAlt); // live Alt flips the ring
      if (on_terrain)
        renderer_set_brush_cursor(tx, tz, SC.radius,
                                  SC.tool == SculptTool::Erase ? true
                                                               : erase_look);
      // brush size on the wheel — the shortcut every sculpting app shares
      if (io.MouseWheel != 0.f) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        SC.radius = std::clamp(SC.radius * (io.MouseWheel > 0 ? 1.12f : 0.89f),
                               0.005f, 0.4f);
      }
      if (on_terrain && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
          !io.KeyCtrl) {
        bool saved_inv = SC.invert;
        if (io.KeyAlt) SC.invert = !SC.invert;
        sculpt_apply(a, tx, tz, io.DeltaTime);
        SC.invert = saved_inv;
      }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) sculpt_end_stroke(a);
    }

    bool rot = !sculpting && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
               ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.f);
    bool pan = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
               ImGui::IsMouseDown(ImGuiMouseButton_Right);
    // Ctrl+drag dollies (moves the camera along its view axis)
    bool dolly = io.KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    float wheel = sculpting ? 0.f : io.MouseWheel;
    if (vc.camera == 0)
      renderer_camera_input(io.MouseDelta.x, io.MouseDelta.y, wheel,
                            rot && !dolly, pan, dolly);
    else
      renderer_view_input(vc, io.MouseDelta.x, io.MouseDelta.y, wheel, rot,
                          pan, w);
    // click (without dragging) selects the object under the cursor
    if (!sculpting && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.f)) {
      int hit = renderer_pick(slot, vc, u, v, w, h);
      if (hit >= 0) {
        scene().selected = hit; // shared: updates every view and the panels
        a.scene_selection_serial++;
      }
    }
    // right-click (without panning) opens the same options menu
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Right, 3.f))
      ImGui::OpenPopup("view_ctx");
  }
  view_options_popup(a, slot, vc, "view_ctx");
  ImDrawList *dl = ImGui::GetWindowDrawList();
  // corner labels
  ImU32 sh = IM_COL32(0, 0, 0, 150), fg = IM_COL32(225, 222, 216, 210);
  std::string cam = CAMERA_NAMES[vc.camera & 3];
  if (vc.camera == 0)
    cam += std::string("  ·  ") +
           ENGINE_NAMES[render_settings().viewport_engine & 1];
  dl->AddText(ImVec2(p0.x + 9, p0.y + 7), sh, cam.c_str());
  dl->AddText(ImVec2(p0.x + 8, p0.y + 6), fg, cam.c_str());
  // selected object name
  SceneState &sc = scene();
  if (sc.selected >= 0 && sc.selected < (int)sc.objects.size()) {
    std::string sel = "selected: " + sc.objects[sc.selected].name;
    dl->AddText(ImVec2(p0.x + 9, p0.y + h - 21), sh, sel.c_str());
    dl->AddText(ImVec2(p0.x + 8, p0.y + h - 22), IM_COL32(230, 150, 70, 230),
                sel.c_str());
  }
  draw_scale_bar(dl, ImVec2(p0.x + w, p0.y + h), (float)w, vc);
  orient_draw(dl, balls, gizC, gizBR, giz_hot);
  gizmo_draw(a, slot, vc, p0, w, h);
}

void draw_panel_viewport(App &a) {
  RenderSettings &rs = render_settings();
  // the brush ring only lives while a hovered view re-arms it each frame
  renderer_set_brush_cursor(0, 0, -1.f, false);
  // a stroke that leaves the window still ends when the button comes up
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) sculpt_end_stroke(a);
  // The open viewports, in slot order. A closed slot is skipped entirely -
  // the set is what Prefs::view_mask holds, so closing View 2 of four leaves
  // 1, 3 and 4 where they are instead of renumbering them.
  const unsigned mask = prefs().view_mask ? prefs().view_mask : 1u;
  int closing = -1;
  for (int slot = 0; slot < RenderSettings::MAX_VIEWS; ++slot) {
    if (!(mask & (1u << slot))) continue;
    // Only offer the close box while another viewport would remain.
    const bool last_one = (mask & ~(1u << slot)) == 0;
    bool stay = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    panel_float_prepare(a, view_window_name(slot));
    bool open = ImGui::Begin(view_window_name(slot),
                             last_one ? nullptr : &stay,
                             ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!stay) closing = slot;
    // Where a new viewport goes when the user asks for one.
    if (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) a.view_focus = slot;
    if (open) {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
      ImGui::BeginChild("##hdr", ImVec2(0, ImGui::GetFrameHeight() + 8),
                        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
      view_header(a, slot, rs.views[slot]);
      ImGui::EndChild();
      ImGui::PopStyleVar();
      view_body(a, slot, rs.views[slot]);
    }
    ImGui::End();
  }
  // Closed after the loop: the mask must not change while it is being walked.
  if (closing >= 0) view_close(a, closing);
}

} // namespace studio




