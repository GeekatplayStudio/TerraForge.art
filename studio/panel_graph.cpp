// Geekatplay Studio — node graph panel (imgui-node-editor)
#include "app.hpp"
#include "theme_colors.hpp"
#include "undo.hpp"
#include "gpx/metanode.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ed = ax::NodeEditor;

namespace studio {

// Show a node in the node editor from anywhere else in the application:
// switch to the workspace that holds its category, select it and pan to it.
// The panning itself happens in draw_panel_graph, which is the only place
// with a live node-editor context.
void graph_focus_node(App &a, uint64_t node) {
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (lk.owns_lock()) {
    gpx::Node *n = a.graph.find_node(node);
    if (!n) return;
    a.workspace = domain_of_category(n->category);
  }
  a.selected_node = node;
  a.focus_node = node;
  a.prop_tab = TAB_NODE;
}

static ed::EditorContext *ED = nullptr;

// pin id encoding: node_id * 4096 + port index + 1
static uint64_t pin_id(uint64_t node, size_t port) { return node * 4096 + port + 1; }
static void decode_pin(uint64_t pin, uint64_t &node, size_t &port) {
  node = pin / 4096;
  port = (size_t)(pin % 4096) - 1;
}

// Node metrics. Cinema 4D publishes almost no numbers — palette icon sizes are
// the only pixel values in the whole manual — so these come from measuring the
// reference screenshots rather than from documentation.
namespace nodemetric {
constexpr float HEADER_H = 20.f;   // the coloured title bar
constexpr float PORT_R = 4.5f;     // port dot radius
constexpr float ROW_H = 16.f;      // one port row
constexpr float PREVIEW = 96.f;    // thumbnail edge
constexpr float PAD_X = 9.f;
constexpr float COL_GAP = 18.f;    // clear space between the two port columns
} // namespace nodemetric

// A node is drawn as a coloured header over a dark body, which is how Cinema
// 4D and Cycles 4D draw theirs. The header *is* the category: a graph is then
// readable at a distance, before any label is legible, which is the whole
// reason for colouring nodes at all. Our previous design put a four-pixel tick
// beside the title, which carried the same information and conveyed none of it.
static void draw_node(App &a, const App::NodeView &n) {
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
static void add_node_popup(App &a) {
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
    if (n) {
      ed::SetNodePosition(n->id, cp);
      a.selected_node = n->id;
      a.request_eval();
    }
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

  const auto &all = gpx::NodeRegistry::instance().all();

  if (filter[0]) {
    std::string lf = filter;
    for (auto &ch : lf) ch = (char)tolower(ch);
    int shown = 0;
    for (const gpx::NodeDef *d : all) {
      if (!in_scope(d)) continue;
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
    if (!in_scope(d)) continue;
    if (d->category != last_cat) {
      if (open) ImGui::EndMenu();
      last_cat = d->category;
      open = ImGui::BeginMenu(last_cat.c_str());
    }
    if (open) entry(d);
  }
  if (open) ImGui::EndMenu();
}

void draw_panel_graph(App &a) {
  if (!ED) {
    ed::Config cfg;
    cfg.SettingsFile = "geekatplay_graph_view.json";
    // A view file with absurd values hangs the editor for ever; never hand it
    // one without looking (see graph_view_is_sane).
    discard_insane_graph_view(cfg.SettingsFile);
    ED = ed::CreateEditor(&cfg);
  }
  if (!ImGui::Begin("Graph")) {
    ImGui::End();
    return;
  }
  // The graph is always drawn from App::node_views, so it never blinks out
  // while evaluation holds the lock. Editing needs the real graph, so those
  // paths are simply skipped for the frames where the lock is busy.
  std::unique_lock<std::mutex> graph_lock(a.graph_mtx, std::try_to_lock);
  bool can_edit = graph_lock.owns_lock();
  {
    const char *ws_names[4] = {"Terrain", "Materials", "Atmosphere", "Render"};
    ImGui::TextDisabled("%s nodes", ws_names[std::clamp(a.workspace, 0, 3)]);
    ImGui::SameLine();
    studio::Checkbox("show all domains", &a.graph_show_all_domains);
    if (a.eval.running.load()) {
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
      ImGui::Text("computing %d/%d", a.eval.progress_done.load(),
                  a.eval.progress_total.load());
      ImGui::PopStyleColor();
    }
  }
  ed::SetCurrentEditor(ED);
  ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(0.075f, 0.075f, 0.08f, 1.f));
  ed::PushStyleColor(ed::StyleColor_Grid, ImVec4(1.f, 1.f, 1.f, 0.025f));
  ed::Begin("GeekatplayGraph");

  bool eval_running = !can_edit;

  auto view_visible = [&](const App::NodeView &n) {
    return a.graph_show_all_domains || domain_of_category(n.category) == a.workspace;
  };
  static uint64_t layout_serial_seen = 0;
  bool push_positions = layout_serial_seen != a.graph_layout_serial;
  // Which nodes the editor actually laid out this frame. Asking it for the
  // position of a node it has never drawn returns (0,0), and writing that back
  // destroys the position the node was created with — which is why a graph
  // built by script or by a preset arrived in a single heap at the origin.
  static std::vector<uint64_t> drawn_this_frame;
  {
    if (push_positions)
      for (const auto &n : a.node_views)
        ed::SetNodePosition(n.id, ImVec2(n.pos_x, n.pos_y));
    drawn_this_frame.clear();
    for (const auto &n : a.node_views)
      if (view_visible(n)) {
        draw_node(a, n);
        drawn_this_frame.push_back(n.id);
      }
    auto find_view = [&](uint64_t id) -> const App::NodeView * {
      for (const auto &n : a.node_views)
        if (n.id == id) return &n;
      return nullptr;
    };
    for (const App::LinkView &l : a.link_views) {
      const App::NodeView *fn = find_view(l.from_node);
      const App::NodeView *tn = find_view(l.to_node);
      if (!fn || !tn) continue;
      if (!view_visible(*fn) || !view_visible(*tn)) continue;
      // Direction-aware, and this is not optional: a node may name an input and
      // an output identically. TerrainOutput has `heightmap` both ways
      // (engine/nodes/nodes_export.cpp:18,22), and matching on the name alone
      // took the last hit - the output - so every wire feeding TerrainOutput
      // was drawn arriving at its output pin. AGENTS.md engine rule 3.
      size_t fi = SIZE_MAX, ti = SIZE_MAX;
      for (size_t i = 0; i < fn->ports.size(); ++i)
        if (!fn->ports[i].is_input && fn->ports[i].name == l.from_port) {
          fi = i;
          break;
        }
      for (size_t i = 0; i < tn->ports.size(); ++i)
        if (tn->ports[i].is_input && tn->ports[i].name == l.to_port) {
          ti = i;
          break;
        }
      // A link naming a port that does not exist is a corrupt file, not a
      // link to draw at pin 0.
      if (fi == SIZE_MAX || ti == SIZE_MAX) continue;
      ed::Link(l.id, pin_id(l.from_node, fi), pin_id(l.to_node, ti),
               ImVec4(0.78f, 0.47f, 0.19f, 0.9f), 1.5f);
    }
  }

  // link creation
  if (!eval_running && ed::BeginCreate(ImVec4(0.78f, 0.47f, 0.19f, 1.f), 2.f)) {
    ed::PinId a_pin, b_pin;
    if (ed::QueryNewLink(&a_pin, &b_pin) && a_pin && b_pin) {
      uint64_t na, nb;
      size_t pa, pb;
      decode_pin((uint64_t)a_pin.Get(), na, pa);
      decode_pin((uint64_t)b_pin.Get(), nb, pb);
      gpx::Node *node_a = a.graph.find_node(na);
      gpx::Node *node_b = a.graph.find_node(nb);
      if (node_a && node_b && pa < node_a->ports.size() && pb < node_b->ports.size()) {
        gpx::Port &port_a = node_a->ports[pa];
        gpx::Port &port_b = node_b->ports[pb];
        // normalize direction: out -> in
        gpx::Node *fn = node_a; gpx::Node *tn = node_b;
        gpx::Port *fp = &port_a; gpx::Port *tp = &port_b;
        if (fp->dir == gpx::PortDir::In) {
          std::swap(fn, tn);
          std::swap(fp, tp);
        }
        if (fp->dir != gpx::PortDir::Out || tp->dir != gpx::PortDir::In ||
            fp->type != tp->type || fn == tn) {
          ed::RejectNewItem(ImVec4(0.8f, 0.2f, 0.15f, 1.f), 2.f);
        } else if (ed::AcceptNewItem()) {
          undo_push_locked(a, "Connect nodes");
          if (a.graph.add_link(fn->id, fp->name, tn->id, tp->name))
            a.request_eval();
        }
      }
    }
  }
  ed::EndCreate();

  // deletion
  if (!eval_running && ed::BeginDelete()) {
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid)) {
      if (ed::AcceptDeletedItem()) {
        undo_push_locked(a, "Delete link");
        a.graph.remove_link((uint64_t)lid.Get());
        a.request_eval();
      }
    }
    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid)) {
      if (ed::AcceptDeletedItem()) {
        undo_push_locked(a, "Delete node");
        a.graph.remove_node((uint64_t)nid.Get());
        if (a.selected_node == (uint64_t)nid.Get()) a.selected_node = 0;
        a.request_eval();
      }
    }
  }
  ed::EndDelete();

  // selection sync + store positions
  {
    // Only nodes the editor drew this frame have a position worth trusting,
    // and never on the frame we just pushed positions into it.
    if (!push_positions)
      for (auto &n : a.graph.nodes) {
        bool drawn = false;
        for (uint64_t id : drawn_this_frame)
          if (id == n->id) { drawn = true; break; }
        if (!drawn) continue;
        ImVec2 p = ed::GetNodePosition(n->id);
        n->pos_x = p.x;
        n->pos_y = p.y;
      }
    ed::NodeId sel[8];
    int count = ed::GetSelectedNodes(sel, 8);
    if (count > 0) {
      uint64_t picked = (uint64_t)sel[0].Get();
      a.selected_node = picked;
    }
  }

  // group the selection into a MetaNode (Ctrl+G) / expand one (Ctrl+Shift+G)
  if (!eval_running && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
      ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
    ed::NodeId sel[64];
    int count = ed::GetSelectedNodes(sel, 64);
    std::string err;
    if (ImGui::GetIO().KeyShift) {
      // expand: only meaningful on a MetaNode
      for (int i = 0; i < count; ++i) {
        gpx::Node *n = a.graph.find_node((uint64_t)sel[i].Get());
        if (!n || n->type != "MetaNode") continue;
        undo_push_locked(a, "Expand MetaNode");
        std::vector<uint64_t> back = gpx::metanode_ungroup(a.graph, n->id, err);
        a.status = back.empty() ? "expand failed: " + err
                                : "expanded into " + std::to_string(back.size()) +
                                      " nodes";
        a.graph_layout_serial++;
        a.request_eval();
        break;
      }
    } else if (count > 0) {
      std::vector<uint64_t> ids;
      for (int i = 0; i < count; ++i) ids.push_back((uint64_t)sel[i].Get());
      undo_push_locked(a, "Group into MetaNode");
      gpx::Node *meta = gpx::metanode_group(a.graph, ids, err);
      if (meta) {
        a.selected_node = meta->id;
        a.status = "grouped " + std::to_string(ids.size()) + " nodes";
      } else {
        a.status = "group failed: " + err;
      }
      a.graph_layout_serial++;
      a.request_eval();
    }
  }

  // bypass the selection (Ctrl+E) — the shortcut every node app has for
  // "take this out of the chain and show me what changes"
  if (!eval_running && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
      ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
    ed::NodeId sel[32];
    int count = ed::GetSelectedNodes(sel, 32);
    if (count > 0) {
      undo_push_locked(a, count > 1 ? "Bypass nodes" : "Bypass node");
      // flip them all to match the first, so a mixed selection becomes uniform
      gpx::Node *first = a.graph.find_node((uint64_t)sel[0].Get());
      bool target = first ? !first->enabled : false;
      for (int i = 0; i < count; ++i)
        if (gpx::Node *n = a.graph.find_node((uint64_t)sel[i].Get())) {
          n->enabled = target;
          a.graph.mark_dirty(n->id);
        }
      a.request_eval();
      a.status = target ? "nodes enabled" : "nodes bypassed";
    }
  }

  // copy / paste selected nodes (Ctrl+C / Ctrl+V)
  {
    struct ClipNode {
      std::string type;
      gpx::AttrSet attrs;
      float x, y;
      uint64_t orig_id;
    };
    static std::vector<ClipNode> clip_nodes;
    static std::vector<gpx::Link> clip_links;
    ImGuiIO &io = ImGui::GetIO();
    bool editor_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    if (editor_focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
      clip_nodes.clear();
      clip_links.clear();
      ed::NodeId sel[64];
      int count = ed::GetSelectedNodes(sel, 64);
      std::vector<uint64_t> ids;
      for (int s = 0; s < count; ++s) {
        gpx::Node *n = a.graph.find_node((uint64_t)sel[s].Get());
        if (!n) continue;
        clip_nodes.push_back({n->type, n->attrs, n->pos_x, n->pos_y, n->id});
        ids.push_back(n->id);
      }
      for (const gpx::Link &l : a.graph.links) {
        bool from_in = std::find(ids.begin(), ids.end(), l.from_node) != ids.end();
        bool to_in = std::find(ids.begin(), ids.end(), l.to_node) != ids.end();
        if (from_in && to_in) clip_links.push_back(l);
      }
      if (!clip_nodes.empty())
        a.status = "copied " + std::to_string(clip_nodes.size()) + " node(s)";
    }
    if (!eval_running && editor_focused && io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_V, false) && !clip_nodes.empty()) {
      std::map<uint64_t, uint64_t> remap;
      undo_push_locked(a, "Paste nodes");
      ed::ClearSelection();
      for (const ClipNode &cn : clip_nodes) {
        gpx::Node *n = a.graph.add_node(cn.type, cn.x + 48, cn.y + 48);
        if (!n) continue;
        n->attrs = cn.attrs;
        remap[cn.orig_id] = n->id;
        ed::SetNodePosition(n->id, ImVec2(n->pos_x, n->pos_y));
        ed::SelectNode(n->id, true);
        a.selected_node = n->id;
      }
      for (const gpx::Link &l : clip_links) {
        auto f = remap.find(l.from_node), t = remap.find(l.to_node);
        if (f != remap.end() && t != remap.end())
          a.graph.add_link(f->second, l.from_port, t->second, l.to_port);
      }
      a.request_eval();
    }
  }

  // double-click a node -> pin it to the 3D view
  if (ed::NodeId dbl = ed::GetDoubleClickedNode()) {
    a.view_node = (uint64_t)dbl.Get();
    a.uploaded_serial = 0; // force viewport refresh
  }

  ed::Suspend();
  // Right-clicking a port disconnects it. Dragging a wire off a pin works, but
  // only if you can grab it; on a dense graph the wires overlap and the pin is
  // the thing you can actually hit.
  static uint64_t ctx_pin = 0;
  if (!eval_running) {
    ed::PinId pid;
    if (ed::ShowPinContextMenu(&pid)) {
      ctx_pin = (uint64_t)pid.Get();
      ImGui::OpenPopup("pin_menu");
    }
  }
  if (ImGui::BeginPopup("pin_menu")) {
    uint64_t nid = 0;
    size_t pidx = 0;
    decode_pin(ctx_pin, nid, pidx);
    gpx::Node *n = a.graph.find_node(nid);
    const gpx::Port *port = n && pidx < n->ports.size() ? &n->ports[pidx] : nullptr;
    if (!port) {
      ImGui::TextDisabled("no such port");
    } else {
      // Count first, so the item can say what it will do rather than being a
      // verb that might be a no-op.
      int hits = 0;
      for (const gpx::Link &l : a.graph.links)
        if ((port->dir == gpx::PortDir::In && l.to_node == nid &&
             l.to_port == port->name) ||
            (port->dir == gpx::PortDir::Out && l.from_node == nid &&
             l.from_port == port->name))
          ++hits;
      ImGui::TextDisabled("%s  (%s)", port->name.c_str(),
                          port->dir == gpx::PortDir::In ? "input" : "output");
      ImGui::Separator();
      if (hits == 0) {
        ImGui::TextDisabled("nothing connected");
      } else if (ImGui::MenuItem(hits == 1 ? "Disconnect"
                                           : "Disconnect all")) {
        undo_push_locked(a, "Disconnect " + port->name);
        for (size_t k = a.graph.links.size(); k-- > 0;) {
          const gpx::Link &l = a.graph.links[k];
          if ((port->dir == gpx::PortDir::In && l.to_node == nid &&
               l.to_port == port->name) ||
              (port->dir == gpx::PortDir::Out && l.from_node == nid &&
               l.from_port == port->name))
            a.graph.remove_link(l.id);
        }
        a.request_eval();
      }
      if (hits > 1) ImGui::TextDisabled("%d links", hits);
    }
    ImGui::EndPopup();
  }

  // Adding a node mutates the graph, so it waits for a frame that holds the
  // lock — the same rule the connect and delete paths follow.
  if (!eval_running && ed::ShowBackgroundContextMenu())
    ImGui::OpenPopup("add_node");
  if (ImGui::BeginPopup("add_node")) {
    if (eval_running)
      ImGui::TextDisabled("computing...");
    else
      add_node_popup(a);
    ImGui::EndPopup();
  }
  ed::Resume();

  // "open this in the node editor", asked for from another panel
  if (a.focus_node) {
    ed::SelectNode(a.focus_node, false);
    ed::NavigateToSelection(false, 0.2f);
    a.focus_node = 0;
  }

  static int navigate_countdown = -1;
  if (push_positions) {
    layout_serial_seen = a.graph_layout_serial;
    navigate_countdown = 3; // let node sizes settle before fitting the view
  }
  if (navigate_countdown >= 0 && navigate_countdown-- == 0)
    ed::NavigateToContent(0.f);
  ed::End();
  ed::PopStyleColor(2);
  ed::SetCurrentEditor(nullptr);
  ImGui::End();
}

} // namespace studio










