// Geekatplay TerraForge — the UI snapshot of the node graph and the background
// evaluation worker. Split from app.cpp for the 500-line module rule.
#include "app.hpp"
#include "console.hpp"
#include "prefs.hpp"
#include "gpx/field.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace studio {

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
    v.collapse = n->ui_collapse;
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
      if (p.dir == gpx::PortDir::Out && p.has_stat) {
        char buf[48];
        if (p.pts) std::snprintf(buf, sizeof buf, "%d pts", p.stat_count);
        else if (p.tex) std::snprintf(buf, sizeof buf, "%dx%d", p.stat_count, p.stat_count);
        else std::snprintf(buf, sizeof buf, "%.2f..%.2f", p.stat_min, p.stat_max);
        pv.value = buf;
      } else if (p.dir == gpx::PortDir::Out && p.type == gpx::DataType::Field &&
                 p.field_eval) {
        // a field's value at the tile's centre: exact for constants, a
        // sample for everything else
        gpx::FieldContext c = gpx::FieldContext::at(0.5f, 0.f, 0.5f);
        gpx::FieldValue fv = p.field_eval(*n, c);
        char buf[48];
        if (fv.type == gpx::FieldType::Number)
          std::snprintf(buf, sizeof buf, "%.3g", fv.number());
        else
          std::snprintf(buf, sizeof buf, "%.2f %.2f %.2f", fv.v[0], fv.v[1], fv.v[2]);
        pv.value = buf;
      }
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

void eval_worker(App &a) {
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
      // Node compute is already caught per node inside evaluate(); this is
      // for everything around it (thread creation, allocation of the
      // scheduling tables). An exception leaving a std::thread body is
      // std::terminate, and the worker must outlive any one evaluation.
      try {
        a.graph.evaluate();
      } catch (const std::exception &e) {
        log_error("eval", std::string("evaluation aborted: ") + e.what());
      }
      a.graph.resolution = full;
      a.graph.on_progress = nullptr;
    }
    a.eval_serial++;
    log_fmt(LogLevel::Trace, "eval", "serial %llu done, %zu nodes",
            (unsigned long long)a.eval_serial, a.graph.nodes.size());
    a.eval.running.store(false);
  }
  log_info("eval", "worker thread leaving");
}

} // namespace studio
