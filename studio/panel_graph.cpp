// Geekatplay Studio â€” node graph panel (imgui-node-editor)
#include "app.hpp"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace ed = ax::NodeEditor;

namespace studio {

static ed::EditorContext *ED = nullptr;

// pin id encoding: node_id * 4096 + port index + 1
static uint64_t pin_id(uint64_t node, size_t port) { return node * 4096 + port + 1; }
static void decode_pin(uint64_t pin, uint64_t &node, size_t &port) {
  node = pin / 4096;
  port = (size_t)(pin % 4096) - 1;
}

static ImU32 category_color(const std::string &cat) {
  if (cat == "Primitive") return IM_COL32(0x7a, 0x8c, 0x4f, 255);
  if (cat == "Erosion") return IM_COL32(0x9c, 0x5a, 0x3c, 255);
  if (cat == "Filter") return IM_COL32(0x4f, 0x6d, 0x8c, 255);
  if (cat == "Operator") return IM_COL32(0x8c, 0x7a, 0x4f, 255);
  if (cat == "Mask") return IM_COL32(0x6d, 0x4f, 0x8c, 255);
  if (cat == "Transform") return IM_COL32(0x4f, 0x8c, 0x82, 255);
  if (cat == "Texture") return IM_COL32(0xc8, 0x78, 0x30, 255);
  if (cat == "Material") return IM_COL32(0xa8, 0x68, 0x28, 255);
  if (cat == "Logic") return IM_COL32(0x8c, 0x8c, 0x8c, 255);
  if (cat == "Export") return IM_COL32(0x5a, 0x5a, 0x5a, 255);
  return IM_COL32(0x66, 0x66, 0x66, 255);
}

static ImU32 port_color(gpx::DataType t) {
  return t == gpx::DataType::Heightmap ? IM_COL32(0xd6, 0xd3, 0xcd, 255)
                                       : IM_COL32(0xc8, 0x78, 0x30, 255);
}

static void draw_node(App &a, gpx::Node &n) {
  ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.12f, 0.12f, 0.12f, 0.96f));
  ed::PushStyleColor(ed::StyleColor_NodeBorder,
                     n.error.empty() ? ImVec4(0.04f, 0.04f, 0.04f, 1.f)
                                     : ImVec4(0.8f, 0.2f, 0.15f, 1.f));
  ed::PushStyleVar(ed::StyleVar_NodeRounding, 0.f);
  ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(10, 6, 10, 8));
  ed::BeginNode(n.id);

  // header: colored bar + title
  ImU32 hc = category_color(n.category);
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 227, 220, 255));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 hp = ImGui::GetCursorScreenPos();
  dl->AddRectFilled(ImVec2(hp.x - 6, hp.y - 2), ImVec2(hp.x - 2, hp.y + 14), hc);
  ImGui::TextUnformatted(n.type.c_str());
  ImGui::PopStyleColor();
  ImGui::Spacing();

  // pins: inputs left column, outputs right column
  ImGui::BeginGroup();
  for (size_t i = 0; i < n.ports.size(); ++i) {
    gpx::Port &p = n.ports[i];
    if (p.dir != gpx::PortDir::In) continue;
    ed::BeginPin(pin_id(n.id, i), ed::PinKind::Input);
    ImU32 pc = port_color(p.type);
    ImVec2 c = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(c.x, c.y + 3), ImVec2(c.x + 8, c.y + 11), pc);
    ImGui::Dummy(ImVec2(10, 14));
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text,
                          p.optional ? IM_COL32(125, 122, 117, 255)
                                     : IM_COL32(214, 211, 205, 255));
    ImGui::TextUnformatted(p.name.c_str());
    ImGui::PopStyleColor();
    ed::PinRect(ImVec2(c.x - 4, c.y), ImVec2(c.x + 12, c.y + 14));
    ed::EndPin();
  }
  ImGui::EndGroup();

  // preview thumbnail
  if (unsigned tex = previews_get(n.id)) {
    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(112, 112));
  }

  // outputs
  for (size_t i = 0; i < n.ports.size(); ++i) {
    gpx::Port &p = n.ports[i];
    if (p.dir != gpx::PortDir::Out) continue;
    float tw = ImGui::CalcTextSize(p.name.c_str()).x;
    ImGui::Dummy(ImVec2(112.f - tw - 18.f > 0 ? 112.f - tw - 18.f : 0.f, 1));
    ImGui::SameLine();
    ed::BeginPin(pin_id(n.id, i), ed::PinKind::Output);
    ImGui::TextUnformatted(p.name.c_str());
    ImGui::SameLine();
    ImVec2 c = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(c.x, c.y + 3), ImVec2(c.x + 8, c.y + 11),
                      port_color(p.type));
    ImGui::Dummy(ImVec2(10, 14));
    ed::PinRect(ImVec2(c.x - 4, c.y), ImVec2(c.x + 12, c.y + 14));
    ed::EndPin();
  }

  if (n.last_compute_ms > 0.01) {
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(125, 122, 117, 255));
    ImGui::Text("%.1f ms", n.last_compute_ms);
    ImGui::PopStyleColor();
  }
  if (!n.error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 80, 60, 255));
    ImGui::TextUnformatted(n.error.c_str());
    ImGui::PopStyleColor();
  }
  ed::EndNode();
  ed::PopStyleVar(2);
  ed::PopStyleColor(2);
}

static void add_node_popup(App &a) {
  static char filter[64] = "";
  if (ImGui::IsWindowAppearing()) {
    filter[0] = 0;
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##filter", "search nodes...", filter, sizeof filter);
  ImVec2 click_pos = ImGui::GetMousePosOnOpeningCurrentPopup();
  std::string last_cat;
  for (const gpx::NodeDef *d : gpx::NodeRegistry::instance().all()) {
    if (!a.graph_show_all_domains &&
        domain_of_category(d->category) != a.workspace)
      continue;
    if (filter[0]) {
      std::string lt = d->type, lf = filter;
      for (auto &ch : lt) ch = (char)tolower(ch);
      for (auto &ch : lf) ch = (char)tolower(ch);
      if (lt.find(lf) == std::string::npos) continue;
    }
    if (d->category != last_cat) {
      if (!last_cat.empty()) ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(125, 122, 117, 255));
      ImGui::TextUnformatted(d->category.c_str());
      ImGui::PopStyleColor();
      last_cat = d->category;
    }
    if (ImGui::MenuItem(d->type.c_str())) {
      ImVec2 cp = ed::ScreenToCanvas(click_pos);
      gpx::Node *n = a.graph.add_node(d->type, cp.x, cp.y);
      if (n) {
        ed::SetNodePosition(n->id, cp);
        a.selected_node = n->id;
        a.request_eval();
      }
    }
    if (ImGui::IsItemHovered() && !d->description.empty())
      ImGui::SetTooltip("%s", d->description.c_str());
  }
}

void draw_panel_graph(App &a) {
  if (!ED) {
    ed::Config cfg;
    cfg.SettingsFile = "geekatplay_graph_view.json";
    ED = ed::CreateEditor(&cfg);
  }
  if (!ImGui::Begin("Graph")) {
    ImGui::End();
    return;
  }
  // never freeze the UI while a long evaluation holds the graph: if the
  // mutex is busy, show a status line this frame instead of blocking
  std::unique_lock<std::mutex> graph_lock(a.graph_mtx, std::try_to_lock);
  if (!graph_lock.owns_lock()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
    ImGui::Text("computing... (%d/%d)", a.eval.progress_done.load(),
                a.eval.progress_total.load());
    ImGui::PopStyleColor();
    ImGui::End();
    return;
  }
  {
    const char *ws_names[4] = {"Terrain", "Materials", "Atmosphere", "Render"};
    ImGui::TextDisabled("%s nodes", ws_names[std::clamp(a.workspace, 0, 3)]);
    ImGui::SameLine();
    ImGui::Checkbox("show all domains", &a.graph_show_all_domains);
  }
  ed::SetCurrentEditor(ED);
  ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(0.075f, 0.075f, 0.08f, 1.f));
  ed::PushStyleColor(ed::StyleColor_Grid, ImVec4(1.f, 1.f, 1.f, 0.025f));
  ed::Begin("GeekatplayGraph");

  bool eval_running = a.eval.running.load();

  auto node_visible = [&](const gpx::Node &n) {
    return a.graph_show_all_domains || domain_of_category(n.category) == a.workspace;
  };
  static uint64_t layout_serial_seen = 0;
  bool push_positions = layout_serial_seen != a.graph_layout_serial;
  {
    if (push_positions)
      for (auto &n : a.graph.nodes)
        ed::SetNodePosition(n->id, ImVec2(n->pos_x, n->pos_y));
    for (auto &n : a.graph.nodes)
      if (node_visible(*n)) draw_node(a, *n);
    for (const gpx::Link &l : a.graph.links) {
      gpx::Node *fn = a.graph.find_node(l.from_node);
      gpx::Node *tn = a.graph.find_node(l.to_node);
      if (!fn || !tn) continue;
      if (!node_visible(*fn) || !node_visible(*tn)) continue;
      size_t fi = 0, ti = 0;
      for (size_t i = 0; i < fn->ports.size(); ++i)
        if (fn->ports[i].name == l.from_port) fi = i;
      for (size_t i = 0; i < tn->ports.size(); ++i)
        if (tn->ports[i].name == l.to_port) ti = i;
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
          a.graph.remove_link((uint64_t)lid.Get());
        a.request_eval();
      }
    }
    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid)) {
      if (ed::AcceptDeletedItem()) {
          a.graph.remove_node((uint64_t)nid.Get());
        if (a.selected_node == (uint64_t)nid.Get()) a.selected_node = 0;
        a.request_eval();
      }
    }
  }
  ed::EndDelete();

  // selection sync + store positions
  {
    for (auto &n : a.graph.nodes) {
      ImVec2 p = ed::GetNodePosition(n->id);
      n->pos_x = p.x;
      n->pos_y = p.y;
    }
    ed::NodeId sel[8];
    int count = ed::GetSelectedNodes(sel, 8);
    if (count > 0) {
      uint64_t picked = (uint64_t)sel[0].Get();
      if (picked != a.selected_node) a.prop_tab = TAB_NODE;
      a.selected_node = picked;
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
    if (editor_focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) &&
        !clip_nodes.empty()) {
      std::map<uint64_t, uint64_t> remap;
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
  if (ed::ShowBackgroundContextMenu()) ImGui::OpenPopup("add_node");
  if (ImGui::BeginPopup("add_node")) {
    add_node_popup(a);
    ImGui::EndPopup();
  }
  ed::Resume();

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


