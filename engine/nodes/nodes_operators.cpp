// Geekatplay Studio — blend / math operators
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"

namespace gpx {

REGISTER_NODE(
    Blend, "Operator", "Blend two heightmaps (many modes)",
    [](Node &n) {
      n.add_in("input A");
      n.add_in("input B");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      add_choice(n.attrs, "mode", "Mode",
                 {"Mix", "Add", "Subtract", "Multiply", "Min", "Max", "Smooth min",
                  "Smooth max", "Overlay", "Screen", "Difference"},
                 0);
      add_float(n.attrs, "factor", "Factor", 0.5f, 0.f, 1.f);
      add_float(n.attrs, "smooth_k", "Smooth k", 0.1f, 0.01f, 0.5f);
    },
    [](Node &n) {
      const Heightmap *a = require_in(n, "input A");
      const Heightmap *b0 = require_in(n, "input B");
      if (!a || !b0) return;
      Heightmap bres;
      const Heightmap *b = b0;
      if (b0->w != a->w || b0->h != a->h) {
        bres = b0->resampled(a->w, a->h);
        b = &bres;
      }
      Heightmap &out = n.out_hmap("output");
      int mode = n.attrs.get_choice("mode");
      float f = n.attrs.get_f("factor", 0.5f);
      float k = n.attrs.get_f("smooth_k", 0.1f);
      const Heightmap *mask = n.in_hmap("mask");
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float va = a->v[i], vb = b->v[i], r = va;
          switch (mode) {
            case 0: r = va * (1 - f) + vb * f; break;
            case 1: r = va + vb * f; break;
            case 2: r = va - vb * f; break;
            case 3: r = va * (vb * f + (1 - f)); break;
            case 4: r = std::min(va, vb); break;
            case 5: r = std::max(va, vb); break;
            case 6: { // smooth min
              float d = std::clamp(0.5f + 0.5f * (vb - va) / k, 0.f, 1.f);
              r = vb + (va - vb) * d - k * d * (1 - d);
            } break;
            case 7: { // smooth max
              float d = std::clamp(0.5f - 0.5f * (vb - va) / k, 0.f, 1.f);
              r = vb + (va - vb) * d + k * d * (1 - d);
            } break;
            case 8: r = va < 0.5f ? 2 * va * vb : 1 - 2 * (1 - va) * (1 - vb); break;
            case 9: r = 1 - (1 - va) * (1 - vb); break;
            case 10: r = std::fabs(va - vb); break;
          }
          if (mask) {
            float m = std::clamp(mask->v[i], 0.f, 1.f);
            r = va * (1 - m) + r * m;
          }
          out.v[i] = r;
        }
      });
    })

REGISTER_NODE(
    Math, "Operator", "Per-pixel math on one input",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_choice(n.attrs, "op", "Operation",
                 {"Multiply", "Add", "Power", "Absolute", "Negate", "One minus",
                  "Square root", "Log1p", "Sine", "Smoothstep"},
                 0);
      add_float(n.attrs, "value", "Value", 1.f, -4.f, 4.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      int op = n.attrs.get_choice("op");
      float val = n.attrs.get_f("value", 1.f);
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float v = in->v[i];
          switch (op) {
            case 0: v *= val; break;
            case 1: v += val; break;
            case 2: v = std::pow(std::max(v, 0.f), val); break;
            case 3: v = std::fabs(v); break;
            case 4: v = -v; break;
            case 5: v = 1.f - v; break;
            case 6: v = std::sqrt(std::max(v, 0.f)); break;
            case 7: v = std::log1p(std::max(v, 0.f)); break;
            case 8: v = std::sin(v * 6.2831853f * val); break;
            case 9: {
              v = std::clamp(v, 0.f, 1.f);
              v = v * v * (3.f - 2.f * v);
            } break;
          }
          out.v[i] = v;
        }
      });
    })

REGISTER_NODE(
    MixLayers, "Operator", "Height-stack: stack up to 4 layers by max",
    [](Node &n) {
      n.add_in("layer 1");
      n.add_in("layer 2", DataType::Heightmap, true);
      n.add_in("layer 3", DataType::Heightmap, true);
      n.add_in("layer 4", DataType::Heightmap, true);
      n.add_out("output");
      add_float(n.attrs, "smooth_k", "Blend softness", 0.05f, 0.f, 0.4f);
    },
    [](Node &n) {
      const Heightmap *l1 = require_in(n, "layer 1");
      if (!l1) return;
      Heightmap &out = n.out_hmap("output");
      out = *l1;
      float k = n.attrs.get_f("smooth_k", 0.05f);
      for (const char *port : {"layer 2", "layer 3", "layer 4"}) {
        const Heightmap *l = n.in_hmap(port);
        if (!l || l->empty()) continue;
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i) {
            float a = out.v[i], b = l->v[i];
            if (k <= 1e-6f) {
              out.v[i] = std::max(a, b);
            } else {
              float d = std::clamp(0.5f - 0.5f * (b - a) / k, 0.f, 1.f);
              out.v[i] = b + (a - b) * d + k * d * (1 - d);
            }
          }
        });
      }
    })

} // namespace gpx
