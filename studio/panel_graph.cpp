// Geekatplay Studio — node graph editors (imgui-node-editor).
//
// There can be several: the main one follows the workspace bar, and any
// number more are pinned to a domain, each with its own canvas, selection
// and side pane, and each able to float out onto another screen. They all
// edit the one graph.
#include "app.hpp"
#include "panel_graph_internal.hpp"
#include "console.hpp"
#include "panel_float.hpp"
#include "prefs.hpp"
#include "theme_colors.hpp"
#include "gpx/port_catalog.hpp"
#include "undo.hpp"
#include "gpx/metanode.hpp"
#include <imgui.h>
#include <imgui_internal.h> // SetPixelDensity: crisp text at any zoom
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

namespace {

std::vector<GraphEditor> &editors() {
  static std::vector<GraphEditor> v;
  return v;
}

const char *DOMAIN_NAMES[5] = {"Terrain", "Materials", "Atmosphere", "Render",
                               "All domains"};

void editor_make(GraphEditor &e, int index, int domain) {
  e.index = index;
  e.domain = domain;
  if (index == 0) {
    e.title = "Graph";
    e.settings = "geekatplay_graph_view.json";
  } else {
    e.title = std::string(DOMAIN_NAMES[std::clamp(domain, 0, 4)]) +
              " nodes###nodes_editor_" + std::to_string(index);
    e.settings = "geekatplay_graph_view_" + std::to_string(index) + ".json";
    e.show_props = true;
  }
  ed::Config cfg;
  cfg.SettingsFile = e.settings.c_str();
  // A view file with absurd values hangs the editor for ever; never hand it
  // one without looking (see graph_view_is_sane).
  discard_insane_graph_view(cfg.SettingsFile);
  e.ctx = ed::CreateEditor(&cfg);
}

void editors_init() {
  std::vector<GraphEditor> &v = editors();
  if (!v.empty()) return;
  v.emplace_back();
  editor_make(v.back(), 0, -1);
  int k = 1;
  for (int d : prefs().editor_domains) {
    v.emplace_back();
    editor_make(v.back(), k++, std::clamp(d, 0, 4));
  }
}

void editors_save_prefs() {
  prefs().editor_domains.clear();
  for (const GraphEditor &e : editors())
    if (e.index > 0 && e.open) prefs().editor_domains.push_back(e.domain);
  prefs_save();
}

} // namespace

void graph_editor_add(App &a, int domain) {
  (void)a;
  editors_init();
  std::vector<GraphEditor> &v = editors();
  int index = 1;
  for (const GraphEditor &e : v) index = std::max(index, e.index + 1);
  v.emplace_back();
  editor_make(v.back(), index, std::clamp(domain, 0, 4));
  v.back().fresh = true;
  editors_save_prefs();
}

// Show a node in the node editor from anywhere else in the application:
// switch to the workspace that holds its category, select it and pan to it.
// The panning itself happens in the editor that draws it next frame.
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

static void draw_graph_editor(App &a, GraphEditor &e);

void draw_panel_graph(App &a) {
  editors_init();
  std::vector<GraphEditor> &v = editors();
  bool closed_any = false;
  for (GraphEditor &e : v) {
    if (!e.open) continue;
    draw_graph_editor(a, e);
    if (!e.open) closed_any = true;
  }
  if (closed_any) {
    for (GraphEditor &e : v)
      if (!e.open && e.ctx) {
        ed::DestroyEditor(e.ctx);
        e.ctx = nullptr;
      }
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const GraphEditor &e) { return !e.open; }),
            v.end());
    editors_save_prefs();
  }
}

// ----------------------------------------------------------- one editor
static void editor_toolbar(App &a, GraphEditor &e, bool can_edit) {
  if (e.index == 0) {
    ImGui::TextDisabled("%s nodes", DOMAIN_NAMES[std::clamp(a.workspace, 0, 3)]);
  } else {
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##dom", &e.domain,
                 "Terrain\0Materials\0Atmosphere\0Render\0All domains\0");
    if (ImGui::IsItemDeactivatedAfterEdit()) editors_save_prefs();
  }
  ImGui::SameLine();
  if (e.domain != 4) {
    if (e.index == 0) {
      studio::Checkbox("show all domains", &a.graph_show_all_domains);
      e.show_all = a.graph_show_all_domains;
    } else {
      studio::Checkbox("show all domains", &e.show_all);
    }
    ImGui::SameLine();
  }
  studio::Checkbox("parameters", &e.show_props);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The selected node's parameters in a pane on the right\n"
                      "of this editor - the Properties panel, but here.");
  if (!can_edit) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
    ImGui::Text("computing %d/%d", a.eval.progress_done.load(),
                a.eval.progress_total.load());
    ImGui::PopStyleColor();
  }
}

static void draw_graph_editor(App &a, GraphEditor &e) {
  panel_float_prepare(a, e.title.c_str());
  if (e.fresh) {
    // Just created: as a tab beside the main Graph, whatever the layout file
    // remembers for a window of this name, from where one click on the
    // corner button floats it out. Editors restored from the preferences
    // keep the place the user left them in.
    e.fresh = false;
    if (ImGuiWindow *g = ImGui::FindWindowByName("Graph"); g && g->DockId)
      ImGui::SetNextWindowDockID(g->DockId, ImGuiCond_Always);
    else
      ImGui::SetNextWindowSize(ImVec2(960, 640), ImGuiCond_Always);
  }
  bool *p_open = e.index == 0 ? nullptr : &e.open;
  if (!ImGui::Begin(e.title.c_str(), p_open)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, e.title.c_str());
  // The graph is always drawn from App::node_views, so it never blinks out
  // while evaluation holds the lock. Editing needs the real graph, so those
  // paths are simply skipped for the frames where the lock is busy.
  std::unique_lock<std::mutex> graph_lock(a.graph_mtx, std::try_to_lock);
  bool can_edit = graph_lock.owns_lock();
  editor_toolbar(a, e, can_edit);

  // the canvas, and beside it the side pane when asked for
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  float canvas_w = avail.x;
  if (e.show_props) {
    e.props_w = std::clamp(e.props_w, 220.f, std::max(220.f, avail.x - 200.f));
    canvas_w = avail.x - e.props_w - 6.f;
  }
  // The editor is placed straight in the window (its own Begin takes a
  // size); wrapping it in a child of ours cost the right-click context menu.
  ed::SetCurrentEditor(e.ctx);
  ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(0.075f, 0.075f, 0.08f, 1.f));
  ed::PushStyleColor(ed::StyleColor_Grid, ImVec4(1.f, 1.f, 1.f, 0.025f));
  const ImVec4 acc = ImGui::ColorConvertU32ToFloat4(theme::accent());
  ed::PushStyleColor(ed::StyleColor_HovNodeBorder, ImVec4(acc.x, acc.y, acc.z, 0.6f));
  ed::PushStyleColor(ed::StyleColor_SelNodeBorder, acc);
  ed::PushStyleColor(ed::StyleColor_HovLinkBorder, ImVec4(acc.x, acc.y, acc.z, 0.7f));
  ed::PushStyleColor(ed::StyleColor_SelLinkBorder, acc);
  ed::PushStyleColor(ed::StyleColor_PinRect, ImVec4(acc.x, acc.y, acc.z, 0.35f));
  ed::PushStyleColor(ed::StyleColor_PinRectBorder, ImVec4(acc.x, acc.y, acc.z, 0.9f));
  ed::PushStyleVar(ed::StyleVar_HoveredNodeBorderWidth, 2.f);
  ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderWidth, 2.5f);
  ed::PushStyleVar(ed::StyleVar_LinkStrength, 120.f);
  ed::PushStyleVar(ed::StyleVar_PinRadius, 0.f);
  ed::Begin("GeekatplayGraph", ImVec2(e.show_props ? canvas_w : 0.f, 0.f));
  // Text is rasterised for the zoom the canvas is drawn at. The editor scales
  // its draw list after the fact, so glyphs baked at the base size arrive
  // stretched — that was the blur on zooming in. GetCurrentZoom() is the
  // inverse scale; the pixel density is the scale itself.
  const bool dyn_fonts =
      (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;
  const float fb_scale = ImGui::GetWindowViewport()->FramebufferScale.x;
  if (dyn_fonts) {
    float inv = ed::GetCurrentZoom();
    float scale = inv > 1e-4f ? 1.f / inv : 1.f;
    ImGui::SetPixelDensity(std::clamp(scale, 0.5f, 4.f) * fb_scale);
  }

  bool eval_running = !can_edit;
  // collapse requests raised by last frame's chevrons or the H key
  if (!eval_running && !g_collapse_requests.empty()) {
    for (const CollapseRequest &r : g_collapse_requests)
      if (gpx::Node *n = a.graph.find_node(r.node))
        n->ui_collapse = r.mode < 0 ? (n->ui_collapse + 1) % 3
                                    : std::clamp(r.mode, 0, 2);
    g_collapse_requests.clear();
    a.refresh_snapshot();
  } else if (eval_running) {
    g_collapse_requests.clear();
  }

  const int domain = e.effective_domain(a);
  const bool all = e.all_domains();
  // A node belongs to the workspace of its category — and also to any
  // workspace it is wired into. ErosionLayers sits in Terrain, but once its
  // masks feed a MaterialStack it is part of the material too, so it shows
  // there as well: the shared node the two editors have in common.
  std::vector<uint64_t> bridged;
  if (!all) {
    auto dom_of = [&](uint64_t id) -> int {
      for (const auto &n : a.node_views)
        if (n.id == id) return domain_of_category(n.category);
      return -1;
    };
    for (const App::LinkView &l : a.link_views) {
      int df = dom_of(l.from_node), dt = dom_of(l.to_node);
      if (df == dt) continue;
      if (dt == domain) bridged.push_back(l.from_node);
      if (df == domain) bridged.push_back(l.to_node);
    }
  }
  auto view_visible = [&](const App::NodeView &n) {
    if (all || domain_of_category(n.category) == domain) return true;
    return std::find(bridged.begin(), bridged.end(), n.id) != bridged.end();
  };
  bool push_positions = e.layout_serial_seen != a.graph_layout_serial;
  // Which nodes the editor actually laid out this frame. Asking it for the
  // position of a node it has never drawn returns (0,0), and writing that back
  // destroys the position the node was created with — which is why a graph
  // built by script or by a preset arrived in a single heap at the origin.
  {
    if (push_positions)
      for (const auto &n : a.node_views)
        ed::SetNodePosition(n.id, ImVec2(n.pos_x, n.pos_y));
    e.drawn_this_frame.clear();
    for (const auto &n : a.node_views)
      if (view_visible(n)) {
        draw_node(a, n);
        e.drawn_this_frame.push_back(n.id);
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
      // an output identically (TerrainOutput has `heightmap` both ways).
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
      // the wire is the colour of what it carries, same as the connector
      const App::PortView &fp = fn->ports[fi];
      ImVec4 lc = ImGui::ColorConvertU32ToFloat4(
          theme::port_color(fp.is_texture, fp.is_field, fp.field_type, fp.is_points));
      lc.w = 0.85f;
      ed::Link(l.id, pin_id(l.from_node, fi), pin_id(l.to_node, ti), lc, 2.2f);
    }
  }

  editor_create_delete(a, e, eval_running, acc);

  // selection sync + store positions
  {
    // Only nodes the editor drew this frame have a position worth trusting,
    // and never on the frame we just pushed positions into it.
    if (!push_positions && can_edit)
      for (auto &n : a.graph.nodes) {
        bool drawn = false;
        for (uint64_t id : e.drawn_this_frame)
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
      e.selected = picked;
      // the last editor the user clicked in owns the global selection
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) ||
          a.selected_node == 0)
        a.selected_node = picked;
    }
  }

  editor_shortcuts(a, e, eval_running, can_edit);

  // double-click a node -> pin it to the 3D view
  if (ed::NodeId dbl = ed::GetDoubleClickedNode()) {
    a.view_node = (uint64_t)dbl.Get();
    a.uploaded_serial = 0; // force viewport refresh
  }

  editor_context_menus(a, eval_running, can_edit, domain, all);

  // "open this in the node editor", asked for from another panel: the
  // editor that can show the node takes it
  if (a.focus_node) {
    bool here = false;
    for (const auto &n : a.node_views)
      if (n.id == a.focus_node && view_visible(n)) here = true;
    if (here) {
      ed::SelectNode(a.focus_node, false);
      ed::NavigateToSelection(false, 0.2f);
      e.selected = a.focus_node;
      a.focus_node = 0;
    }
  }

  if (push_positions) {
    e.layout_serial_seen = a.graph_layout_serial;
    e.navigate_countdown = 3; // let node sizes settle before fitting the view
  }
  if (e.navigate_countdown >= 0 && e.navigate_countdown-- == 0)
    ed::NavigateToContent(0.f);
  ed::End();
  if (dyn_fonts) ImGui::SetPixelDensity(fb_scale);
  ed::PopStyleVar(4);
  ed::PopStyleColor(8);
  ed::SetCurrentEditor(nullptr);

  // the side pane: this editor's selected node, its parameters
  if (e.show_props) {
    ImGui::SameLine(0, 0);
    // a splitter: drag to resize the pane
    ImGui::InvisibleButton("##split", ImVec2(6.f, -1.f));
    if (ImGui::IsItemActive()) e.props_w -= ImGui::GetIO().MouseDelta.x;
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    {
      ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddLine(ImVec2((p0.x + p1.x) * 0.5f, p0.y),
                                          ImVec2((p0.x + p1.x) * 0.5f, p1.y),
                                          theme::fade(theme::text_dim(), 0.4f));
    }
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##props", ImVec2(0, 0), ImGuiChildFlags_Borders);
    // The graph lock is this frame's try-lock; the parameter pane takes its
    // own, so release ours first or it can never write an edit through.
    if (graph_lock.owns_lock()) graph_lock.unlock();
    uint64_t show = e.selected ? e.selected : a.selected_node;
    node_properties_ui(a, show, true);
    ImGui::EndChild();
  }
  ImGui::End();
}

} // namespace studio
