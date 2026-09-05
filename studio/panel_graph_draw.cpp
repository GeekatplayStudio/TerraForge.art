// Geekatplay TerraForge - drawing the graph: nodes, ports, previews, and the
// create menu (grouped by category; filtered by the dragged port during a
// drag-to-create). Split from panel_graph.cpp for the 500-line module rule;
// the editor interaction stays there.
#include "panel_graph_internal.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "theme_colors.hpp"
#include "undo.hpp"
#include "gpx/port_catalog.hpp"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace ed = ax::NodeEditor;

namespace studio {


// A node is a card: a rounded dark body under a coloured title bar, with the
// connectors sitting on its left and right edges so a wire visibly *meets*
// the node rather than disappearing into it. The bar is the category — a
// graph is readable at a distance, before any label is legible — and the
// connector's colour is the data type, so what a wire carries is visible at
// both ends and along its length. Three levels of detail: expanded (ports,
// preview, timing), compact (ports only) and header only, where every wire
// converges on one connector per side.
void draw_node(App &a, const App::NodeView &n) {
  using namespace nodemetric;
  const bool selected = a.selected_node == n.id;
  const int collapse = std::clamp(n.collapse, 0, 2);
  // a bypassed node stays visible but reads as inert, so you can see the
  // structure you are keeping without mistaking it for part of the result
  ed::PushStyleColor(ed::StyleColor_NodeBg,
                     ImGui::ColorConvertU32ToFloat4(
                         n.enabled ? theme::node_bg()
                                   : theme::fade(theme::node_bg(), 0.72f)));
  ed::PushStyleColor(
      ed::StyleColor_NodeBorder,
      ImGui::ColorConvertU32ToFloat4(!n.error.empty() ? theme::error()
                                     : selected      ? theme::accent()
                                                     : theme::node_border()));
  ed::PushStyleVar(ed::StyleVar_NodeRounding, ROUNDING);
  ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, selected ? 2.f : 1.f);
  // no top padding: the header bar has to reach the node's own edges
  ed::PushStyleVar(ed::StyleVar_NodePadding,
                   ImVec4(0, 0, 0, collapse == 2 ? 0.f : 7.f));
  ed::BeginNode(n.id);

  ImU32 hc = theme::category_color(n.category);
  if (!n.enabled) hc = theme::fade(theme::shade(hc, 0.55f), 0.9f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 body_col = n.enabled ? theme::node_bg()
                                   : theme::fade(theme::node_bg(), 0.72f);

  // ---- measure ----------------------------------------------------------
  // Inputs on the left and outputs on the right share rows, so the width is
  // decided before either column is drawn.
  const float dot_col = PORT_R + 4.f; // room between the edge and the label
  float in_label_w = 0.f, out_label_w = 0.f;
  size_t in_n = 0, out_n = 0;
  for (const App::PortView &p : n.ports) {
    float tw = ImGui::CalcTextSize(p.name.c_str()).x;
    if (p.is_input) { in_label_w = std::max(in_label_w, tw); ++in_n; }
    else { out_label_w = std::max(out_label_w, tw); ++out_n; }
  }
  float head_w = ImGui::CalcTextSize(n.type.c_str()).x + CHEVRON_W + 4.f;
  if (!n.enabled) head_w += 6.f + ImGui::CalcTextSize(tr("bypassed")).x;
  const unsigned prev_tex = collapse == 0 ? previews_get(n.id) : 0;
  const float ports_w = (in_n ? dot_col + in_label_w : 0.f) +
                        (in_n && out_n ? COL_GAP : 0.f) +
                        (out_n ? out_label_w + dot_col : 0.f);
  float body_w = std::max(head_w, collapse == 2 ? 0.f : ports_w) + PAD_X * 2.f;
  if (prev_tex) body_w = std::max(body_w, PREVIEW + PAD_X * 2.f);
  body_w = std::max(body_w, 120.f);

  // ---- header ----------------------------------------------------------
  const ImVec2 head_pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(body_w, HEADER_H));

  ImGui::SetCursorScreenPos(
      ImVec2(head_pos.x + PAD_X, head_pos.y + (HEADER_H - ImGui::GetFontSize()) * 0.5f));
  ImGui::PushStyleColor(ImGuiCol_Text, theme::text_on_header());
  ImGui::TextUnformatted(n.type.c_str());
  ImGui::PopStyleColor();
  if (!n.enabled) {
    ImGui::SameLine(0, 6);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::accent());
    ImGui::TextUnformatted(tr("bypassed"));
    ImGui::PopStyleColor();
  }
  // the collapse toggle: a chevron at the header's right, cycling the three
  // levels; H does the same for the whole selection
  {
    ImVec2 c0(head_pos.x + body_w - CHEVRON_W - 2.f, head_pos.y + 2.f);
    ImGui::SetCursorScreenPos(c0);
    ImGui::PushID((int)(n.id & 0x7fffffff));
    if (ImGui::InvisibleButton("##collapse", ImVec2(CHEVRON_W, HEADER_H - 4.f)))
      g_collapse_requests.push_back({n.id, -1});
    ImGui::PopID();
    const bool hot = ImGui::IsItemHovered();
    if (hot)
      ImGui::SetTooltip("%s", collapse == 0 ? tr("Compact: hide the preview")
                              : collapse == 1 ? tr("Collapse to the title bar")
                                              : tr("Expand"));
    ImVec2 cc(c0.x + CHEVRON_W * 0.5f, head_pos.y + HEADER_H * 0.5f);
    ImU32 ccol = hot ? IM_COL32(255, 255, 255, 255)
                     : theme::fade(theme::text_on_header(), 0.75f);
    const float s = 3.5f;
    if (collapse == 2) // pointing right: closed
      dl->AddTriangleFilled(ImVec2(cc.x - s * 0.6f, cc.y - s),
                            ImVec2(cc.x + s * 0.8f, cc.y),
                            ImVec2(cc.x - s * 0.6f, cc.y + s), ccol);
    else if (collapse == 1) // a short bar: half way
      dl->AddRectFilled(ImVec2(cc.x - s, cc.y - 1.f), ImVec2(cc.x + s, cc.y + 1.f),
                        ccol);
    else // pointing down: open
      dl->AddTriangleFilled(ImVec2(cc.x - s, cc.y - s * 0.6f),
                            ImVec2(cc.x + s, cc.y - s * 0.6f),
                            ImVec2(cc.x, cc.y + s * 0.8f), ccol);
  }

  // The connector: a filled disc on the edge with a dark ring, so it reads
  // against both the body and the canvas. Required ports are solid, optional
  // ones hollow — which is one less thing to learn from a tooltip.
  auto port_dot = [&](ImVec2 c, ImU32 col, bool filled) {
    dl->AddCircleFilled(c, PORT_R + 1.5f, theme::shade(body_col, 0.6f), 16);
    if (filled) dl->AddCircleFilled(c, PORT_R, col, 16);
    else {
      dl->AddCircleFilled(c, PORT_R, body_col, 16);
      dl->AddCircle(c, PORT_R - 0.5f, col, 16, 1.8f);
    }
  };
  auto pin_rect = [&](ImVec2 c) {
    ed::PinRect(ImVec2(c.x - 9, c.y - 9), ImVec2(c.x + 9, c.y + 9));
  };

  // ---- ports -------------------------------------------------------------
  if (collapse == 2) {
    // header only: every input meets the card at one point on the left edge,
    // every output leaves from one point on the right
    const ImVec2 lc(head_pos.x, head_pos.y + HEADER_H * 0.5f);
    const ImVec2 rc(head_pos.x + body_w, head_pos.y + HEADER_H * 0.5f);
    bool drew_in = false, drew_out = false;
    for (size_t i = 0; i < n.ports.size(); ++i) {
      const App::PortView &p = n.ports[i];
      ImVec2 c = p.is_input ? lc : rc;
      ImGui::SetCursorScreenPos(ImVec2(c.x - 1, c.y - 1));
      ed::BeginPin(pin_id(n.id, i), p.is_input ? ed::PinKind::Input
                                                : ed::PinKind::Output);
      ImGui::Dummy(ImVec2(2, 2));
      pin_rect(c);
      ed::EndPin();
      bool &drew = p.is_input ? drew_in : drew_out;
      if (!drew) {
        port_dot(c, theme::fade(theme::text(), 0.9f), true);
        drew = true;
      }
    }
    ImGui::SetCursorScreenPos(ImVec2(head_pos.x, head_pos.y + HEADER_H));
    ImGui::Dummy(ImVec2(body_w, 1.f));
  } else {
    // one row per index, so the first input sits opposite the first output
    // and a wire entering lines up with the wire leaving
    const float rows_top = head_pos.y + HEADER_H + 6.f;
    const size_t rows = std::max(in_n, out_n);
    size_t in_row = 0, out_row = 0;
    for (size_t i = 0; i < n.ports.size(); ++i) {
      const App::PortView &p = n.ports[i];
      const size_t row = p.is_input ? in_row++ : out_row++;
      const float y = rows_top + row * ROW_H;
      const float tw = ImGui::CalcTextSize(p.name.c_str()).x;
      const ImU32 pc = theme::port_color(p.is_texture, p.is_field, p.field_type,
                                         p.is_points);
      if (p.is_input) {
        ImVec2 dot(head_pos.x, y + ROW_H * 0.5f); // on the left edge
        ImGui::SetCursorScreenPos(ImVec2(head_pos.x + PAD_X, y));
        ed::BeginPin(pin_id(n.id, i), ed::PinKind::Input);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              p.optional ? theme::text_dim() : theme::text());
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopStyleColor();
        pin_rect(dot);
        ed::EndPin();
        port_dot(dot, pc, !p.optional);
      } else {
        ImVec2 dot(head_pos.x + body_w, y + ROW_H * 0.5f); // on the right edge
        ImGui::SetCursorScreenPos(ImVec2(head_pos.x + body_w - PAD_X - tw, y));
        ed::BeginPin(pin_id(n.id, i), ed::PinKind::Output);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::text());
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopStyleColor();
        pin_rect(dot);
        ed::EndPin();
        port_dot(dot, pc, true);
        // what the connector carries, in the gap before the label when the
        // card is wide enough; always in the tooltip
        if (!p.value.empty() && collapse == 0) {
          ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.8f);
          float vw = ImGui::CalcTextSize(p.value.c_str()).x;
          float x0 = head_pos.x + body_w - PAD_X - tw - 8.f - vw;
          float x_min = head_pos.x + PAD_X + (in_n ? dot_col + in_label_w + 8.f : 0.f);
          if (x0 > x_min)
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                        ImVec2(x0, y + (ROW_H - ImGui::GetFontSize()) * 0.5f),
                        theme::fade(theme::text_dim(), 0.9f), p.value.c_str());
          ImGui::PopFont();
          if (ImGui::IsMouseHoveringRect(ImVec2(dot.x - 9, dot.y - 9),
                                         ImVec2(dot.x + 9, dot.y + 9)))
            ImGui::SetTooltip("%s: %s", p.name.c_str(), p.value.c_str());
        }
      }
    }
    // claim the block the rows occupy so the preview lands underneath
    ImGui::SetCursorScreenPos(ImVec2(head_pos.x, rows_top));
    ImGui::Dummy(ImVec2(body_w, rows * ROW_H + 2.f));

    if (prev_tex) {
      ImVec2 pp(head_pos.x + (body_w - PREVIEW) * 0.5f,
                ImGui::GetCursorScreenPos().y + 2.f);
      ImGui::SetCursorScreenPos(pp);
      ImGui::Image((ImTextureID)(intptr_t)prev_tex, ImVec2(PREVIEW, PREVIEW));
      dl->AddRect(pp, ImVec2(pp.x + PREVIEW, pp.y + PREVIEW),
                  theme::shade(body_col, 0.5f), 3.f);
    }
    // node-local indent: SetCursorPosX is window-relative, which throws these
    // outside any node that is not at the window's left edge
    auto indent = [] {
      ImGui::Dummy(ImVec2(PAD_X, 0));
      ImGui::SameLine(0, 0);
    };
    if (collapse == 0 && n.ms > 0.01) {
      indent();
      ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim());
      ImGui::Text("%.1f ms", n.ms);
      ImGui::PopStyleColor();
    }
    if (!n.error.empty()) {
      indent();
      ImGui::PushStyleColor(ImGuiCol_Text, theme::error());
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + body_w - PAD_X * 2.f);
      ImGui::TextUnformatted(n.error.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
    }
  }
  ed::EndNode();
  ed::PopStyleVar(3);
  ed::PopStyleColor(2);

  // The header can only be painted once the node's true width is known, and
  // it has to land *behind* the title already written into that space: the
  // editor keeps a per-node background list for exactly this.
  ImVec2 tl = ed::GetNodePosition(n.id);
  ImVec2 sz = ed::GetNodeSize(n.id);
  if (sz.x > 1.f) {
    ImDrawList *bg = ed::GetNodeBackgroundDrawList(n.id);
    const bool whole = collapse == 2 || sz.y <= HEADER_H + 2.f;
    bg->AddRectFilled(tl, ImVec2(tl.x + sz.x, tl.y + HEADER_H), hc, ROUNDING,
                      whole ? ImDrawFlags_RoundCornersAll
                            : ImDrawFlags_RoundCornersTop);
    if (!whole)
      bg->AddLine(ImVec2(tl.x, tl.y + HEADER_H), ImVec2(tl.x + sz.x, tl.y + HEADER_H),
                  theme::shade(hc, 0.55f), 1.f);
  }
}

// The node list is long enough that a flat menu is unusable: a hundred and
// eighteen types in one column. Categories become submenus that open on hover,
// which is how every DCC does this, and the search box collapses the whole
// thing to a flat filtered list the moment you type - because when you are
// searching, the grouping is in the way.
// What a drag that ended on empty canvas was carrying. Set inside BeginCreate,
// consumed by the create menu on the next frame, cleared when the menu closes.
// Zero means the menu was opened the ordinary way and filters nothing.

void add_node_popup(App &a) {
  static char filter[64] = "";
  if (ImGui::IsWindowAppearing()) {
    filter[0] = 0;
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(240);
  ImGui::InputTextWithHint("##filter", tr("search nodes..."), filter, sizeof filter);
  const ImVec2 click_pos = ImGui::GetMousePosOnOpeningCurrentPopup();

  auto place = [&](const gpx::NodeDef *d) {
    undo_push_locked(a, "Add " + d->type);
    ImVec2 cp = ed::ScreenToCanvas(click_pos);
    gpx::Node *n = a.graph.add_node(d->type, cp.x, cp.y);
    if (!n) return;
    ed::SetNodePosition(n->id, cp);
    a.selected_node = n->id;
    // A node created by dragging a wire off a port is wired up on the spot.
    // Which port is decided against the live node (gpx::select_port), so
    // "first declared" means the order the node actually declares, and the
    // link is made from the OUT side in both drag directions - so the
    // direction check in add_link is unreachable rather than merely guarded.
    if (g_drag_create.active()) {
      gpx::Node *other = a.graph.find_node(g_drag_create.node);
      std::string np = gpx::select_port(*n, g_drag_create.type,
                                        g_drag_create.want_dir,
                                        g_drag_create.field_type);
      if (other && !np.empty()) {
        bool ok = g_drag_create.want_dir == gpx::PortDir::In
                      ? a.graph.add_link(other->id, g_drag_create.port, n->id, np)
                      : a.graph.add_link(n->id, np, other->id, g_drag_create.port);
        if (!ok)
          log_warn("graph", "created " + d->type +
                                " but could not connect it automatically");
      }
      g_drag_create.clear();
    }
    a.request_eval();
  };
  auto entry = [&](const gpx::NodeDef *d) {
    if (ImGui::MenuItem(d->type.c_str())) place(d);
    if (ImGui::IsItemHovered() && !d->description.empty())
      ImGui::SetTooltip("%s", d->description.c_str());
  };
  auto in_scope = [&](const gpx::NodeDef *d) {
    return g_popup_all || domain_of_category(d->category) == g_popup_domain;
  };

  const auto all = gpx::NodeRegistry::instance().all();

  // During a drag, offer only node types that could actually take the wire.
  // If nothing can, show everything instead: a gesture that opens an empty
  // menu reads as broken, and a permissive menu still lets the node be
  // placed - it just will not connect itself.
  bool filtering = g_drag_create.active();
  if (filtering) {
    bool any = false;
    for (const gpx::NodeDef *d : all)
      if (in_scope(d) &&
          gpx::node_offers(d->type, g_drag_create.type, g_drag_create.want_dir))
        any = true;
    filtering = any;
  }
  auto offerable = [&](const gpx::NodeDef *d) {
    return !filtering ||
           gpx::node_offers(d->type, g_drag_create.type, g_drag_create.want_dir);
  };
  if (g_drag_create.active()) {
    const char *what = g_drag_create.type == gpx::DataType::Heightmap
                           ? tr("heightmap")
                           : g_drag_create.type == gpx::DataType::Texture
                                 ? tr("texture")
                                 : g_drag_create.type == gpx::DataType::Points
                                       ? tr("points")
                                       : tr("field");
    if (filtering)
      ImGui::TextDisabled(tr("nodes that accept a %s"), what);
    else
      ImGui::TextDisabled(tr("nothing accepts a %s - showing everything"), what);
  }

  if (filter[0]) {
    std::string lf = filter;
    for (auto &ch : lf) ch = (char)tolower(ch);
    int shown = 0;
    for (const gpx::NodeDef *d : all) {
      if (!in_scope(d) || !offerable(d)) continue;
      std::string lt = d->type, lc = d->category;
      for (auto &ch : lt) ch = (char)tolower(ch);
      for (auto &ch : lc) ch = (char)tolower(ch);
      // match the category too, so "erosion" finds the whole family
      if (lt.find(lf) == std::string::npos && lc.find(lf) == std::string::npos)
        continue;
      if (++shown > 40) {
        ImGui::TextDisabled("%s", tr("...more, keep typing"));
        break;
      }
      entry(d);
      ImGui::SameLine();
      ImGui::TextDisabled("%s", d->category.c_str());
    }
    if (!shown) ImGui::TextDisabled("%s", tr("nothing matches"));
    return;
  }

  ImGui::Separator();
  std::string last_cat;
  bool open = false;
  for (const gpx::NodeDef *d : all) {
    if (!in_scope(d) || !offerable(d)) continue;
    if (d->category != last_cat) {
      if (open) ImGui::EndMenu();
      last_cat = d->category;
      open = ImGui::BeginMenu(last_cat.c_str());
    }
    if (open) entry(d);
  }
  if (open) ImGui::EndMenu();
}

} // namespace studio
