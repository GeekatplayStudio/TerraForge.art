// Geekatplay Studio — main loop, docking layout, background evaluation
#include "app.hpp"
#include "console.hpp"
#include "autosave.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "planet_renderer.hpp"
#include "scene.hpp"
#include "gpx/field_glsl.hpp"
#include "gpx/planet_math.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

namespace studio {

// the last heightmap handed to the renderer, kept so the points overlay can
// sample real elevations without re-walking the graph
static std::shared_ptr<gpx::Heightmap> g_overlay_terrain;

// Rebuild every scattered object's copy list from its bound Points node.
// Called when the evaluation moves, and again by the render exporter so a
// scripted set_scatter -> render in one batch never ships an empty forest.
void scene_rebuild_scatter_instances(App &a) {
  const gpx::Heightmap *hm = g_overlay_terrain && !g_overlay_terrain->empty()
                                 ? g_overlay_terrain.get()
                                 : nullptr;
  float hs = render_settings().height_scale;
  for (SceneObject &o : scene().objects) {
    if (o.type != SceneObject::Mesh || !o.scatter_node) {
      o.inst.clear();
      continue;
    }
    gpx::Node *sn = a.graph.find_node(o.scatter_node);
    const gpx::PointCloud *pc = nullptr;
    if (sn)
      for (const gpx::Port &p : sn->ports)
        if (p.dir == gpx::PortDir::Out && p.type == gpx::DataType::Points &&
            p.pts && p.pts->size()) {
          pc = p.pts.get();
          break;
        }
    o.inst.clear();
    if (!pc) continue;
    size_t count = std::min(pc->size(), (size_t)4096);
    o.inst.reserve(count * 8);
    for (size_t i = 0; i < count; ++i) {
      float px = pc->x[i], pz = pc->y[i], py = 0.f;
      if (hm) {
        int ix = std::clamp((int)(px * hm->w), 0, hm->w - 1);
        int iy = std::clamp((int)(pz * hm->h), 0, hm->h - 1);
        py = hm->v[(size_t)iy * hm->w + ix] * hs;
      }
      uint32_t hb = gpx::planet::pl_hash_bits((int)i, 11, 0, o.scatter_seed);
      float yaw = (hb & 0xffffu) / 65535.f * 6.2831853f;
      float sj =
          1.f + (((hb >> 16) & 0xffu) / 255.f - 0.5f) * o.scatter_jitter;
      float sc = o.scatter_scale * sj;
      // point values scale the copies when asked: a power-law cloud then
      // reads as many saplings and a few grown trees
      if (o.scatter_value_size > 0.f) {
        float vv = std::clamp(pc->v[i], 0.f, 2.f);
        sc *= 1.f + (vv - 1.f) * o.scatter_value_size;
        sc = std::max(sc, 0.02f * o.scatter_scale);
      }
      o.inst.push_back(px);
      o.inst.push_back(py);
      o.inst.push_back(pz);
      o.inst.push_back(sc);
      o.inst.push_back(std::cos(yaw));
      o.inst.push_back(std::sin(yaw));
      // a per-copy brightness so a stand of one mesh reads as many
      o.inst.push_back(0.85f + ((hb >> 24) & 0xffu) / 255.f * 0.3f);
      o.inst.push_back(0.f);
    }
  }
}

// studio/layout.cpp
void build_default_layout(ImGuiID dockspace_id, int view_count);


void App::refresh_snapshot() {
  node_views.clear();
  link_views.clear();
  node_views.reserve(graph.nodes.size());
  size_t bytes = 0;
  double total = 0;
  for (const auto &n : graph.nodes) {
    NodeView v;
    v.id = n->id;
    v.type = n->type;
    v.category = n->category;
    v.error = n->error;
    // Node errors used to live only on the node, as red text you had to be
    // looking at. Mirroring them here puts every one in the console, where it
    // can be read after the fact and copied. log_add collapses repeats, so a
    // node failing every frame is one line with a count.
    if (!n->error.empty())
      log_error("graph", n->type + ": " + n->error);
    v.pos_x = n->pos_x;
    v.pos_y = n->pos_y;
    v.ms = n->last_compute_ms;
    v.enabled = n->enabled;
    total += n->last_compute_ms;
    v.ports.reserve(n->ports.size());
    for (const auto &p : n->ports) {
      PortView pv;
      pv.name = p.name;
      pv.is_input = p.dir == gpx::PortDir::In;
      pv.is_texture = p.type == gpx::DataType::Texture;
      pv.is_field = p.type == gpx::DataType::Field;
      pv.is_points = p.type == gpx::DataType::Points;
      pv.field_type = (unsigned)p.field_type;
      pv.optional = p.optional;
      v.ports.push_back(std::move(pv));
      if (p.hmap) bytes += p.hmap->v.size() * sizeof(float);
      if (p.tex) bytes += p.tex->v.size() * sizeof(float);
    }
    node_views.push_back(std::move(v));
  }
  for (const auto &l : graph.links)
    link_views.push_back({l.id, l.from_node, l.to_node, l.from_port, l.to_port});
  snapshot_resolution = graph.resolution;
  snapshot_bytes = bytes;
  snapshot_total_ms = total;
}

static void eval_worker(App &a) {
  while (true) {
    if (!a.eval.request.exchange(false)) {
      if (a.eval.running.load() == false && a.window == nullptr) return;
      if (glfwWindowShouldClose(a.window)) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      continue;
    }
    a.eval.running.store(true);
    {
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      a.graph.cancel.store(false);
      a.graph.on_progress = [&a](int done, int total, const std::string &t) {
        a.eval.progress_done.store(done);
        a.eval.progress_total.store(total);
        std::lock_guard<std::mutex> lk2(a.eval.mtx);
        a.eval.current_node = t;
      };
      // interactive drags compute at reduced resolution for smoothness;
      // any resolution switch forces a full recompute so no node keeps
      // stale-size buffers
      static int last_eval_res = 0;
      int full = a.graph.resolution;
      int intended = a.eval_interactive.load()
                         ? std::min(prefs().interactive_res, full)
                         : full;
      if (intended != last_eval_res) a.graph.mark_all_dirty();
      last_eval_res = intended;
      a.graph.resolution = intended;
      // The memory ceiling. Nothing is released while the graph fits, so the
      // default is a floor under the worst case rather than a constraint on
      // ordinary work. What the viewport shows and what the user has selected
      // are kept whatever the budget says — those are the two buffers someone
      // is looking at.
      a.graph.buffer_budget = prefs().graph_memory_mb > 0
                                  ? (size_t)prefs().graph_memory_mb * 1024u * 1024u
                                  : 0;
      a.graph.protected_nodes.clear();
      if (a.view_node) a.graph.protected_nodes.push_back(a.view_node);
      if (a.selected_node) a.graph.protected_nodes.push_back(a.selected_node);
      a.graph.evaluate();
      a.graph.resolution = full;
      a.graph.on_progress = nullptr;
    }
    a.eval_serial++;
    a.eval.running.store(false);
  }
}

void run_main() {
  // Before anything else can write to it: GLFW, the drivers and the shader
  // compilers all report through stderr, and this is a windowed process, so
  // without this those messages go nowhere at all.
  log_capture_stderr();
  log_info("app", "TerraForge starting");
  autosave_session_begin(); // detects whether the last one ended cleanly
  App &a = app();
  renderer_init();
  scene_init_builtins();
  project_default_graph(a);
  a.request_eval();
  a.eval.worker = std::thread(eval_worker, std::ref(a));

  bool first_frame = true;
  while (!glfwWindowShouldClose(a.window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // full-window dockspace under the toolbar
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##host", nullptr, host_flags);
    ImGui::PopStyleVar();

    camera_apply_film(); // exposure + film stock of the active camera
    draw_toolbar(a);     // row 1: classic text menus

    // Rows 2 and 3, then the global tool column beside the dockspace. Each is
    // its own band so the eye can find them: what you are working on, then the
    // tools for that work, then everything else.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 3));
    ImGui::BeginChild("##wsbar", ImVec2(0, 30), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    draw_workspace_bar(a);
    ImGui::EndChild();
    ImGui::BeginChild("##toolbar", ImVec2(0, 28), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    draw_tool_bar(a);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // The global tools used to be a 56 px column down the left edge, which
    // spent a whole column of the window on seven buttons and put undo where
    // no application keeps it. They are icons on the menu row now, where the
    // hand already is.

    // version bumped whenever the default layout changes shape
    ImGuiID dockspace_id = ImGui::GetID("GeekatplayDockspaceV7");
    if (first_frame || a.request_layout_reset) {
      if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr ||
          a.request_layout_reset)
        build_default_layout(dockspace_id, prefs().view_count);
      a.request_layout_reset = false;
      first_frame = false;
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();

    // keep the UI snapshot fresh whenever evaluation is not holding the lock
    {
      std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock()) a.refresh_snapshot();
    }
    if (a.show_library) draw_panel_library(a);
  if (a.show_nodelist) draw_panel_nodelist(a);
    if (a.show_viewport) draw_panel_viewport(a);
    draw_panel_graph(a);
    draw_console(a);
    if (a.show_properties) draw_panel_properties(a);
    // apply material maps from the graph to the renderer
    {
      std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock()) {
        RenderSettings &rs = render_settings();
        auto tex_of = [&](uint64_t id) -> const gpx::TextureRGBA * {
          gpx::Node *n = a.graph.find_node(id);
          if (!n) return nullptr;
          gpx::Port *p = n->first_out(gpx::DataType::Texture);
          return (p && p->tex && !p->tex->empty()) ? p->tex.get() : nullptr;
        };
        // a MaterialOutput assigned to the terrain supplies its own channels
        gpx::Node *mat_out = nullptr;
        for (const SceneObject &o : scene().objects)
          if (o.type == SceneObject::Terrain && o.material_node)
            mat_out = a.graph.find_node(o.material_node);
        if (mat_out && mat_out->type == "MaterialOutput") {
          auto chan = [&](const char *port) -> const gpx::TextureRGBA * {
            const gpx::TextureRGBA *t = mat_out->in_tex(port);
            return (t && !t->empty()) ? t : nullptr;
          };
          renderer_set_material_maps(chan("normal"), chan("roughness"),
                                     chan("height"), a.eval_serial);
        } else {
          renderer_set_material_maps(tex_of(rs.map_normal_node),
                                     tex_of(rs.map_roughness_node),
                                     tex_of(rs.map_displacement_node),
                                     a.eval_serial);
        }
      }
    }
    draw_panel_ai(a);
    draw_panel_scene(a); // Outliner
    render_service_requests(a);
    draw_render_window(a);
    autosave_recovery_dialog(a); // offers the last session back after a crash
    autosave_tick(a, glfwGetTime());
    studio_api_tick(a); // apply queued script/MCP actions, publish state

    // upload fresh eval results to GPU (main thread only)
    if (a.uploaded_serial != a.eval_serial && !a.eval.running.load()) {
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      // skip thumbnail regeneration during interactive drags — keeps the
      // slider->viewport loop as tight as possible
      if (!a.eval_interactive.load()) previews_update(a);
      // Terragen-style: atmosphere/render nodes drive the renderer
      apply_scene_nodes(a);

      // A TerrainDisplacement node compiles its field graph to GLSL and the
      // viewport evaluates it per vertex. Transpiling is pure string work on
      // the CPU; the relink happens at draw time where the context is current.
      {
        // Compile whichever sink is present into the shader it drives. Both
        // follow the same shape: find the node, transpile what feeds it, hand
        // the source to the renderer, and say so on the node when we cannot.
        // Buffers the generated programs sample, collected across both sinks.
        std::vector<std::pair<std::string, const gpx::Heightmap *>> field_tex;
        // A sampler is named after the Sample node that declared it, so the
        // buffer to bind is that node's own input.
        auto collect_samplers = [&](const gpx::GlslProgram &prog) {
          for (const std::string &s : prog.samplers) {
            size_t us = s.rfind('_');
            if (us == std::string::npos) continue;
            uint64_t id = 0;
            try {
              id = std::stoull(s.substr(us + 1));
            } catch (const std::exception &) {
              continue;
            }
            gpx::Node *sn = a.graph.find_node(id);
            const gpx::Heightmap *hm = sn ? sn->in_hmap("input") : nullptr;
            if (hm && !hm->empty()) field_tex.emplace_back(s, hm);
          }
        };

        auto compile_sink = [&](const char *type, const char *port,
                                const char *fn) -> std::string {
          gpx::Node *sink = nullptr;
          for (auto &cand : a.graph.nodes)
            if (cand->type == type && cand->enabled) sink = cand.get();
          if (!sink || !sink->attrs.get_b("live", true) ||
              !sink->field_connected(port))
            return {};
          gpx::Node *src = a.graph.upstream_node(*sink, port);
          const gpx::Port *sp = a.graph.upstream(*sink, port);
          if (!src) return {};
          gpx::GlslProgram prog =
              gpx::field_to_glsl(*src, sp ? sp->name : "", fn);
          if (!prog.ok) {
            sink->error = prog.error;
            return {};
          }
          // A graph reading a buffer needs its textures bound. There are only
          // so many units to spare, so say plainly when a graph asks for more
          // rather than binding some and sampling black for the rest.
          if (prog.samplers.size() > 4) {
            sink->error = "too many sampled buffers in one graph (max 4)";
            return {};
          }
          // The procedural surfaces are unbounded and have no heightmap, so
          // there is nothing a Sample node could read there. Refusing is
          // honest; binding an empty texture and shading black is not.
          if (std::string(type) == "SurfaceDisplacement" &&
              !prog.samplers.empty()) {
            sink->error = "planets and the infinite ground plane cannot read\n"
                          "buffers - they have no heightmap. Use field nodes\n"
                          "only, or Rasterize into the terrain tile instead.";
            return {};
          }
          collect_samplers(prog);
          sink->error.clear();
          if (std::string(type) == "TerrainDisplacement")
            render_settings().field_displacement =
                sink->attrs.get_f("strength", 0.05f);
          return prog.code;
        };
        renderer_set_field_program(
            compile_sink("TerrainDisplacement", "field", "gpx_terrain_field"),
            a.eval_serial);
        renderer_set_surface_program(
            compile_sink("TerrainSurface", "color", "gpx_terrain_surface"),
            compile_sink("TerrainSurface", "roughness", "gpx_terrain_rough"),
            compile_sink("TerrainSurface", "bump", "gpx_terrain_bump"),
            a.eval_serial);
        // The same bridge, one domain out: this one shapes every surface that
        // has no heightmap to bake into - planets and the endless ground
        // plane. See engine/nodes/nodes_displace.cpp, SurfaceDisplacement.
        {
          std::string surf =
              compile_sink("SurfaceDisplacement", "field", "gpx_surface_field");
          float strength = 0.f;
          for (auto &cand : a.graph.nodes)
            if (cand->type == "SurfaceDisplacement" && cand->enabled)
              strength = cand->attrs.get_f("strength", 1.f);
          planet_set_field_program(surf, strength);
        }
        for (auto &cand : a.graph.nodes)
          if (cand->type == "TerrainSurface" && cand->enabled)
            renderer_set_surface_bump(cand->attrs.get_f("bump_strength", 1.f),
                                      cand->attrs.get_f("bump_scale", 0.004f));
        renderer_set_field_textures(field_tex, a.eval_serial);
      }
      uint64_t view = a.view_node ? a.view_node : a.selected_node;
      gpx::Node *n = a.graph.find_node(view);
      // a TerrainOutput node is the canonical final terrain — prefer it
      // unless the user explicitly pinned another node
      if (!a.view_node) {
        for (auto &cand : a.graph.nodes)
          if (cand->type == "TerrainOutput") n = cand.get();
      }
      if (!n) {
        // fall back to last node with a heightmap output
        for (auto &cand : a.graph.nodes)
          if (cand->first_out(gpx::DataType::Heightmap)) n = cand.get();
      }
      if (n) {
        gpx::Port *ph = n->first_out(gpx::DataType::Heightmap);
        gpx::Port *pt = n->first_out(gpx::DataType::Texture);
        // if the node has no texture out, look downstream? keep simple:
        // search graph for a TerrainTexture/Colorize whose input chain includes n
        const gpx::TextureRGBA *albedo = pt && pt->tex ? pt->tex.get() : nullptr;
        // material assignment: a MaterialOutput node assigned to the terrain
        // object wins over the older albedo-source modes
        RenderSettings &rs = render_settings();
        gpx::Node *mat_out = nullptr;
        for (const SceneObject &o : scene().objects)
          if (o.type == SceneObject::Terrain && o.material_node)
            mat_out = a.graph.find_node(o.material_node);
        if (mat_out && mat_out->type == "MaterialOutput") {
          const gpx::TextureRGBA *base = mat_out->in_tex("base color");
          albedo = (base && !base->empty()) ? base : nullptr;
          const gpx::AttrSet &at = mat_out->attrs;
          rs.mat_roughness = at.get_f("roughness", rs.mat_roughness);
          rs.mat_metallic = at.get_f("metallic", rs.mat_metallic);
          rs.mat_specular = at.get_f("specular", rs.mat_specular);
          rs.mat_reflection = at.get_f("reflection", rs.mat_reflection);
          rs.mat_translucency = at.get_f("translucency", rs.mat_translucency);
          rs.mat_transparency = at.get_f("transparency", rs.mat_transparency);
          rs.mat_normal_strength = at.get_f("normal_strength", rs.mat_normal_strength);
          rs.mat_displacement = at.get_f("displacement", rs.mat_displacement);
        } else if (rs.terrain_material_mode == 1) {
          albedo = nullptr; // procedural in-shader material
        } else if (rs.terrain_material_mode == 2) {
          albedo = nullptr;
          if (gpx::Node *mn = a.graph.find_node(rs.terrain_material_node)) {
            gpx::Port *mp = mn->first_out(gpx::DataType::Texture);
            if (mp && mp->tex && !mp->tex->empty()) albedo = mp->tex.get();
          }
        } else if (!albedo) {
          // auto: the last composite albedo in the graph; skip data textures
          for (auto &cand : a.graph.nodes) {
            if (cand->type == "Splatmap" || cand->type == "NormalMap" ||
                cand->type == "AlbedoToPBR")
              continue;
            gpx::Port *cpt = cand->first_out(gpx::DataType::Texture);
            if (cpt && cpt->tex && !cpt->tex->empty()) albedo = cpt->tex.get();
          }
        }
        if (ph && ph->hmap && !ph->hmap->empty()) {
          renderer_set_terrain(*ph->hmap, albedo);
          g_overlay_terrain = ph->hmap; // for the points-overlay heights
        }
        else if (pt && pt->tex) {
          // texture-only node: keep last heightmap, update albedo
        }
      }
      a.uploaded_serial = a.eval_serial;
    }

    // points overlay: whenever the selection or the evaluation moves, hand
    // the renderer the selected node's point cloud (if it has one) with
    // heights sampled from the current terrain
    {
      static uint64_t last_sel = ~0ull, last_ser = ~0ull;
      if (last_sel != a.selected_node || last_ser != a.eval_serial) {
        last_sel = a.selected_node;
        last_ser = a.eval_serial;
        std::vector<float> xyz;
        gpx::Node *sn = a.graph.find_node(a.selected_node);
        const gpx::PointCloud *pc = nullptr;
        if (sn)
          for (const gpx::Port &p : sn->ports)
            if (p.dir == gpx::PortDir::Out &&
                p.type == gpx::DataType::Points && p.pts && p.pts->size()) {
              pc = p.pts.get();
              break;
            }
        if (pc) {
          const gpx::Heightmap *hm =
              g_overlay_terrain && !g_overlay_terrain->empty()
                  ? g_overlay_terrain.get()
                  : nullptr;
          float hs = render_settings().height_scale;
          size_t count = std::min(pc->size(), (size_t)20000);
          xyz.reserve(count * 3);
          for (size_t i = 0; i < count; ++i) {
            float hx = pc->x[i], hz = pc->y[i], hy = 0.f;
            if (hm) {
              int ix = std::clamp((int)(hx * hm->w), 0, hm->w - 1);
              int iy = std::clamp((int)(hz * hm->h), 0, hm->h - 1);
              hy = hm->v[(size_t)iy * hm->w + ix] * hs;
            }
            xyz.push_back(hx);
            xyz.push_back(hy);
            xyz.push_back(hz);
          }
        }
        renderer_set_points_overlay(xyz);
      }
    }

    // scatter instances: every Mesh object bound to a Points node gets its
    // copy list rebuilt when the evaluation moves
    {
      static uint64_t last_inst_ser = ~0ull;
      if (last_inst_ser != a.eval_serial) {
        last_inst_ser = a.eval_serial;
        scene_rebuild_scatter_instances(a);
      }
    }

    ImGui::Render();
    int dw, dh;
    glfwGetFramebufferSize(a.window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.09f, 0.09f, 0.10f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      GLFWwindow *backup = glfwGetCurrentContext();
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      glfwMakeContextCurrent(backup);
    }
    glfwSwapBuffers(a.window);
  }

  a.graph.cancel.store(true);
  a.eval.request.store(false);
  if (a.eval.worker.joinable()) a.eval.worker.join();
  previews_clear();
  renderer_shutdown();
  autosave_session_end(); // an orderly exit leaves no lock behind
}

} // namespace studio





