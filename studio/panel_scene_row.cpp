// Geekatplay TerraForge — one row of the Objects tree.
//
// A row is a single invisible button the width of the panel, with every
// glyph painted over it and every click resolved by the zone the mouse is
// in. That way there is no gap between widgets that swallows a click, the
// paint-to-inherit brush can cross rows that are not the active item, and a
// drag can start from the name without the dots claiming it.
#include "app.hpp"
#include "panel_scene_internal.hpp"
#include "i18n.hpp"
#include "icons.hpp"
#include "render_settings.hpp"
#include "theme_colors.hpp"
#include "undo.hpp"
#include <cstdio>
#include <cstring>

namespace studio {

namespace {

enum Zone {
  Z_NONE, Z_EXPANDER, Z_ICON, Z_NAME, Z_SWATCH, Z_DOT_VP, Z_DOT_RN, Z_TICK,
  Z_CAM, Z_CHIP_MAT, Z_CHIP_DRV, Z_CHIP_SCT
};

struct Rect {
  ImVec2 a, b;
  bool has(ImVec2 p) const {
    return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y;
  }
};

ImU32 dot_color(int state) {
  if (state == 1) return IM_COL32(0x5c, 0xb8, 0x4c, 0xff);
  if (state == 2) return IM_COL32(0xd4, 0x4a, 0x3c, 0xff);
  return theme::shade(theme::LEAD_INK, 0.42f);
}

void dotted_v(ImDrawList *dl, float x, float y0, float y1, ImU32 c) {
  for (float y = y0; y < y1; y += 3.f)
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 1.f, y + 1.f), c);
}
void dotted_h(ImDrawList *dl, float x0, float x1, float y, ImU32 c) {
  for (float x = x0; x < x1; x += 3.f)
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 1.f, y + 1.f), c);
}

void tick(ImDrawList *dl, ImVec2 c, float s, ImU32 col) {
  dl->AddLine(ImVec2(c.x - s * 0.45f, c.y), ImVec2(c.x - s * 0.1f, c.y + s * 0.35f),
              col, 1.5f);
  dl->AddLine(ImVec2(c.x - s * 0.1f, c.y + s * 0.35f),
              ImVec2(c.x + s * 0.45f, c.y - s * 0.35f), col, 1.5f);
}
void cross(ImDrawList *dl, ImVec2 c, float s, ImU32 col) {
  dl->AddLine(ImVec2(c.x - s * 0.35f, c.y - s * 0.35f),
              ImVec2(c.x + s * 0.35f, c.y + s * 0.35f), col, 1.5f);
  dl->AddLine(ImVec2(c.x - s * 0.35f, c.y + s * 0.35f),
              ImVec2(c.x + s * 0.35f, c.y - s * 0.35f), col, 1.5f);
}

// a tag chip: a small rounded rect with a one-letter glyph
void chip(ImDrawList *dl, const Rect &r, const char *glyph, ImU32 bg,
          bool hot) {
  dl->AddRectFilled(r.a, r.b, hot ? theme::accent() : bg, 2.f);
  ImVec2 ts = ImGui::CalcTextSize(glyph);
  dl->AddText(ImVec2((r.a.x + r.b.x - ts.x) * 0.5f, (r.a.y + r.b.y - ts.y) * 0.5f),
              theme::text_on_header(), glyph);
}

// true when `i` is under any selected object that is not itself
bool under_selection(const SceneState &sc, int i) {
  int guard = (int)sc.objects.size();
  for (int p = sc.objects[i].parent; p >= 0 && guard-- > 0;
       p = sc.objects[p].parent)
    if (tree_is_selected(sc, p)) return true;
  return false;
}

void set_expanded(SceneState &sc, int i, bool on, bool subtree) {
  sc.objects[i].expanded = on;
  if (!subtree) return;
  for (int c = 0; c < (int)sc.objects.size(); ++c)
    if (sc.objects[c].parent == i) set_expanded(sc, c, on, true);
}

} // namespace

bool tree_is_selected(const SceneState &sc, int i) {
  if (i == sc.selected) return true;
  for (int s : sc.selection)
    if (s == i) return true;
  return false;
}

void tree_select(App &a, SceneState &sc, int i, bool ctrl, bool shift) {
  TreeState &g = tree_state();
  if (shift && sc.selected >= 0) {
    int r0 = tree_row_of(sc.selected), r1 = tree_row_of(i);
    if (r0 >= 0 && r1 >= 0) {
      if (r0 > r1) std::swap(r0, r1);
      if (!ctrl) sc.selection.clear();
      for (int r = r0; r <= r1; ++r)
        if (g.rows[r].idx >= 0 && !tree_is_selected(sc, g.rows[r].idx))
          sc.selection.push_back(g.rows[r].idx);
      if (!tree_is_selected(sc, sc.selected)) sc.selection.push_back(sc.selected);
    }
  } else if (ctrl) {
    bool was = false;
    for (size_t k = 0; k < sc.selection.size(); ++k)
      if (sc.selection[k] == i) {
        sc.selection.erase(sc.selection.begin() + (long)k);
        was = true;
        break;
      }
    if (was && i == sc.selected) {
      sc.selected = sc.selection.empty() ? i : sc.selection.back();
      if (sc.selection.empty()) sc.selection = {i};
    } else if (!was) {
      if (!tree_is_selected(sc, sc.selected)) sc.selection.push_back(sc.selected);
      sc.selection.push_back(i);
      sc.selected = i;
    }
  } else {
    sc.selection = {i};
    sc.selected = i;
  }
  a.scene_selection_serial++;
  g.seen_serial = a.scene_selection_serial;
}

void tree_select_subtree(App &a, SceneState &sc, int i) {
  sc.selection = {i};
  sc.selected = i;
  for (int c = 0; c < (int)sc.objects.size(); ++c)
    if (c != i && scene_is_descendant(sc, c, i)) sc.selection.push_back(c);
  a.scene_selection_serial++;
  tree_state().seen_serial = a.scene_selection_serial;
}

void tree_begin_rename(const SceneState &sc, int i) {
  TreeState &g = tree_state();
  if (i < 0 || i >= (int)sc.objects.size()) return;
  g.rename = i;
  g.rename_focus = true;
  snprintf(g.rename_buf, sizeof g.rename_buf, "%s", sc.objects[i].name.c_str());
}

void tree_set_state(SceneState &sc, int i, int kind, int value, bool subtree) {
  if (i < 0 || i >= (int)sc.objects.size()) return;
  SceneObject &o = sc.objects[i];
  if (kind == 0) o.vis_viewport = value;
  else if (kind == 1) o.vis_render = value;
  else if (kind == 2) o.enabled = value != 0;
  else if (kind == 3) o.layer = value;
  if (!subtree) return;
  for (int c = 0; c < (int)sc.objects.size(); ++c)
    if (sc.objects[c].parent == i) tree_set_state(sc, c, kind, value, true);
}

void tree_row_draw(App &a, SceneState &sc, const TreeRow &r, int row_no) {
  TreeState &g = tree_state();
  SceneObject &o = sc.objects[r.idx];
  const float h = tree_row_height(), fs = ImGui::GetFontSize();
  const float ind = tree_indent();
  const ImGuiIO &io = ImGui::GetIO();
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  float w = ImGui::GetContentRegionAvail().x;
  if (w < 40.f) w = 40.f;
  ImVec2 p1(p0.x + w, p0.y + h);
  ImGui::PushID(r.idx);
  ImGui::InvisibleButton("##row", ImVec2(w, h),
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const bool hovered = ImGui::IsItemHovered();
  const bool selected = tree_is_selected(sc, r.idx);
  const bool shown = sc.object_visible(o);
  const bool active_cam =
      o.type == SceneObject::Camera && scene_active_camera() == r.idx;
  const float alpha = shown ? 1.f : 0.45f;

  // ---- geometry: left zone, middle bar, tags ----
  const float mid = p0.y + h * 0.5f;
  float x = p0.x + 4.f + ind * r.depth;
  Rect z_exp{ImVec2(x, p0.y), ImVec2(x + fs, p1.y)};
  x += fs + 2.f;
  Rect z_icon{ImVec2(x, p0.y), ImVec2(x + fs, p1.y)};
  x += fs + 5.f;
  const float name_x = x;

  int chips = 0;
  if (g.show_tags) chips = (o.material_node ? 1 : 0) + (o.driver_node ? 1 : 0) +
                           (o.scatter_node ? 1 : 0);
  const float chip_w = fs + 4.f;
  const float tags_w = chips * (chip_w + 2.f) + (chips ? 4.f : 0.f);
  const float dot_d = fs * 0.5f;
  const float bar_w = (fs * 0.55f + 4.f) + (dot_d + 6.f) + (fs + 2.f) +
                      (o.type == SceneObject::Camera ? fs + 2.f : 0.f) + 6.f;
  float bx = p1.x - tags_w - bar_w;
  const float name_max = bx - 4.f;
  Rect z_sw{ImVec2(bx, mid - fs * 0.3f), ImVec2(bx + fs * 0.55f, mid + fs * 0.3f)};
  bx += fs * 0.55f + 4.f;
  Rect z_vp{ImVec2(bx, mid - dot_d - 0.5f), ImVec2(bx + dot_d, mid - 0.5f)};
  Rect z_rn{ImVec2(bx, mid + 0.5f), ImVec2(bx + dot_d, mid + dot_d + 0.5f)};
  bx += dot_d + 6.f;
  Rect z_tick{ImVec2(bx, p0.y), ImVec2(bx + fs, p1.y)};
  bx += fs + 2.f;
  Rect z_cam{ImVec2(bx, p0.y), ImVec2(bx + fs, p1.y)};
  Rect z_chip[3];
  int chip_zone[3] = {Z_NONE, Z_NONE, Z_NONE};
  const char *chip_glyph[3] = {"", "", ""};
  const char *chip_tip[3] = {"", "", ""};
  {
    float cx = p1.x - tags_w + 4.f;
    int n = 0;
    auto add = [&](int zone, const char *glyph, const char *tip) {
      z_chip[n] = Rect{ImVec2(cx, mid - fs * 0.5f), ImVec2(cx + chip_w, mid + fs * 0.5f)};
      chip_zone[n] = zone;
      chip_glyph[n] = glyph;
      chip_tip[n] = tip;
      cx += chip_w + 2.f;
      ++n;
    };
    if (g.show_tags && o.material_node) add(Z_CHIP_MAT, "M", tr("om.chip_material"));
    if (g.show_tags && o.driver_node) add(Z_CHIP_DRV, "N", tr("om.chip_driver"));
    if (g.show_tags && o.scatter_node) add(Z_CHIP_SCT, "S", tr("om.chip_scatter"));
  }

  auto zone_at = [&](ImVec2 m) -> int {
    if (r.has_children && z_exp.has(m)) return Z_EXPANDER;
    if (z_icon.has(m)) return Z_ICON;
    if (z_sw.has(m)) return Z_SWATCH;
    if (z_vp.has(m)) return Z_DOT_VP;
    if (z_rn.has(m)) return Z_DOT_RN;
    if (z_tick.has(m)) return Z_TICK;
    if (o.type == SceneObject::Camera && z_cam.has(m)) return Z_CAM;
    for (int k = 0; k < 3; ++k)
      if (chip_zone[k] != Z_NONE && z_chip[k].has(m)) return chip_zone[k];
    return Z_NAME;
  };
  const int hz = hovered ? zone_at(io.MousePos) : Z_NONE;

  // ---- background: selected, last-selected, drop feedback ----
  if (selected) {
    dl->AddRectFilled(p0, p1, theme::fade(theme::accent(),
                                          r.idx == sc.selected ? 0.55f : 0.35f));
  } else if (hovered && g.paint_kind < 0) {
    dl->AddRectFilled(p0, p1, theme::fade(theme::LEAD_INK, 0.06f));
  }

  // ---- hierarchy guides ----
  ImU32 gc = theme::fade(theme::text_dim(), 0.55f);
  if (!g.flat && !g.by_layer) {
    for (int d = 0; d + 1 < r.depth; ++d)
      if (r.guides & (uint64_t(1) << d))
        dotted_v(dl, p0.x + 4.f + ind * d + fs * 0.5f, p0.y, p1.y, gc);
    if (r.depth > 0) {
      float gx = p0.x + 4.f + ind * (r.depth - 1) + fs * 0.5f;
      dotted_v(dl, gx, p0.y, r.last ? mid : p1.y, gc);
      dotted_h(dl, gx, z_exp.a.x + (r.has_children ? 0.f : fs * 0.5f), mid, gc);
    }
  }

  // ---- expander, icon, name ----
  if (r.has_children && !g.flat && !g.by_layer)
    icon_draw(dl, o.expanded ? Icon::ChevronDown : Icon::Chevron,
              ImVec2(z_exp.a.x + fs * 0.5f, mid), fs * 0.8f,
              hz == Z_EXPANDER ? theme::accent() : theme::text_dim());
  ImU32 ic = active_cam ? theme::accent() : theme::fade(theme::text_dim(), alpha);
  icon_draw(dl, scene_type_icon(o.type), ImVec2(z_icon.a.x + fs * 0.5f, mid),
            fs * 0.85f, hz == Z_ICON ? theme::accent() : ic);

  if (g.rename != r.idx) {
    ImU32 tc = active_cam ? theme::accent() : theme::text();
    if (!selected && under_selection(sc, r.idx)) tc = theme::text_dim();
    if (r.idx == sc.selected) tc = theme::text_on_header();
    tc = theme::fade(tc, alpha);
    ImVec4 clip(name_x, p0.y, name_max, p1.y);
    dl->AddText(nullptr, 0.f, ImVec2(name_x, mid - fs * 0.5f), tc, o.name.c_str(),
                nullptr, 0.f, &clip);
  }

  // ---- the middle bar ----
  {
    const SceneLayer *L = o.layer >= 0 && o.layer < (int)sc.layers.size()
                              ? &sc.layers[o.layer]
                              : nullptr;
    ImU32 lc = L ? ImGui::ColorConvertFloat4ToU32(
                       ImVec4(L->color[0], L->color[1], L->color[2], 1.f))
                 : theme::text_dim();
    dl->AddRectFilled(z_sw.a, z_sw.b, lc);
    if (hz == Z_SWATCH) dl->AddRect(z_sw.a, z_sw.b, theme::text());
    ImVec2 cv((z_vp.a.x + z_vp.b.x) * 0.5f, (z_vp.a.y + z_vp.b.y) * 0.5f);
    ImVec2 cr((z_rn.a.x + z_rn.b.x) * 0.5f, (z_rn.a.y + z_rn.b.y) * 0.5f);
    dl->AddCircleFilled(cv, dot_d * 0.5f, dot_color(o.vis_viewport), 12);
    dl->AddCircleFilled(cr, dot_d * 0.5f, dot_color(o.vis_render), 12);
    if (hz == Z_DOT_VP) dl->AddCircle(cv, dot_d * 0.5f + 1.f, theme::text(), 12);
    if (hz == Z_DOT_RN) dl->AddCircle(cr, dot_d * 0.5f + 1.f, theme::text(), 12);
    ImVec2 ct((z_tick.a.x + z_tick.b.x) * 0.5f, mid);
    if (o.enabled) tick(dl, ct, fs * 0.8f, hz == Z_TICK ? theme::text() : dot_color(1));
    else cross(dl, ct, fs * 0.8f, hz == Z_TICK ? theme::text() : dot_color(2));
    if (o.type == SceneObject::Camera)
      icon_draw(dl, Icon::Eye, ImVec2((z_cam.a.x + z_cam.b.x) * 0.5f, mid), fs * 0.8f,
                active_cam ? theme::accent()
                           : hz == Z_CAM ? theme::text() : theme::text_dim());
  }

  // ---- tags ----
  for (int k = 0; k < 3; ++k)
    if (chip_zone[k] != Z_NONE)
      chip(dl, z_chip[k], chip_glyph[k], theme::shade(theme::LEAD_SURFACE, 1.6f),
           hz == chip_zone[k]);

  // ---- tooltips ----
  if (hovered && g.paint_kind < 0) {
    switch (hz) {
      case Z_ICON: ImGui::SetTooltip("%s", scene_type_name(o.type)); break;
      case Z_SWATCH: ImGui::SetTooltip("%s", tr("om.swatch_tip")); break;
      case Z_DOT_VP: ImGui::SetTooltip("%s", tr("om.dot_viewport_tip")); break;
      case Z_DOT_RN: ImGui::SetTooltip("%s", tr("om.dot_render_tip")); break;
      case Z_TICK: ImGui::SetTooltip("%s", tr("om.tick_tip")); break;
      case Z_CAM: ImGui::SetTooltip("%s", tr("om.cam_tip")); break;
      default:
        for (int k = 0; k < 3; ++k)
          if (hz == chip_zone[k]) ImGui::SetTooltip("%s", chip_tip[k]);
    }
  }

  // ---- clicks ----
  if (ImGui::IsItemActivated()) g.press_zone = zone_at(io.MousePos);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    int z = zone_at(io.MousePos);
    auto cycle = [&](int kind, int cur) {
      int nv = (cur + 1) % 3;
      undo_push(a, tr("om.undo.visibility"));
      tree_set_state(sc, r.idx, kind, nv, io.KeyCtrl);
      g.paint_kind = kind;
      g.paint_value = nv;
    };
    switch (z) {
      case Z_EXPANDER: set_expanded(sc, r.idx, !o.expanded, io.KeyCtrl); break;
      case Z_SWATCH: ImGui::OpenPopup("##layerpick"); break;
      case Z_DOT_VP: cycle(0, o.vis_viewport); break;
      case Z_DOT_RN: cycle(1, o.vis_render); break;
      case Z_TICK:
        undo_push(a, tr("om.undo.enable"));
        tree_set_state(sc, r.idx, 2, o.enabled ? 0 : 1, io.KeyAlt);
        g.paint_kind = 2;
        g.paint_value = o.enabled ? 1 : 0;
        break;
      case Z_CAM:
        scene_active_camera() = r.idx;
        scene_last_used_camera() = r.idx;
        a.status = std::string(tr("om.status.looking_through")) + " " + o.name;
        break;
      case Z_CHIP_MAT: graph_focus_node(a, o.material_node); break;
      case Z_CHIP_DRV: graph_focus_node(a, o.driver_node); break;
      case Z_CHIP_SCT: graph_focus_node(a, o.scatter_node); break;
      case Z_ICON:
      default:
        tree_select(a, sc, r.idx, io.KeyCtrl, io.KeyShift);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          if (io.KeyAlt && r.has_children) g.root = r.idx;
          else if (o.type == SceneObject::Camera) {
            scene_active_camera() = r.idx;
            scene_last_used_camera() = r.idx;
          } else if (o.type == SceneObject::Planet) {
            scene_active_camera() = -1;
            renderer_camera_look_at(o.pos, o.planet.radius * 3.5f);
          } else {
            tree_begin_rename(sc, r.idx);
          }
        }
    }
  }
  if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) tree_select_subtree(a, sc, r.idx);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected)
    tree_select(a, sc, r.idx, false, false);

  // paint-to-inherit: the brush crosses rows that are not the active item
  if (g.paint_kind >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
      ImGui::IsMouseHoveringRect(ImVec2(z_sw.a.x, p0.y), ImVec2(z_cam.b.x, p1.y)))
    tree_set_state(sc, r.idx, g.paint_kind, g.paint_value, false);

  // layer picker popup
  if (ImGui::BeginPopup("##layerpick")) {
    for (int li = 0; li < (int)sc.layers.size(); ++li) {
      ImGui::PushID(li);
      const SceneLayer &L = sc.layers[li];
      ImVec2 c = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(c.x, c.y + 3.f), ImVec2(c.x + fs * 0.6f, c.y + fs - 2.f),
          ImGui::ColorConvertFloat4ToU32(ImVec4(L.color[0], L.color[1], L.color[2], 1.f)));
      ImGui::Dummy(ImVec2(fs * 0.6f, fs));
      ImGui::SameLine();
      if (ImGui::MenuItem(L.name.c_str(), nullptr, li == o.layer)) {
        undo_push(a, tr("om.undo.layer"));
        o.layer = li;
      }
      ImGui::PopID();
    }
    ImGui::EndPopup();
  }

  // drag and drop from the name; the dots and the tick start a paint instead
  if (g.press_zone == Z_NAME || g.press_zone == Z_ICON || g.press_zone == Z_EXPANDER)
    tree_dnd_source(sc, r.idx);
  tree_dnd_target(sc, r, p0, p1);
  tree_context_menu(a, sc, r.idx);
  // The edit box goes last: every IsItem* query above refers to the row.
  if (g.rename == r.idx) {
    ImGui::SetCursorScreenPos(ImVec2(name_x, p0.y + (h - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::SetNextItemWidth(name_max - name_x);
    if (g.rename_focus) {
      ImGui::SetKeyboardFocusHere();
      g.rename_focus = false;
    }
    bool done = ImGui::InputText("##rn", g.rename_buf, sizeof g.rename_buf,
                                 ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll);
    int move = 0;
    if (ImGui::IsItemActive()) {
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) move = 1;
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) move = -1;
    }
    if (done || move || ImGui::IsItemDeactivated()) {
      if (g.rename_buf[0] && o.name != g.rename_buf) {
        undo_push(a, tr("om.undo.rename"));
        o.name = g.rename_buf;
      }
      g.rename = -1;
      if (move) {
        // arrow keys move row to row while staying in edit mode
        int rr = row_no + move;
        while (rr >= 0 && rr < (int)g.rows.size() && g.rows[rr].idx < 0) rr += move;
        if (rr >= 0 && rr < (int)g.rows.size()) {
          tree_select(a, sc, g.rows[rr].idx, false, false);
          tree_begin_rename(sc, g.rows[rr].idx);
        }
      }
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
  }
  ImGui::PopID();
}

} // namespace studio
