// Geekatplay Studio — main loop, docking layout, background evaluation
#include "app.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

namespace studio {

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
      a.graph.evaluate();
      a.graph.resolution = full;
      a.graph.on_progress = nullptr;
    }
    a.eval_serial++;
    a.eval.running.store(false);
  }
}

static void build_default_layout(ImGuiID dockspace_id, int view_count) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

  ImGuiID main_id = dockspace_id;
  ImGuiID right = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, 0.24f,
                                              nullptr, &main_id);
  ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.68f,
                                                     nullptr, &right);
  ImGuiID left = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Left, 0.16f,
                                             nullptr, &main_id);
  ImGuiID top = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Up, 0.55f, nullptr,
                                            &main_id);
  ImGui::DockBuilderDockWindow("Library", left);
  ImGui::DockBuilderDockWindow("AI", left); // tabbed with Library
  ImGui::DockBuilderDockWindow("Graph", main_id);
  // Blender layout: Outliner on top, one Properties editor below it
  ImGui::DockBuilderDockWindow("Outliner", right);
  ImGui::DockBuilderDockWindow("Properties", right_bottom);

  // viewport region split into 1..6 independent view windows; each remains a
  // normal window the user can resize, re-dock or float
  view_count = std::clamp(view_count, 1, 6);
  ImGuiID cells[6];
  for (int i = 0; i < 6; ++i) cells[i] = top;
  switch (view_count) {
    case 1:
      break;
    case 2: {
      ImGuiID l = top;
      cells[1] = ImGui::DockBuilderSplitNode(l, ImGuiDir_Right, 0.5f, nullptr, &l);
      cells[0] = l;
    } break;
    case 3: {
      ImGuiID l = top;
      ImGuiID r = ImGui::DockBuilderSplitNode(l, ImGuiDir_Right, 0.5f, nullptr, &l);
      ImGuiID rb = ImGui::DockBuilderSplitNode(r, ImGuiDir_Down, 0.5f, nullptr, &r);
      cells[0] = l; cells[1] = r; cells[2] = rb;
    } break;
    case 4: {
      ImGuiID tl = top;
      ImGuiID tr = ImGui::DockBuilderSplitNode(tl, ImGuiDir_Right, 0.5f, nullptr, &tl);
      ImGuiID bl = ImGui::DockBuilderSplitNode(tl, ImGuiDir_Down, 0.5f, nullptr, &tl);
      ImGuiID br = ImGui::DockBuilderSplitNode(tr, ImGuiDir_Down, 0.5f, nullptr, &tr);
      cells[0] = tl; cells[1] = tr; cells[2] = bl; cells[3] = br;
    } break;
    case 5: {
      ImGuiID t = top;
      ImGuiID b = ImGui::DockBuilderSplitNode(t, ImGuiDir_Down, 0.5f, nullptr, &t);
      ImGuiID t2 = ImGui::DockBuilderSplitNode(t, ImGuiDir_Right, 0.5f, nullptr, &t);
      ImGuiID b2 = ImGui::DockBuilderSplitNode(b, ImGuiDir_Right, 0.66f, nullptr, &b);
      ImGuiID b3 = ImGui::DockBuilderSplitNode(b2, ImGuiDir_Right, 0.5f, nullptr, &b2);
      cells[0] = t; cells[1] = t2; cells[2] = b; cells[3] = b2; cells[4] = b3;
    } break;
    default: { // 6 = 3 columns x 2 rows
      ImGuiID t = top;
      ImGuiID b = ImGui::DockBuilderSplitNode(t, ImGuiDir_Down, 0.5f, nullptr, &t);
      ImGuiID t2 = ImGui::DockBuilderSplitNode(t, ImGuiDir_Right, 0.66f, nullptr, &t);
      ImGuiID t3 = ImGui::DockBuilderSplitNode(t2, ImGuiDir_Right, 0.5f, nullptr, &t2);
      ImGuiID b2 = ImGui::DockBuilderSplitNode(b, ImGuiDir_Right, 0.66f, nullptr, &b);
      ImGuiID b3 = ImGui::DockBuilderSplitNode(b2, ImGuiDir_Right, 0.5f, nullptr, &b2);
      cells[0] = t; cells[1] = t2; cells[2] = t3;
      cells[3] = b; cells[4] = b2; cells[5] = b3;
    } break;
  }
  for (int i = 0; i < view_count; ++i)
    ImGui::DockBuilderDockWindow(view_window_name(i), cells[i]);
  ImGui::DockBuilderFinish(dockspace_id);
}

void run_main() {
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

    draw_toolbar(a); // menu bar + tool strip

    // version bumped whenever the default layout changes shape
    ImGuiID dockspace_id = ImGui::GetID("GeekatplayDockspaceV6");
    if (first_frame || a.request_layout_reset) {
      if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr ||
          a.request_layout_reset)
        build_default_layout(dockspace_id, prefs().view_count);
      a.request_layout_reset = false;
      first_frame = false;
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();

    if (a.show_library) draw_panel_library(a);
    if (a.show_viewport) draw_panel_viewport(a);
    draw_panel_graph(a);
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
                                     chan("height"));
        } else {
          renderer_set_material_maps(tex_of(rs.map_normal_node),
                                     tex_of(rs.map_roughness_node),
                                     tex_of(rs.map_displacement_node));
        }
      }
    }
    draw_panel_ai(a);
    draw_panel_scene(a); // Outliner
    draw_render_window(a);

    // upload fresh eval results to GPU (main thread only)
    if (a.uploaded_serial != a.eval_serial && !a.eval.running.load()) {
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      // skip thumbnail regeneration during interactive drags — keeps the
      // slider->viewport loop as tight as possible
      if (!a.eval_interactive.load()) previews_update(a);
      // Terragen-style: atmosphere/render nodes drive the renderer
      apply_scene_nodes(a);
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
        if (ph && ph->hmap && !ph->hmap->empty())
          renderer_set_terrain(*ph->hmap, albedo);
        else if (pt && pt->tex) {
          // texture-only node: keep last heightmap, update albedo
        }
      }
      a.uploaded_serial = a.eval_serial;
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
}

} // namespace studio
