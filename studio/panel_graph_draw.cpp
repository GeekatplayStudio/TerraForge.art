// Geekatplay TerraForge - drawing the graph: nodes, ports, previews, and the
// create menu (grouped by category; filtered by the dragged port during a
// drag-to-create). Split from panel_graph.cpp for the 500-line module rule;
// the editor interaction stays there.
#include "panel_graph_internal.hpp"
#include "console.hpp"
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


// A node is drawn as a coloured header over a dark body, which is how Cinema
// 4D and Cycles 4D draw theirs. The header *is* the category: a graph is then
// readable at a distance, before any label is legible, which is the whole
// reason for colouring nodes at all. Our previous design put a four-pixel tick
// beside the title, which carried the same information and conveyed none of it.
void draw_node(App &a, const App::NodeView &n) {
  const bool selected = a.selected_node == n.id;
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
  ed::PushStyleVar(ed::StyleVar_NodeRounding, 0.f);
  ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, selected ? 2.f : 1.f);
  // no top padding: the header bar has to reach the node's own edges
  ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 6));
  ed::BeginNode(n.id);

  ImU32 hc = theme::category_color(n.category);
  if (!n.enabled) hc = theme::fade(theme::shade(hc, 0.55f), 0.9f);
  ImDrawList *dl = ImGui::GetWindowDrawList();

  // ---- measure ----------------------------------------------------------
  // Inputs on the left and outputs on the right have to share rows, so the
  // node's width must be decided *before* either column is drawn. Laying the
  // inputs out first and then right-aligning the outputs against whatever
  // width happened to result puts the two sets on different rows entirely,
  // with the outputs stranded below the preview — which is not how any node
  // editor is read.
  const float dot_col = nodemetric::PORT_R * 2 + 6.f;
  float in_label_w = 0.f, out_label_w = 0.f;
  size_t in_n = 0, out_n = 0;
  for (const App::PortView &p : n.ports) {
    float tw = ImGui::CalcTextSize(p.name.c_str()).x;
    if (p.is_input) { in_label_w = std::max(in_label_w, tw); ++in_n; }
    else { out_label_w = std::max(out_label_w, tw); ++out_n; }
  }
  float head_w = ImGui::CalcTextSize(n.type.c_str()).x;
  if (!n.enabled) head_w += 6.f + ImGui::CalcTextSize("bypassed").x;
  const unsigned prev_tex = previews_get(n.id);
  const float ports_w = (in_n ? dot_col + in_label_w : 0.f) +
                        (in_n && out_n ? nodemetric::COL_GAP : 0.f) +
                        (out_n ? out_label_w + dot_col : 0.f);
  float body_w = std::max(head_w, ports_w) + nodemetric::PAD_X * 2.f;
  if (prev_tex)
    body_w = std::max(body_w, nodemetric::PREVIEW + nodemetric::PAD_X * 2.f);

  // ---- header ----------------------------------------------------------
  // The bar itself is painted after EndNode, when the node's real rectangle is
  // known; this reserves its height and, with the measured width, fixes the
  // node's own width so the columns below can be placed absolutely.
  const ImVec2 head_pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(body_w, nodemetric::HEADER_H));

  ImGui::SetCursorScreenPos(
      ImVec2(head_pos.x + nodemetric::PAD_X, head_pos.y + 3.f));
  ImGui::PushStyleColor(ImGuiCol_Text, theme::text_on_header());
  ImGui::TextUnformatted(n.type.c_str());
  ImGui::PopStyleColor();
  if (!n.enabled) {
    ImGui::SameLine(0, 6);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::accent());
    ImGui::TextUnformatted("bypassed");
    ImGui::PopStyleColor();
  }

  auto port_dot = [&](ImVec2 c, ImU32 col, bool filled) {
    // filled = required, hollow = optional. The shape says whether you have
    // to connect it, so that is one less thing to learn from a tooltip.
    if (filled) dl->AddCircleFilled(c, nodemetric::PORT_R, col, 12);
    else {
      dl->AddCircleFilled(c, nodemetric::PORT_R, theme::node_bg(), 12);
      dl->AddCircle(c, nodemetric::PORT_R, col, 12, 1.6f);
    }
  };

  // ---- ports: inputs left, outputs right, on shared rows ----------------
  // One row per index, so the first input sits opposite the first output and a
  // wire entering a node lines up with the wire leaving it. Both columns are
  // positioned in screen space against the width measured above.
  const float rows_top = head_pos.y + nodemetric::HEADER_H + 5.f;
  const size_t rows = std::max(in_n, out_n);
  size_t in_row = 0, out_row = 0;
  for (size_t i = 0; i < n.ports.size(); ++i) {
    const App::PortView &p = n.ports[i];
    const size_t row = p.is_input ? in_row++ : out_row++;
    const float y = rows_top + row * nodemetric::ROW_H;
    const float tw = ImGui::CalcTextSize(p.name.c_str()).x;

    if (p.is_input) {
      ImGui::SetCursorScreenPos(ImVec2(head_pos.x + nodemetric::PAD_X, y));
      ed::BeginPin(pin_id(n.id, i), ed::PinKind::Input);
      ImVec2 dot(head_pos.x + nodemetric::PAD_X + nodemetric::PORT_R,
                 y + nodemetric::ROW_H * 0.5f);
      port_dot(dot, theme::port_color(p.is_texture, p.is_field, p.field_type),
               !p.optional);
      ImGui::Dummy(ImVec2(dot_col, nodemetric::ROW_H));
      ImGui::SameLine(0, 0);
      ImGui::PushStyleColor(ImGuiCol_Text,
                            p.optional ? theme::text_dim() : theme::text());
      ImGui::TextUnformatted(p.name.c_str());
      ImGui::PopStyleColor();
      ed::PinRect(ImVec2(dot.x - 7, dot.y - 7), ImVec2(dot.x + 7, dot.y + 7));
      ed::EndPin();
    } else {
      // right-aligned: the label ends where the dot column begins, so every
      // output dot in the node sits on one vertical line
      const float x = head_pos.x + body_w - nodemetric::PAD_X - dot_col - tw;
      ImGui::SetCursorScreenPos(ImVec2(x, y));
      ed::BeginPin(pin_id(n.id, i), ed::PinKind::Output);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::text());
      ImGui::TextUnformatted(p.name.c_str());
      ImGui::PopStyleColor();
      ImVec2 dot(head_pos.x + body_w - nodemetric::PAD_X - nodemetric::PORT_R,
                 y + nodemetric::ROW_H * 0.5f);
      port_dot(dot, theme::port_color(p.is_texture, p.is_field, p.field_type),
               true);
      ImGui::SameLine(0, 0);
      ImGui::Dummy(ImVec2(dot_col, nodemetric::ROW_H));
      ed::PinRect(ImVec2(dot.x - 7, dot.y - 7), ImVec2(dot.x + 7, dot.y + 7));
      ed::EndPin();
    }
  }
  // Claim the full block the rows occupy, so the node's height follows them
  // and the preview lands underneath rather than on top.
  ImGui::SetCursorScreenPos(ImVec2(head_pos.x, rows_top));
  ImGui::Dummy(ImVec2(body_w, rows * nodemetric::ROW_H + 3.f));

  if (prev_tex) {
    ImGui::SetCursorScreenPos(
        ImVec2(head_pos.x + (body_w - nodemetric::PREVIEW) * 0.5f,
               ImGui::GetCursorScreenPos().y));
    ImGui::Image((ImTextureID)(intptr_t)prev_tex,
                 ImVec2(nodemetric::PREVIEW, nodemetric::PREVIEW));
  }

  // node-local indent: SetCursorPosX is window-relative, which throws these
  // outside any node that is not at the window's left edge
  auto indent = [] {
    ImGui::Dummy(ImVec2(nodemetric::PAD_X, 0));
    ImGui::SameLine(0, 0);
  };
  if (n.ms > 0.01) {
    indent();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim());
    ImGui::Text("%.1f ms", n.ms);
    ImGui::PopStyleColor();
  }
  if (!n.error.empty()) {
    indent();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::error());
    ImGui::TextUnformatted(n.error.c_str());
    ImGui::PopStyleColor();
  }
  ed::EndNode();
  ed::PopStyleVar(3);
  ed::PopStyleColor(2);

  // The header can only be painted once the node's true width is known, and it
  // has to land *behind* the title that was already written into that space.
  // The editor keeps a per-node background list for exactly this; drawing into
  // the ordinary list here would paint the bar over its own text.
  ImVec2 tl = ed::GetNodePosition(n.id);
  ImVec2 sz = ed::GetNodeSize(n.id);
  if (sz.x > 1.f) {
    ImDrawList *bg = ed::GetNodeBackgroundDrawList(n.id);
    bg->AddRectFilled(tl, ImVec2(tl.x + sz.x, tl.y + nodemetric::HEADER_H), hc);
    // a hairline under the header separates it from the body without a border
    bg->AddLine(ImVec2(tl.x, tl.y + nodemetric::HEADER_H),
                ImVec2(tl.x + sz.x, tl.y + nodemetric::HEADER_H),
                theme::shade(hc, 0.6f), 1.f);
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
  ImGui::InputTextWithHint("##filter", "search nodes...", filter, sizeof filter);
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
    return a.graph_show_all_domains ||
           domain_of_category(d->category) == a.workspace;
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
                           ? "heightmap"
                           : g_drag_create.type == gpx::DataType::Texture
                                 ? "texture"
                                 : "field";
    if (filtering)
      ImGui::TextDisabled("nodes that accept a %s", what);
    else
      ImGui::TextDisabled("nothing accepts a %s - showing everything", what);
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
        ImGui::TextDisabled("...more, keep typing");
        break;
      }
      entry(d);
      ImGui::SameLine();
      ImGui::TextDisabled("%s", d->category.c_str());
    }
    if (!shown) ImGui::TextDisabled("nothing matches");
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
