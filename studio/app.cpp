// Geekatplay Studio — main loop, docking layout, background evaluation
#include "app.hpp"
#include "perf.hpp"
#include "ai_describe.hpp"
#include "ai_jobs.hpp"
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




// studio/layout_workspace.cpp
void build_workspace_default_layout(App &a, unsigned dockspace_id);

// studio/app_eval.cpp
void eval_worker(App &a);
void app_service_upload_shutdown();
void app_service_scatter(App &a);


void run_main() {
  // Before anything else can write to it: GLFW, the drivers and the shader
  // compilers all report through stderr, and this is a windowed process, so
  // without this those messages go nowhere at all.
  log_capture_stderr();
  log_info("app", "TerraForge starting");
  autosave_session_begin(); // detects whether the last one ended cleanly
  App &a = app();
  renderer_init();
  perf_init_gpu();
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
        else glfwWaitEventsTimeout(remain);
      }
      frame_t0 = glfwGetTime();
    }
    perf_frame_begin(); // the work, sleep excluded
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // A layout being loaded lands here: ImGui can only take a new set of
    // window positions between frames, before anything is submitted.
    if (!a.pending_layout_ini.empty()) {
      ImGui::LoadIniSettingsFromMemory(a.pending_layout_ini.c_str(),
                                       a.pending_layout_ini.size());
      a.pending_layout_ini.clear();
    }

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

    // Every path that changes the workspace - the bar, a node focus, a
    // script - lands here, so the layout follows the workspace no matter who
    // changed it.
    static int last_workspace = -1;
    if (last_workspace != a.workspace) {
      if (last_workspace >= 0) workspace_layout_switch(a, last_workspace, a.workspace);
      last_workspace = a.workspace;
    }

    // version bumped whenever the default layout changes shape
    ImGuiID dockspace_id = ImGui::GetID("GeekatplayDockspaceV8");
    if (first_frame || a.request_layout_reset) {
      if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr ||
          a.request_layout_reset)
        build_workspace_default_layout(a, dockspace_id);
      a.request_layout_reset = false;
      first_frame = false;
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0, -statusbar_height()), ImGuiDockNodeFlags_None);
    a.dockspace_id = dockspace_id;
    draw_statusbar(a); // health at a glance, along the bottom
    ImGui::End();

    // Keep the UI snapshot fresh whenever evaluation is not holding the lock -
    // but only when something could have changed it: an evaluation, a graph
    // edit, a pointer button down (a node being dragged), or a quarter second
    // gone by. Rebuilding every node view every frame was measurable on a
    // big graph and bought nothing on a still one.
    {
      static uint64_t snap_eval = ~0ull, snap_layout = ~0ull;
      static size_t snap_nodes = ~(size_t)0;
      static double snap_t = 0;
      double snow = glfwGetTime();
      bool changed = snap_eval != a.eval_serial || snap_layout != a.graph_layout_serial ||
                     snap_nodes != a.graph.nodes.size() ||
                     snow - snap_t > 0.25;
      if (changed) {
        std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
        if (lk.owns_lock()) {
          a.refresh_snapshot();
          snap_eval = a.eval_serial;
          snap_layout = a.graph_layout_serial;
          snap_nodes = a.graph.nodes.size();
          snap_t = snow;
        }
      }
    }
    perf_mark("host");
    if (a.show_library) draw_panel_library(a);
  if (a.show_nodelist) draw_panel_nodelist(a);
    perf_mark("panels.left");
    draw_panel_graph(a);
    perf_mark("graph");
    draw_console(a);
    draw_panel_timeline(a);
    if (a.show_properties) draw_panel_properties(a);
    draw_panel_material_editor(a);
    draw_panel_mesh(a);
    draw_panel_material_studio(a);
    draw_panel_material_browser(a);
    perf_mark("panels.mid");
    draw_panel_settings(a);
    draw_panel_ai_generate(a);
    draw_panel_ai_describe(a);
    ai_jobs_service(a);
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
    perf_mark("material.maps");
    draw_panel_ai(a);
    draw_panel_scene(a); // Outliner
    perf_mark("outliner");
    app_service_sequence(a);

    render_service_requests(a);
    draw_render_window(a);
    autosave_recovery_dialog(a); // offers the last session back after a crash
    autosave_tick(a, glfwGetTime());
    perf_mark("ui");
    studio_api_tick(a); // apply queued script/MCP actions, publish state
    perf_mark("api");

    app_service_upload(a);
    app_service_camera_anim(a);
    app_service_points_overlay(a);
    perf_mark("services");

    // scatter instances: every Mesh object bound to a Points node gets its
    // copy list rebuilt when the evaluation moves
    app_service_scatter(a);

    perf_mark("upload");
    // Apply completed terrain/scene updates before displaying either view.
    if (ImGui::IsAnyItemActive() || ImGui::IsMouseReleased(0) ||
        ImGui::IsMouseReleased(1) || ImGui::GetIO().MouseWheel != 0.f)
      renderer_invalidate_views();
    if (a.show_viewport) draw_panel_viewport(a);
    perf_mark("viewports");
    draw_panel_preview(a);
    perf_mark("preview");
    perf_governor_tick(a);
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
    perf_mark("ui.submit");
    perf_frame_end();
    glfwSwapBuffers(a.window);
  }

  // Every step of the way out is logged: the crash reports on record all
  // came from abort() with no message, and most of them at closing time.
  log_info("app", "shutdown: window closed, stopping the evaluation worker");
  a.graph.cancel.store(true);
  {
    std::lock_guard<std::mutex> lk(a.eval.wake_mtx);
    a.eval.stopping = true;
    a.eval.request.store(false);
  }
  a.eval.wake.notify_one();
  if (a.eval.worker.joinable()) a.eval.worker.join();
  app_service_upload_shutdown();
  log_info("app", "shutdown: worker joined, clearing previews");
  previews_clear();
  log_info("app", "shutdown: renderer");
  renderer_shutdown();
  autosave_session_end(); // an orderly exit leaves no lock behind
  log_info("app", "shutdown: session ended");
}

} // namespace studio
