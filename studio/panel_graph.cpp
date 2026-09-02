// Geekatplay Studio — node graph panel (imgui-node-editor)
#include "app.hpp"
#include "panel_graph_internal.hpp"
#include "console.hpp"
#include "theme_colors.hpp"
#include "gpx/port_catalog.hpp"
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

    // A wire dropped on empty canvas: offer to create something that can
    // take it. The menu opens on the next frame, filtered by this port.
    ed::PinId new_pin;
    if (ed::QueryNewNode(&new_pin) && new_pin) {
      log_trace("graph", "drag ended on canvas, pin " +
                             std::to_string((uint64_t)new_pin.Get()));
      uint64_t nn;
      size_t pp;
      decode_pin((uint64_t)new_pin.Get(), nn, pp);
      gpx::Node *src = a.graph.find_node(nn);
      if (src && pp < src->ports.size() && ed::AcceptNewItem()) {
        const gpx::Port &port = src->ports[pp];
        g_drag_create.node = nn;
        g_drag_create.port = port.name;
        g_drag_create.type = port.type;
        g_drag_create.field_type = port.field_type;
        // the new node must offer the opposite direction to the one dragged
        g_drag_create.want_dir = port.dir == gpx::PortDir::Out
                                     ? gpx::PortDir::In
                                     : gpx::PortDir::Out;
        ed::Suspend();
        ImGui::OpenPopup("add_node");
        ed::Resume();
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
  if (!eval_running && ed::ShowBackgroundContextMenu()) {
    g_drag_create.clear(); // a plain right-click filters nothing
    ImGui::OpenPopup("add_node");
  }
  if (ImGui::BeginPopup("add_node")) {
    if (eval_running)
      ImGui::TextDisabled("computing...");
    else
      add_node_popup(a);
    ImGui::EndPopup();
  }
  // A drag whose menu was dismissed must not leave the next right-click
  // filtered by a port nobody is holding any more.
  if (!ImGui::IsPopupOpen("add_node") && g_drag_create.active())
    g_drag_create.clear();
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










