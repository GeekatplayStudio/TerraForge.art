// Geekatplay Studio — simple logic / routing nodes
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"

namespace gpx {

REGISTER_NODE(
    Threshold, "Logic", "Binary/soft threshold to a mask",
    [](Node &n) {
      n.add_in("input");
      n.add_out("mask");
      add_float(n.attrs, "level", "Level", 0.5f, 0.f, 1.f);
      add_float(n.attrs, "softness", "Softness", 0.05f, 0.f, 0.5f);
      add_bool(n.attrs, "invert", "Invert", false);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("mask");
      Heightmap norm = *in;
      norm.remap(0.f, 1.f);
      float level = n.attrs.get_f("level", 0.5f);
      float soft = std::max(n.attrs.get_f("softness", 0.05f), 1e-6f);
      bool inv = n.attrs.get_b("invert");
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float v = std::clamp((norm.v[i] - level) / soft + 0.5f, 0.f, 1.f);
          v = v * v * (3.f - 2.f * v);
          out.v[i] = inv ? 1.f - v : v;
        }
      });
    })

REGISTER_NODE(
    Compare, "Logic", "Compare two inputs into a mask",
    [](Node &n) {
      n.add_in("input A");
      n.add_in("input B");
      n.add_out("mask");
      add_choice(n.attrs, "op", "Operation",
                 {"A > B", "A < B", "|A - B| < tol", "|A - B| > tol"}, 0);
      add_float(n.attrs, "tolerance", "Tolerance", 0.05f, 0.f, 1.f);
      add_float(n.attrs, "softness", "Softness", 0.02f, 0.f, 0.3f);
    },
    [](Node &n) {
      const Heightmap *a = require_in(n, "input A");
      const Heightmap *b = require_in(n, "input B");
      if (!a || !b) return;
      Heightmap &out = n.out_hmap("mask");
      int op = n.attrs.get_choice("op");
      float tol = n.attrs.get_f("tolerance", 0.05f);
      float soft = std::max(n.attrs.get_f("softness", 0.02f), 1e-6f);
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float d = a->v[i] - b->v[i], v = 0;
          switch (op) {
            case 0: v = std::clamp(d / soft + 0.5f, 0.f, 1.f); break;
            case 1: v = std::clamp(-d / soft + 0.5f, 0.f, 1.f); break;
            case 2: v = std::clamp((tol - std::fabs(d)) / soft + 0.5f, 0.f, 1.f); break;
            case 3: v = std::clamp((std::fabs(d) - tol) / soft + 0.5f, 0.f, 1.f); break;
          }
          out.v[i] = v;
        }
      });
    })

REGISTER_NODE(
    Switch, "Logic", "Route input A or B to output",
    [](Node &n) {
      n.add_in("input A");
      n.add_in("input B", DataType::Heightmap, true);
      n.add_out("output");
      add_bool(n.attrs, "use_b", "Use input B", false);
    },
    [](Node &n) {
      const Heightmap *a = n.in_hmap("input A");
      const Heightmap *b = n.in_hmap("input B");
      const Heightmap *sel = (n.attrs.get_b("use_b") && b && !b->empty()) ? b : a;
      if (!sel || sel->empty()) {
        n.error = "no connected input to route";
        return;
      }
      n.out_hmap("output") = *sel;
    })

REGISTER_NODE(
    Thru, "Logic", "Pass-through / organization pin",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      n.out_hmap("output") = *in;
    })

REGISTER_NODE(
    Select, "Logic", "Select one of four inputs by index or selector map",
    [](Node &n) {
      n.add_in("input 1");
      n.add_in("input 2", DataType::Heightmap, true);
      n.add_in("input 3", DataType::Heightmap, true);
      n.add_in("input 4", DataType::Heightmap, true);
      n.add_in("selector", DataType::Heightmap, true);
      n.add_out("output");
      add_int(n.attrs, "index", "Index", 0, 0, 3);
      add_bool(n.attrs, "by_map", "Blend by selector map", false)
          .tooltip = "When on, the selector map (0..1) cross-fades\n"
                     "between the connected inputs instead of the index.";
    },
    [](Node &n) {
      const Heightmap *ins[4] = {n.in_hmap("input 1"), n.in_hmap("input 2"),
                                 n.in_hmap("input 3"), n.in_hmap("input 4")};
      std::vector<const Heightmap *> live;
      for (auto *p : ins)
        if (p && !p->empty()) live.push_back(p);
      if (live.empty()) {
        n.error = "no connected inputs";
        return;
      }
      Heightmap &out = n.out_hmap("output");
      const Heightmap *sel = n.in_hmap("selector");
      if (n.attrs.get_b("by_map") && sel && !sel->empty() && live.size() > 1) {
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i) {
            float t = std::clamp(sel->v[i], 0.f, 1.f) * (live.size() - 1);
            int a = (int)t;
            int b = std::min(a + 1, (int)live.size() - 1);
            float f = t - a;
            out.v[i] = live[a]->v[i] * (1 - f) + live[b]->v[i] * f;
          }
        });
      } else {
        int idx = std::clamp(n.attrs.get_i("index", 0), 0, 3);
        const Heightmap *pick = (ins[idx] && !ins[idx]->empty()) ? ins[idx] : live[0];
        out = *pick;
      }
    })

REGISTER_NODE(
    Repeat, "Logic", "Loop: apply an operation N times",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_choice(n.attrs, "op", "Operation",
                 {"Smooth", "Thermal step", "Expand", "Shrink", "Fold ridges"}, 0);
      add_int(n.attrs, "count", "Loop count", 4, 1, 64);
      add_float(n.attrs, "strength", "Strength per pass", 0.5f, 0.05f, 1.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      int op = n.attrs.get_choice("op");
      int count = n.attrs.get_i("count", 4);
      float strength = n.attrs.get_f("strength", 0.5f);
      Heightmap tmp(out.w, out.h);
      float mn, mx;
      out.minmax(mn, mx);
      float talus = 1.2f * (mx - mn) / out.w;
      for (int it = 0; it < count; ++it) {
        switch (op) {
          case 0: // box smooth
            tmp = out;
            parallel_rows(out.h, [&](int y0, int y1) {
              for (int y = y0; y < y1; ++y)
                for (int x = 0; x < out.w; ++x) {
                  float avg = (tmp.atc(x - 1, y) + tmp.atc(x + 1, y) +
                               tmp.atc(x, y - 1) + tmp.atc(x, y + 1)) * 0.25f;
                  out.at(x, y) += (avg - tmp.at(x, y)) * strength;
                }
            });
            break;
          case 1: // one thermal relaxation step
            tmp = out;
            parallel_rows(out.h, [&](int y0, int y1) {
              for (int y = y0; y < y1; ++y)
                for (int x = 0; x < out.w; ++x) {
                  float lo = tmp.at(x, y);
                  for (int k = 0; k < 4; ++k) {
                    static const int dx4[4] = {-1, 1, 0, 0}, dy4[4] = {0, 0, -1, 1};
                    lo = std::min(lo, tmp.atc(x + dx4[k], y + dy4[k]) + talus);
                  }
                  out.at(x, y) += (lo - tmp.at(x, y)) * strength;
                }
            });
            break;
          case 2:
          case 3: // expand / shrink
            tmp = out;
            parallel_rows(out.h, [&](int y0, int y1) {
              for (int y = y0; y < y1; ++y)
                for (int x = 0; x < out.w; ++x) {
                  float best = tmp.at(x, y);
                  for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                      float v = tmp.atc(x + dx, y + dy);
                      best = op == 2 ? std::max(best, v) : std::min(best, v);
                    }
                  out.at(x, y) += (best - tmp.at(x, y)) * strength;
                }
            });
            break;
          case 4: { // fold
            float m0, m1;
            out.minmax(m0, m1);
            float mid = 0.5f * (m0 + m1);
            parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
              for (size_t i = i0; i < i1; ++i) {
                float folded = m1 - std::fabs(out.v[i] - mid) * 2.f;
                out.v[i] += (folded - out.v[i]) * strength;
              }
            });
          } break;
        }
      }
    })

REGISTER_NODE(
    MathGradient, "Operator", "Derivatives: dx, dy, slope magnitude, laplacian",
    [](Node &n) {
      n.add_in("input");
      n.add_out("dx");
      n.add_out("dy");
      n.add_out("magnitude");
      n.add_out("laplacian");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &dx = n.out_hmap("dx");
      Heightmap &dy = n.out_hmap("dy");
      Heightmap &mag = n.out_hmap("magnitude");
      Heightmap &lap = n.out_hmap("laplacian");
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < in->w; ++x) {
            float gx, gy;
            in->gradient_at(x, y, gx, gy);
            dx.at(x, y) = gx;
            dy.at(x, y) = gy;
            mag.at(x, y) = std::sqrt(gx * gx + gy * gy);
            lap.at(x, y) = in->atc(x - 1, y) + in->atc(x + 1, y) +
                           in->atc(x, y - 1) + in->atc(x, y + 1) -
                           4.f * in->at(x, y);
          }
      });
      mag.remap(0.f, 1.f);
    })

} // namespace gpx
