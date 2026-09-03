// Geekatplay TerraForge — the node editor's edit operations: wiring and
// deleting, keyboard shortcuts, and the pin / canvas context menus. Split from
// panel_graph.cpp for the 500-line module rule; the bodies are verbatim, the
// editor calls them in the same order it used to run them inline.
#include "app.hpp"
#include "panel_graph_internal.hpp"
#include "console.hpp"
#include "undo.hpp"
#include "gpx/port_catalog.hpp"
#include "gpx/metanode.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ed = ax::NodeEditor;

namespace studio {

// link creation and deletion (BeginCreate / BeginDelete)
void editor_create_delete(App &a, GraphEditor &e, bool eval_running,
                          const ImVec4 &acc) {

  // link creation
  if (!eval_running && ed::BeginCreate(acc, 2.f)) {
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
            !gpx::ports_compatible(fp->type, tp->type) || fn == tn) {
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
        if (e.selected == (uint64_t)nid.Get()) e.selected = 0;
        a.request_eval();
      }
    }
  }
  ed::EndDelete();
}

// keyboard shortcuts on a focused editor: H, Ctrl+G, Ctrl+Shift+G, Ctrl+E,
// Ctrl+C / Ctrl+V
void editor_shortcuts(App &a, GraphEditor &e, bool eval_running, bool can_edit) {

  const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
  // H cycles the detail level of the selection: expanded, compact, title bar
  if (focused && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
    ed::NodeId sel[64];
    int count = ed::GetSelectedNodes(sel, 64);
    for (int i = 0; i < count; ++i)
      g_collapse_requests.push_back({(uint64_t)sel[i].Get(), -1});
  }

  // group the selection into a MetaNode (Ctrl+G) / expand one (Ctrl+Shift+G)
  if (!eval_running && focused && ImGui::GetIO().KeyCtrl &&
      ImGui::IsKeyPressed(ImGuiKey_G, false)) {
    ed::NodeId sel[64];
    int count = ed::GetSelectedNodes(sel, 64);
    std::string err;
    if (ImGui::GetIO().KeyShift) {
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
  if (!eval_running && focused && ImGui::GetIO().KeyCtrl &&
      ImGui::IsKeyPressed(ImGuiKey_E, false)) {
    ed::NodeId sel[32];
    int count = ed::GetSelectedNodes(sel, 32);
    if (count > 0) {
      undo_push_locked(a, count > 1 ? "Bypass nodes" : "Bypass node");
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

  // copy / paste selected nodes (Ctrl+C / Ctrl+V); one clipboard for all
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
    if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && can_edit) {
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
    if (!eval_running && focused && io.KeyCtrl &&
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
}

// the pin menu and the create menu, drawn with the editor suspended
void editor_context_menus(App &a, bool eval_running, bool can_edit, int domain,
                          bool all) {

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
    gpx::Node *n = can_edit ? a.graph.find_node(nid) : nullptr;
    const gpx::Port *port = n && pidx < n->ports.size() ? &n->ports[pidx] : nullptr;
    if (!port) {
      ImGui::TextDisabled(can_edit ? "no such port" : "computing...");
    } else {
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
      } else if (ImGui::MenuItem(hits == 1 ? "Disconnect" : "Disconnect all")) {
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
  if (ed::ShowBackgroundContextMenu()) {
    log_trace("graph", eval_running ? "right-click on canvas (computing)"
                                    : "right-click on canvas: create menu");
    g_drag_create.clear(); // a plain right-click filters nothing
    ImGui::OpenPopup("add_node");
  }
  if (ImGui::BeginPopup("add_node")) {
    if (eval_running) {
      ImGui::TextDisabled("computing...");
    } else {
      g_popup_domain = domain;
      g_popup_all = all;
      add_node_popup(a);
    }
    ImGui::EndPopup();
  }
  // A drag whose menu was dismissed must not leave the next right-click
  // filtered by a port nobody is holding any more.
  if (!ImGui::IsPopupOpen("add_node") && g_drag_create.active())
    g_drag_create.clear();
  ed::Resume();
}

} // namespace studio
