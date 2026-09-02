// Geekatplay Studio - field arithmetic: math, trig, remap, curve, mix,
// vector ops. Split from nodes_field.cpp for the 500-line module rule.
// Geekatplay TerraForge — field domain nodes (P0.1).
//
// These are the resolution-independent half of the graph: each one answers
// "what is the value at this point?" rather than "fill this buffer". They are
// the building blocks Vue calls Function nodes and Terragen calls shaders, and
// they are what makes displacement, infinite terrain and per-point material
// control possible.
//
// Two rules every node here follows, because both are load-bearing:
//   * stateless — eval() reads only the node's attributes and the context, so
//     the same point always gives the same answer and evaluation is trivially
//     parallel and re-entrant.
//   * no resolution — nothing here may know how big a buffer is. The moment a
//     node needs neighbours or iteration it belongs in the raster domain.
//
// The 3D noise deliberately shares gpx::planet's implementation so a planet's
// surface and a graph-authored field agree by construction rather than by
// carefully keeping two copies in step.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/metanode.hpp"
#include "gpx/planet_math.hpp"
#include <json.hpp>
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>

namespace gpx {

// -------------------------------------------------------------------- math
REGISTER_NODE(
    FieldMath, "Field Math", "Combines two values: add, subtract, multiply, and the rest",
    [](Node &n) {
      n.add_field_in("a", FieldType::Number, true);
      n.add_field_in("b", FieldType::Number, true);
      add_choice(n.attrs, "op", "Operation",
                 {"Add", "Subtract", "Multiply", "Divide", "Minimum", "Maximum",
                  "Power", "Modulo", "Absolute difference"},
                 0);
      add_float(n.attrs, "a_default", "A (when unconnected)", 0.f, -100.f, 100.f,
                "Defaults");
      add_float(n.attrs, "b_default", "B (when unconnected)", 1.f, -100.f, 100.f,
                "Defaults");
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float a = self.in_number("a", ctx, self.attrs.get_f("a_default", 0.f));
        float b = self.in_number("b", ctx, self.attrs.get_f("b_default", 1.f));
        switch (self.attrs.get_choice("op")) {
          case 0: return FieldValue(a + b);
          case 1: return FieldValue(a - b);
          case 2: return FieldValue(a * b);
          // guarded so a zero divisor cannot poison the whole graph with NaN
          case 3: return FieldValue(std::fabs(b) > 1e-9f ? a / b : 0.f);
          case 4: return FieldValue(std::min(a, b));
          case 5: return FieldValue(std::max(a, b));
          case 6: return FieldValue(std::pow(std::max(a, 0.f), b));
          case 7: return FieldValue(std::fabs(b) > 1e-9f ? std::fmod(a, b) : 0.f);
          default: return FieldValue(std::fabs(a - b));
        }
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldTrig, "Field Math", "Trigonometry: sine, cosine, tangent and their inverses",
    [](Node &n) {
      n.add_field_in("in", FieldType::Number, true);
      add_choice(n.attrs, "fn", "Function",
                 {"Sine", "Cosine", "Tangent", "Arc sine", "Arc cosine",
                  "Arc tangent", "Hyperbolic sine", "Hyperbolic cosine",
                  "Hyperbolic tangent"},
                 0);
      add_bool(n.attrs, "degrees", "Work in degrees", false)
          .tooltip = "Interpret the input (and produce the output of the\n"
                     "inverse functions) in degrees rather than radians.";
      add_float(n.attrs, "scale", "Input scale", 1.f, -32.f, 32.f);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float x = self.in_number("in", ctx, 0.f) * self.attrs.get_f("scale", 1.f);
        bool deg = self.attrs.get_b("degrees");
        if (deg) x *= 0.017453293f;
        float r;
        switch (self.attrs.get_choice("fn")) {
          case 0: r = std::sin(x); break;
          case 1: r = std::cos(x); break;
          // tangent is unbounded at the poles; clamp so one bad point cannot
          // blow out everything downstream
          case 2: r = std::clamp(std::tan(x), -1e4f, 1e4f); break;
          case 3: r = std::asin(std::clamp(x, -1.f, 1.f)); break;
          case 4: r = std::acos(std::clamp(x, -1.f, 1.f)); break;
          case 5: r = std::atan(x); break;
          case 6: r = std::sinh(std::clamp(x, -20.f, 20.f)); break;
          case 7: r = std::cosh(std::clamp(x, -20.f, 20.f)); break;
          default: r = std::tanh(x); break;
        }
        // the inverse functions return an angle, so convert it back
        int fn = self.attrs.get_choice("fn");
        if (deg && fn >= 3 && fn <= 5) r *= 57.29578f;
        return FieldValue(r);
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldRemap, "Field Math", "Rescales a value from one range into another",
    [](Node &n) {
      n.add_field_in("in", FieldType::Number, true);
      add_range(n.attrs, "from", "Input range", -1.f, 1.f, -100.f, 100.f);
      add_range(n.attrs, "to", "Output range", 0.f, 1.f, -100.f, 100.f);
      add_bool(n.attrs, "clamp", "Clamp to the output range", true);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float x = self.in_number("in", ctx, 0.f);
        float f0, f1, t0, t1;
        self.attrs.get_range("from", f0, f1);
        self.attrs.get_range("to", t0, t1);
        float d = (f1 - f0);
        float t = std::fabs(d) > 1e-9f ? (x - f0) / d : 0.f;
        float r = t0 + t * (t1 - t0);
        if (self.attrs.get_b("clamp", true))
          r = std::clamp(r, std::min(t0, t1), std::max(t0, t1));
        return FieldValue(r);
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldCurve, "Field Math", "Shapes a value with a curve: gain, bias, step or smoothstep",
    [](Node &n) {
      n.add_field_in("in", FieldType::Number, true);
      add_choice(n.attrs, "shape", "Shape",
                 {"Gain (gamma)", "Smoothstep", "Step", "Bias", "Invert"}, 0);
      add_float(n.attrs, "amount", "Amount", 1.f, 0.05f, 8.f);
      add_range(n.attrs, "edges", "Edges", 0.f, 1.f, -4.f, 4.f);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float x = self.in_number("in", ctx, 0.f);
        float e0, e1;
        self.attrs.get_range("edges", e0, e1);
        float amt = self.attrs.get_f("amount", 1.f);
        switch (self.attrs.get_choice("shape")) {
          case 0: return FieldValue(std::pow(std::max(x, 0.f), amt));
          case 1: {
            float t = std::fabs(e1 - e0) > 1e-9f ? (x - e0) / (e1 - e0) : 0.f;
            t = std::clamp(t, 0.f, 1.f);
            return FieldValue(t * t * (3.f - 2.f * t));
          }
          case 2: return FieldValue(x >= e0 ? 1.f : 0.f);
          case 3: {
            // Schlick bias: pushes values toward one end without clipping
            float t = std::clamp(x, 0.f, 1.f);
            float b = std::clamp(amt / 8.f, 0.001f, 0.999f);
            return FieldValue(t / (((1.f / b) - 2.f) * (1.f - t) + 1.f));
          }
          default: return FieldValue(1.f - x);
        }
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldMix, "Field Math", "Blends between two inputs by a factor",
    [](Node &n) {
      n.add_field_in("a", FieldType::Number, true);
      n.add_field_in("b", FieldType::Number, true);
      n.add_field_in("factor", FieldType::Number, true);
      add_float(n.attrs, "amount", "Blend (when unconnected)", 0.5f, 0.f, 1.f);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float a = self.in_number("a", ctx, 0.f);
        float b = self.in_number("b", ctx, 1.f);
        float t = std::clamp(
            self.in_number("factor", ctx, self.attrs.get_f("amount", 0.5f)),
            0.f, 1.f);
        return FieldValue(a + (b - a) * t);
      });
    },
    [](Node &) {})

// ------------------------------------------------------------------ vector
REGISTER_NODE(
    FieldVectorOp, "Field Math", "Vector maths: length, dot, cross, normalize, distance",
    [](Node &n) {
      n.add_field_in("a", FieldType::Vector, true);
      n.add_field_in("b", FieldType::Vector, true);
      add_choice(n.attrs, "op", "Operation",
                 {"Length", "Dot product", "Distance", "Normalize (X)",
                  "Cross product (X)"},
                 0);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float a[3], b[3];
        self.in_field("a", ctx, FieldValue::vector(0, 0, 0)).as_vector(a);
        self.in_field("b", ctx, FieldValue::vector(0, 1, 0)).as_vector(b);
        auto len = [](const float *v) {
          return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        };
        switch (self.attrs.get_choice("op")) {
          case 0: return FieldValue(len(a));
          case 1: return FieldValue(a[0]*b[0] + a[1]*b[1] + a[2]*b[2]);
          case 2: {
            float d[3] = {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
            return FieldValue(len(d));
          }
          case 3: {
            float l = len(a);
            return FieldValue(l > 1e-9f ? a[0] / l : 0.f);
          }
          default: return FieldValue(a[1]*b[2] - a[2]*b[1]);
        }
      });
    },
    [](Node &) {})

} // namespace gpx
