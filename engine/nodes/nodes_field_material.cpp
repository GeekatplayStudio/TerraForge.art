// Geekatplay TerraForge — field-domain material nodes (P2).
//
// Vue calls these environment-sensitive materials (manual p749) and they are
// the reason a procedural landscape reads as a landscape rather than as noise
// with a texture on it: rock where it is steep, snow above the treeline, grass
// in the flats, moss on the north face.
//
// The important word is *after*. These read the point's altitude, slope and
// orientation from the evaluation context, so if a FieldComputeNormal sits
// upstream they describe the displaced surface rather than the flat plane it
// started as. That ordering is what Terragen's Compute Terrain exists to
// guarantee, and it is the difference between snow lying on the mountains and
// snow lying on the plane the mountains were displaced out of.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {
namespace {

// Soft membership of a band, with a fade of `fuzz` on each side. Mirrored
// exactly by gpxf_band in the GLSL prelude — if you change one, change both.
//
// Deliberately not GLSL's smoothstep: smoothstep(e, e, x) is undefined when
// the edges coincide, which is exactly what a zero fuzziness asks for.
float band(float x, float lo, float hi, float fuzz) {
  if (fuzz <= 1e-6f) return (x >= lo && x <= hi) ? 1.f : 0.f;
  float a = std::clamp((x - (lo - fuzz)) / (2.f * fuzz), 0.f, 1.f);
  float b = std::clamp(((hi + fuzz) - x) / (2.f * fuzz), 0.f, 1.f);
  a = a * a * (3.f - 2.f * a);
  b = b * b * (3.f - 2.f * b);
  return std::min(a, b);
}

} // namespace

// ------------------------------------------------------------ distribution
REGISTER_NODE(
    FieldDistribution, "Field Material",
    "Where a material belongs: by altitude, steepness and which way the ground faces",
    [](Node &n) {
      // Each criterion can be driven by a field instead of the context, which
      // is how you feed it a computed slope from downstream of a displacement.
      n.add_field_in("altitude", FieldType::Number, true);
      n.add_field_in("slope", FieldType::Number, true);
      n.add_field_in("orientation", FieldType::Number, true);

      add_bool(n.attrs, "use_altitude", "By altitude", true, "Altitude");
      add_range(n.attrs, "altitude", "Altitude band", 0.f, 1.f, -100.f, 100.f,
                "Altitude");
      add_float(n.attrs, "altitude_fuzz", "Altitude fade", 0.1f, 0.f, 10.f,
                "Altitude")
          .tooltip = "How gradually the material gives out at the edges of the\n"
                     "band. Zero gives a hard line, which will look drawn on.";

      add_bool(n.attrs, "use_slope", "By steepness", false, "Steepness");
      add_range(n.attrs, "slope", "Slope band", 0.f, 1.f, -1.f, 1.f, "Steepness")
          .tooltip = "1 is flat ground, 0 is a vertical face. So rock wants a\n"
                     "low band and grass a high one.";
      add_float(n.attrs, "slope_fuzz", "Steepness fade", 0.1f, 0.f, 1.f,
                "Steepness");

      add_bool(n.attrs, "use_orientation", "By facing", false, "Facing");
      add_range(n.attrs, "orientation", "Facing band", -1.f, 1.f, -1.f, 1.f,
                "Facing")
          .tooltip = "Which compass direction the ground faces, as -1 to 1.\n"
                     "Snow lingers on one side of a ridge and not the other.";
      add_float(n.attrs, "orientation_fuzz", "Facing fade", 0.2f, 0.f, 1.f,
                "Facing");

      add_bool(n.attrs, "invert", "Invert", false);

      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float m = 1.f;
        // Criteria multiply: a material belongs where *every* enabled
        // condition holds, which is how a person describes it out loud.
        if (self.attrs.get_b("use_altitude", true)) {
          float lo, hi;
          self.attrs.get_range("altitude", lo, hi);
          m *= band(self.in_number("altitude", ctx, ctx.altitude), lo, hi,
                    self.attrs.get_f("altitude_fuzz", 0.1f));
        }
        if (self.attrs.get_b("use_slope")) {
          float lo, hi;
          self.attrs.get_range("slope", lo, hi);
          m *= band(self.in_number("slope", ctx, ctx.slope), lo, hi,
                    self.attrs.get_f("slope_fuzz", 0.1f));
        }
        if (self.attrs.get_b("use_orientation")) {
          float lo, hi;
          self.attrs.get_range("orientation", lo, hi);
          m *= band(self.in_number("orientation", ctx, ctx.orientation), lo, hi,
                    self.attrs.get_f("orientation_fuzz", 0.2f));
        }
        return FieldValue(self.attrs.get_b("invert") ? 1.f - m : m);
      });
    },
    [](Node &) {})

// -------------------------------------------------------------- colour mix
// FieldMix blends numbers. Materials need the same thing for colour, with the
// blend modes people actually reach for.
REGISTER_NODE(
    FieldColorMix, "Field Color",
    "Blends two colours — mix, add, multiply, screen, overlay, darken, lighten",
    [](Node &n) {
      n.add_field_in("a", FieldType::Color, true);
      n.add_field_in("b", FieldType::Color, true);
      n.add_field_in("factor", FieldType::Number, true);
      add_choice(n.attrs, "mode", "Blend mode",
                 {"Mix", "Add", "Multiply", "Screen", "Overlay", "Darken",
                  "Lighten"},
                 0);
      add_float(n.attrs, "amount", "Amount (when unconnected)", 0.5f, 0.f, 1.f);
      n.add_field_out("out", FieldType::Color, [](const Node &self,
                                                  const FieldContext &ctx) {
        float a[4], b[4];
        self.in_field("a", ctx, FieldValue::color(0, 0, 0)).as_color(a);
        self.in_field("b", ctx, FieldValue::color(1, 1, 1)).as_color(b);
        float t = std::clamp(
            self.in_number("factor", ctx, self.attrs.get_f("amount", 0.5f)),
            0.f, 1.f);
        int mode = self.attrs.get_choice("mode");
        float r[3];
        for (int i = 0; i < 3; ++i) {
          float x = a[i], y = b[i], v;
          switch (mode) {
            case 1: v = x + y; break;
            case 2: v = x * y; break;
            case 3: v = 1.f - (1.f - x) * (1.f - y); break;
            case 4:
              v = x < 0.5f ? 2.f * x * y : 1.f - 2.f * (1.f - x) * (1.f - y);
              break;
            case 5: v = std::min(x, y); break;
            case 6: v = std::max(x, y); break;
            default: v = y; break; // plain mix: the lerp below does the work
          }
          // every mode blends back toward A by the factor, so the factor
          // always means the same thing whichever mode is chosen
          r[i] = x + (v - x) * t;
        }
        return FieldValue::color(r[0], r[1], r[2], a[3] + (b[3] - a[3]) * t);
      });
    },
    [](Node &) {})

} // namespace gpx
