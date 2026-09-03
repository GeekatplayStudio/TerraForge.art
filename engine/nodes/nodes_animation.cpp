// Geekatplay TerraForge — animation nodes (the Animation workspace).
//
// Animation tracks live on attributes (engine rule: no node knows animation
// exists), the timeline drives the graph clock, and FieldTime exposes that
// clock to field graphs. These nodes are the tools that shape time:
// Oscillator turns it into a wave, TimeRemap speeds, offsets and loops it,
// AnimationSequence declares the shot (frame range, rate, output) so the
// sequence renderer reads it from the graph. The field nodes have GLSL twins
// so an animated displacement or material runs on the GPU. The planned nodes
// keep Vue's animation chapter (P7, the largest missing pillar) in view.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <cmath>

namespace gpx {

namespace {
void planned(Node &n, const char *what, const char *phase) {
  add_text(n.attrs, "plan", "Planned", what, "Roadmap").tooltip =
      "This node is a placeholder: it documents a capability on the roadmap\n"
      "so the module is not forgotten. It has no effect on the scene yet.";
  add_text(n.attrs, "phase", "Roadmap phase", phase, "Roadmap");
}

// One period of each waveform over phase p in [0,1). Mirrored in
// field_glsl_emitters_anim (gpxf_wave).
float wave(int shape, float p) {
  p -= std::floor(p);
  switch (shape) {
    case 1: return 1.f - std::fabs(p * 4.f - 2.f);                // triangle -1..1
    case 2: return p < 0.5f ? 1.f : -1.f;                         // square
    case 3: return p * 2.f - 1.f;                                 // sawtooth
    default: return std::sin(p * 6.2831853f);                     // sine
  }
}
} // namespace

REGISTER_NODE(
    Oscillator, "Animation",
    "A wave of time: sine, triangle, square or sawtooth, for anything that should pulse",
    [](Node &n) {
      n.add_field_in("time", FieldType::Number, true);
      add_choice(n.attrs, "shape", "Waveform", {"Sine", "Triangle", "Square", "Sawtooth"},
                 0);
      add_float(n.attrs, "frequency", "Frequency (Hz)", 0.5f, 0.001f, 100.f, "", true);
      add_float(n.attrs, "phase", "Phase", 0.f, 0.f, 1.f);
      add_float(n.attrs, "amplitude", "Amplitude", 1.f, 0.f, 1000.f);
      add_float(n.attrs, "offset", "Offset", 0.f, -1000.f, 1000.f);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float t = self.in_number("time", ctx, ctx.time);
        float p = t * self.attrs.get_f("frequency", 0.5f) + self.attrs.get_f("phase", 0.f);
        return FieldValue(wave(self.attrs.get_choice("shape"), p) *
                              self.attrs.get_f("amplitude", 1.f) +
                          self.attrs.get_f("offset", 0.f));
      });
    },
    [](Node &) {})

REGISTER_NODE(
    TimeRemap, "Animation",
    "Speeds, offsets, loops or ping-pongs time before it reaches a graph",
    [](Node &n) {
      n.add_field_in("time", FieldType::Number, true);
      add_float(n.attrs, "speed", "Speed", 1.f, -100.f, 100.f);
      add_float(n.attrs, "offset", "Offset (s)", 0.f, -10000.f, 10000.f);
      add_float(n.attrs, "loop", "Loop length (s, 0 = none)", 0.f, 0.f, 10000.f);
      add_bool(n.attrs, "pingpong", "Ping-pong", false).tooltip =
          "With a loop length: run forward then backward instead of jumping.";
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float t = self.in_number("time", ctx, ctx.time) * self.attrs.get_f("speed", 1.f) +
                  self.attrs.get_f("offset", 0.f);
        float loop = self.attrs.get_f("loop", 0.f);
        if (loop > 1e-6f) {
          if (self.attrs.get_b("pingpong")) {
            float p = t / (2.f * loop);
            p -= std::floor(p);
            t = (p < 0.5f ? p * 2.f : 2.f - p * 2.f) * loop;
          } else {
            float p = t / loop;
            t = (p - std::floor(p)) * loop;
          }
        }
        return FieldValue(t);
      });
    },
    [](Node &) {})

REGISTER_NODE(
    AnimationSequence, "Animation",
    "The shot: frame range, frame rate, output size and folder for the sequence renderer",
    [](Node &n) {
      add_float(n.attrs, "start", "Start (s)", 0.f, 0.f, 100000.f, "Range");
      add_float(n.attrs, "end", "End (s)", 10.f, 0.f, 100000.f, "Range");
      add_float(n.attrs, "fps", "Frames per second", 30.f, 1.f, 240.f, "Range");
      add_int(n.attrs, "width", "Width", 1280, 64, 8192, "Output");
      add_int(n.attrs, "height", "Height", 720, 64, 8192, "Output");
      add_text(n.attrs, "dir", "Output folder", "sequence", "Output");
      add_bool(n.attrs, "sun_sweep", "Sweep the sun", false, "Day cycle");
      add_float(n.attrs, "sun_from_az", "Sun from: azimuth °", 90.f, 0.f, 360.f, "Day cycle");
      add_float(n.attrs, "sun_from_alt", "Sun from: altitude °", 10.f, -10.f, 90.f, "Day cycle");
      add_float(n.attrs, "sun_to_az", "Sun to: azimuth °", 270.f, 0.f, 360.f, "Day cycle");
      add_float(n.attrs, "sun_to_alt", "Sun to: altitude °", 10.f, -10.f, 90.f, "Day cycle");
    },
    [](Node &) {})

REGISTER_NODE(
    KeyframeCurve, "Animation",
    "[Planned] Editable time spline: keys, tangents, interpolation per segment",
    [](Node &n) {
      planned(n,
              "A curve of value over time with editable keys and tangents, as a\n"
              "node so one curve can drive several attributes and be reused.\n"
              "Keys today live on each attribute's own track.",
              "P7 Animation (Vue p1155-1159 time splines)");
    },
    [](Node &) {})

REGISTER_NODE(
    AnimationClip, "Animation",
    "[Planned] Reusable clip of keyed attributes: paste, shift, stretch, reverse",
    [](Node &n) {
      planned(n,
              "Clip editing: paste animation between objects, destroy, shift the\n"
              "start, change the duration, reverse.",
              "P7 Animation (Vue p1153-1155)");
    },
    [](Node &) {})

REGISTER_NODE(
    Dynamics, "Animation",
    "[Planned] Forward dynamics, linking and tracking between objects",
    [](Node &n) {
      planned(n,
              "Linked hierarchies, tracking, look-ahead and simple forward\n"
              "dynamics (gravity, wind on plants) evaluated per frame.",
              "P7 Animation (Vue p1164-1167)");
    },
    [](Node &) {})

} // namespace gpx
