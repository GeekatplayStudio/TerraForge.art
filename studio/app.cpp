// Geekatplay Studio — main loop, docking layout, background evaluation
#include "app.hpp"
#include "console.hpp"
#include "autosave.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "planet_place.hpp"
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

// ---------------------------------------------------------- placement
// The tile the graph last produced and the albedo that went with it, so a
// change to the planet's layers or the placement settings can re-place it
// without re-evaluating the graph.
static std::shared_ptr<gpx::Heightmap> g_last_tile;
static const gpx::TextureRGBA *g_last_albedo = nullptr;
static uint64_t g_place_key = 0;

static uint64_t placement_key() {
  const RenderSettings &rs = render_settings();
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  };
  for (const gpx::planet::Layer &L : planet_home_layers()) mix(&L, sizeof L);
  PlaceSettings ps{rs.place_on_planet, rs.place_edge, rs.place_flatten,
                   rs.place_presence, rs.place_ground};
  mix(&ps, sizeof ps);
  return h;
}

// Composite the tile onto the planet and hand the result to the renderer.
// Everything downstream - picking, shadows, culling bounds, overlays, the
// surround's base level - sees the placed map, so what you click is what you
// see. Caller holds graph_mtx.
static void upload_placed_terrain(App &a, const std::shared_ptr<gpx::Heightmap> &hm,
                                  const gpx::TextureRGBA *albedo) {
  (void)a;
  const RenderSettings &rs = render_settings();
  PlaceSettings ps{rs.place_on_planet, rs.place_edge, rs.place_flatten,
                   rs.place_presence, rs.place_ground};
  PlaceResult pr;
  auto placed = std::make_shared<gpx::Heightmap>(
      planet_place_tile(*hm, planet_home_layers(), ps, &pr));
  planet_place_set_last(pr);
  renderer_set_terrain_base(pr.ground);
  renderer_set_terrain(*placed, albedo);
  app_set_overlay_terrain(placed);
  g_place_key = placement_key();
}



// studio/layout.cpp
void build_default_layout(ImGuiID dockspace_id, int view_count);

// studio/app_eval.cpp
void eval_worker(App &a);


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
  double frame_t0 = glfwGetTime();
  double last_activity = frame_t0;
  while (!glfwWindowShouldClose(a.window)) {
    // Frame pacing. Vsync already caps at the display rate; below that the
    // viewport rate preference decides, and when nothing at all is going on
    // the loop sleeps to the idle rate, waking on the first event. That is
    // what stops six views, a live preview and the node editor from holding
    // the GPU at 100 % while the user is thinking.
    {
      const ImGuiIO &io = ImGui::GetIO();
      double now = glfwGetTime();
      bool active = a.eval.running.load() || a.eval.request.load() ||
                    a.anim_playing || a.seq_active ||
                    io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f ||
                    io.MouseWheel != 0.f || ImGui::IsAnyMouseDown() ||
                    ImGui::IsAnyItemActive() || io.InputQueueCharacters.Size > 0 ||
                    a.request_camera_render >= 0 || a.eval_serial != a.uploaded_serial;
      for (int k = (int)ImGuiKey_NamedKey_BEGIN; !active && k < (int)ImGuiKey_NamedKey_END; ++k)
        if (ImGui::IsKeyDown((ImGuiKey)k)) active = true;
      if (active) last_activity = now;
      bool idle = now - last_activity > 0.4;
      int fps = idle ? std::max(prefs().idle_fps, 1) : std::max(prefs().viewport_fps, 5);
      double period = 1.0 / fps;
      double remain = period - (now - frame_t0);
      if (remain > 0.001) {
        if (idle) glfwWaitEventsTimeout(remain); // any event wakes the loop
        else std::this_thread::sleep_for(std::chrono::duration<double>(remain));
      }
      frame_t0 = glfwGetTime();
    }
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
    ImGuiID dockspace_id = ImGui::GetID("GeekatplayDockspaceV8");
    if (first_frame || a.request_layout_reset) {
      if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr ||
          a.request_layout_reset)
        build_default_layout(dockspace_id, prefs().view_count);
      a.request_layout_reset = false;
      first_frame = false;
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    a.dockspace_id = dockspace_id;
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
    draw_panel_timeline(a);
    if (a.show_properties) draw_panel_properties(a);
    draw_panel_material_editor(a);
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
    draw_panel_preview(a);
    app_service_sequence(a);

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

        auto compile_node = [&](gpx::Node *sink, const char *type,
                                const char *port, const char *fn) -> std::string {
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
        // the last enabled sink of a type is the one that counts
        auto compile_sink = [&](const char *type, const char *port,
                                const char *fn) -> std::string {
          gpx::Node *sink = nullptr;
          for (auto &cand : a.graph.nodes)
            if (cand->type == type && cand->enabled) sink = cand.get();
          return compile_node(sink, type, port, fn);
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
        // Every SurfaceDisplacement node is its own program: a planet names
        // the one that shapes it, the way a Terragen planet owns its terrain
        // network, and several worlds can each carry a different graph.
        {
          std::vector<unsigned long long> live;
          for (auto &cand : a.graph.nodes) {
            if (cand->type != "SurfaceDisplacement") continue;
            live.push_back(cand->id);
            std::string surf = cand->enabled
                                   ? compile_node(cand.get(), "SurfaceDisplacement",
                                                  "field", "gpx_surface_field")
                                   : std::string();
            planet_set_field_program(cand->id, surf,
                                     cand->attrs.get_f("strength", 1.f));
          }
          planet_field_programs_keep(live);
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
          g_last_tile = ph->hmap;
          g_last_albedo = albedo;
          upload_placed_terrain(a, ph->hmap, albedo);
        }
        else if (pt && pt->tex) {
          // texture-only node: keep last heightmap, update albedo
        }
      }
      a.uploaded_serial = a.eval_serial;
    }
    // The placement also changes when the planet's layers or the placement
    // settings do, with no evaluation in between; re-place the last tile.
    {
      uint64_t key = placement_key();
      if (key != g_place_key && g_last_tile && a.uploaded_serial == a.eval_serial &&
          !a.eval.running.load()) {
        std::lock_guard<std::mutex> lk(a.graph_mtx);
        g_place_key = key;
        upload_placed_terrain(a, g_last_tile, g_last_albedo);
      }
    }

    app_service_camera_anim(a);
    app_service_points_overlay(a);

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

  // Every step of the way out is logged: the crash reports on record all
  // came from abort() with no message, and most of them at closing time.
  log_info("app", "shutdown: window closed, stopping the evaluation worker");
  a.graph.cancel.store(true);
  a.eval.request.store(false);
  if (a.eval.worker.joinable()) a.eval.worker.join();
  log_info("app", "shutdown: worker joined, clearing previews");
  previews_clear();
  log_info("app", "shutdown: renderer");
  renderer_shutdown();
  autosave_session_end(); // an orderly exit leaves no lock behind
  log_info("app", "shutdown: session ended");
}

} // namespace studio





