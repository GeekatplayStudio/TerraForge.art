// Geekatplay TerraForge — field-domain colour nodes: HSV in both directions
// and a colour-correction stage. These are what a material graph reaches for
// to vary a photoscanned texture across a mountainside (hue drift by
// altitude, desaturation toward the horizon) without a second texture set.
//
// Colour maths is shared with the GLSL prelude through gpx/color_math.hpp:
// the CPU functions and gpxf_rgb2hsv / gpxf_hsv2rgb are the same algorithm,
// statement for statement.
#include "gpx/color_math.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

REGISTER_NODE(
    FieldColorHSV, "Field Color",
    "Takes a colour apart as hue, saturation and value (each 0..1)",
    [](Node &n) {
      n.add_field_in("color", FieldType::Color, true);
      auto lane = [](int k) {
        return [k](const Node &self, const FieldContext &ctx) {
          float c[4], hsv[3];
          self.in_field("color", ctx, FieldValue::color(0, 0, 0, 1)).as_color(c);
          rgb_to_hsv(c, hsv);
          return FieldValue(hsv[k]);
        };
      };
      n.add_field_out("h", FieldType::Number, lane(0));
      n.add_field_out("s", FieldType::Number, lane(1));
      n.add_field_out("v", FieldType::Number, lane(2));
    },
    [](Node &) {})

REGISTER_NODE(
    FieldColorFromHSV, "Field Color",
    "Builds a colour from hue, saturation, value and alpha numbers",
    [](Node &n) {
      n.add_field_in("h", FieldType::Number, true);
      n.add_field_in("s", FieldType::Number, true);
      n.add_field_in("v", FieldType::Number, true);
      n.add_field_in("a", FieldType::Number, true);
      add_float(n.attrs, "h", "Hue (when unconnected)", 0.1f, 0.f, 1.f);
      add_float(n.attrs, "s", "Saturation (when unconnected)", 0.4f, 0.f, 1.f);
      add_float(n.attrs, "v", "Value (when unconnected)", 0.6f, 0.f, 1.f);
      add_float(n.attrs, "a", "Alpha (when unconnected)", 1.f, 0.f, 1.f);
      n.add_field_out("out", FieldType::Color, [](const Node &self,
                                                  const FieldContext &ctx) {
        float hsv[3] = {self.in_number("h", ctx, self.attrs.get_f("h", 0.1f)),
                        self.in_number("s", ctx, self.attrs.get_f("s", 0.4f)),
                        self.in_number("v", ctx, self.attrs.get_f("v", 0.6f))};
        float c[3];
        hsv_to_rgb(hsv, c);
        return FieldValue::color(c[0], c[1], c[2],
                                 self.in_number("a", ctx, self.attrs.get_f("a", 1.f)));
      });
    },
    [](Node &) {})

// Order of operations, shared with the emitter: hue shift, saturation,
// contrast, brightness, gamma, invert. Alpha passes through untouched.
REGISTER_NODE(
    FieldColorAdjust, "Field Color",
    "Colour correction: hue shift, saturation, contrast, brightness, gamma, invert",
    [](Node &n) {
      n.add_field_in("color", FieldType::Color, true);
      add_float(n.attrs, "hue", "Hue shift °", 0.f, -180.f, 180.f);
      add_float(n.attrs, "saturation", "Saturation", 1.f, 0.f, 3.f);
      add_float(n.attrs, "contrast", "Contrast", 1.f, 0.f, 3.f);
      add_float(n.attrs, "brightness", "Brightness", 0.f, -1.f, 1.f);
      add_float(n.attrs, "gamma", "Gamma", 1.f, 0.1f, 5.f);
      add_bool(n.attrs, "invert", "Invert", false);
      n.add_field_out("out", FieldType::Color, [](const Node &self,
                                                  const FieldContext &ctx) {
        float c[4];
        self.in_field("color", ctx, FieldValue::color(0.5f, 0.5f, 0.5f, 1)).as_color(c);
        float hue = self.attrs.get_f("hue", 0.f);
        if (hue != 0.f) {
          float hsv[3];
          rgb_to_hsv(c, hsv);
          hsv[0] += hue / 360.f;
          hsv_to_rgb(hsv, c);
        }
        float sat = self.attrs.get_f("saturation", 1.f);
        float lum = luminance_rgb(c);
        float con = self.attrs.get_f("contrast", 1.f);
        float bri = self.attrs.get_f("brightness", 0.f);
        float inv_gamma = 1.f / std::max(self.attrs.get_f("gamma", 1.f), 1e-3f);
        bool inv = self.attrs.get_b("invert");
        for (int i = 0; i < 3; ++i) {
          float x = lum + (c[i] - lum) * sat;
          x = (x - 0.5f) * con + 0.5f + bri;
          x = std::pow(std::max(x, 0.f), inv_gamma);
          c[i] = inv ? 1.f - x : x;
        }
        return FieldValue::color(c[0], c[1], c[2], c[3]);
      });
    },
    [](Node &) {})

} // namespace gpx
