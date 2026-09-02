// Geekatplay TerraForge — analytic shape functions in the field domain.
//
// The primitives every catalogue carries and ours did not: waves, bands,
// bumps, cones, steps. One node with a waveform choice rather than eight
// nodes with one waveform each — the parameters are the same, and a choice
// is searchable where eight names are guessable.
//
// The CPU evaluation and the GLSL emitter live side by side in this file, on
// purpose: they are two spellings of one function, and the CPU/GPU agreement
// check holds them together. The emitter registers itself with the
// transpiler's registry at static-initialisation time; the registry is a
// function-local static, so the order is safe.
#include "../field_glsl_internal.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

constexpr float TAU = 6.2831853071795865f;

// The shared geometry: project the field position onto a direction in the
// ground plane (u) and measure the distance from a centre (r). Every mode is
// a function of one of those two numbers.
struct ShapeFrame {
  float u, r;
};

ShapeFrame shape_frame(const Node &self, const float p[3]) {
  float cx, cy;
  {
    float v2[2];
    const Attribute *c = self.attrs.find("center");
    v2[0] = c ? c->v2[0] : 0.5f;
    v2[1] = c ? c->v2[1] : 0.5f;
    cx = v2[0];
    cy = v2[1];
  }
  const float ang = self.attrs.get_f("direction", 0.f) * (TAU / 360.f);
  const float dx = p[0] - cx, dz = p[2] - cy;
  ShapeFrame f;
  f.u = dx * std::cos(ang) + dz * std::sin(ang);
  f.r = std::sqrt(dx * dx + dz * dz);
  return f;
}

float shape_value(int mode, float u, float r, float freq, float width,
                  float phase) {
  const float w = std::max(width, 1e-6f);
  switch (mode) {
    case 0: // sine wave
      return 0.5f + 0.5f * std::sin((u * freq + phase) * TAU);
    case 1: { // square wave
      float t = u * freq + phase;
      return (t - std::floor(t)) < 0.5f ? 1.f : 0.f;
    }
    case 2: { // triangle wave
      float t = u * freq + phase;
      return 1.f - std::fabs((t - std::floor(t)) * 2.f - 1.f);
    }
    case 3: { // sawtooth
      float t = u * freq + phase;
      return t - std::floor(t);
    }
    case 4: // gaussian bump about the centre
      return std::exp(-(r * r) / (w * w) * 2.f);
    case 5: // cone about the centre
      return std::max(0.f, 1.f - r / w);
    case 6: { // band across the direction
      float t = std::fabs(u) / (w * 0.5f);
      return t >= 1.f ? 0.f : 1.f - t * t * (3.f - 2.f * t);
    }
    default: // step: one side of the line
      return u >= 0.f ? 1.f : 0.f;
  }
}

} // namespace

REGISTER_NODE(
    FieldShape, "Field Noise",
    "Analytic shapes - waves, bands, bumps, cones and steps, as a function",
    [](Node &n) {
      n.add_field_in("position", FieldType::Vector, true);
      add_choice(n.attrs, "mode", "Shape",
                 {"Sine wave", "Square wave", "Triangle wave", "Sawtooth",
                  "Gaussian bump", "Cone", "Band", "Step"},
                 0);
      add_vec2(n.attrs, "center", "Center", 0.5f, 0.5f, -4.f, 4.f, "Placement");
      add_float(n.attrs, "direction", "Direction", 0.f, -180.f, 180.f,
                "Placement")
          .tooltip = "Which way the waves run, the band lies, or the step\n"
                     "faces, in degrees on the ground plane.";
      add_float(n.attrs, "frequency", "Frequency", 4.f, 0.01f, 200.f, "Shape")
          .tooltip = "Wave repetitions per unit of ground. Waves only.";
      add_float(n.attrs, "width", "Width", 0.25f, 0.001f, 8.f, "Shape")
          .tooltip = "Radius of the bump or cone; thickness of the band.";
      add_float(n.attrs, "phase", "Phase", 0.f, -2.f, 2.f, "Shape");
      add_float(n.attrs, "amplitude", "Amplitude", 1.f, 0.f, 8.f, "Output");
      add_float(n.attrs, "offset", "Offset", 0.f, -4.f, 4.f, "Output");
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float p[3];
        self.in_field("position", ctx,
                      FieldValue::vector(ctx.pos[0], ctx.pos[1], ctx.pos[2]))
            .as_vector(p);
        ShapeFrame f = shape_frame(self, p);
        float v = shape_value(self.attrs.get_choice("mode"), f.u, f.r,
                              self.attrs.get_f("frequency", 4.f),
                              self.attrs.get_f("width", 0.25f),
                              self.attrs.get_f("phase", 0.f));
        return FieldValue(v * self.attrs.get_f("amplitude", 1.f) +
                          self.attrs.get_f("offset", 0.f));
      });
    },
    [](Node &) {})

// --------------------------------------------------------------- the mirror
// Emits only the chosen mode's formula, spelt to match shape_value exactly.
namespace {

struct ShapeEmitterRegistrar {
  ShapeEmitterRegistrar() {
    glslgen::reg("FieldShape", [](const Node &n, const glslgen::InputFn &in,
                                  glslgen::EmitCtx &ctx) {
      using glslgen::f2s;
      std::string p = in("#position", ctx.pos.c_str());
      float cx = 0.5f, cy = 0.5f;
      if (const Attribute *c = n.attrs.find("center")) {
        cx = c->v2[0];
        cy = c->v2[1];
      }
      const float ang = n.attrs.get_f("direction", 0.f) * (TAU / 360.f);
      const float freq = n.attrs.get_f("frequency", 4.f);
      const float width = std::max(n.attrs.get_f("width", 0.25f), 1e-6f);
      const float phase = n.attrs.get_f("phase", 0.f);
      std::string dx = ctx.declare("float", "sdx",
                                   "(" + p + ").x - " + f2s(cx));
      std::string dz = ctx.declare("float", "sdz",
                                   "(" + p + ").z - " + f2s(cy));
      std::string u = ctx.declare(
          "float", "su",
          dx + " * " + f2s(std::cos(ang)) + " + " + dz + " * " +
              f2s(std::sin(ang)));
      std::string e;
      switch (n.attrs.get_choice("mode")) {
        case 0:
          e = "0.5 + 0.5 * sin((" + u + " * " + f2s(freq) + " + " +
              f2s(phase) + ") * 6.2831853)";
          break;
        case 1:
          e = "(fract(" + u + " * " + f2s(freq) + " + " + f2s(phase) +
              ") < 0.5 ? 1.0 : 0.0)";
          break;
        case 2:
          e = "(1.0 - abs(fract(" + u + " * " + f2s(freq) + " + " +
              f2s(phase) + ") * 2.0 - 1.0))";
          break;
        case 3:
          e = "fract(" + u + " * " + f2s(freq) + " + " + f2s(phase) + ")";
          break;
        case 4: {
          std::string r2 = dx + "*" + dx + " + " + dz + "*" + dz;
          e = "exp(-(" + r2 + ") / " + f2s(width * width) + " * 2.0)";
          break;
        }
        case 5: {
          std::string r = "sqrt(" + dx + "*" + dx + " + " + dz + "*" + dz + ")";
          e = "max(0.0, 1.0 - " + r + " / " + f2s(width) + ")";
          break;
        }
        case 6: {
          std::string t = ctx.declare(
              "float", "sbt", "abs(" + u + ") / " + f2s(width * 0.5f));
          e = "(" + t + " >= 1.0 ? 0.0 : 1.0 - " + t + "*" + t +
              " * (3.0 - 2.0*" + t + "))";
          break;
        }
        default:
          e = "(" + u + " >= 0.0 ? 1.0 : 0.0)";
          break;
      }
      e = "(" + e + " * " + f2s(n.attrs.get_f("amplitude", 1.f)) + " + " +
          f2s(n.attrs.get_f("offset", 0.f)) + ")";
      return "vec4(" + e + ", 0.0, 0.0, 1.0)";
    });
  }
};
const ShapeEmitterRegistrar reg_shape_emitter;

} // namespace

} // namespace gpx
