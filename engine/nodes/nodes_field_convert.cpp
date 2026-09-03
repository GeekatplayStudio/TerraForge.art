// Geekatplay TerraForge — field-domain type converters.
//
// The field graph moves four value types around (number, colour, vector,
// texture coordinates) and every port is marked with the one it carries. A
// link between two different types still works — FieldValue converts on the
// way in, a colour read as a number is its luminance, a number read as a
// vector broadcasts — but that silent conversion is only right most of the
// time. These nodes make the conversion explicit and complete, the way Vue's
// converter nodes do: take a colour apart into its channels and alpha, build a
// vector from three numbers, choose which lane of a vector becomes a number.
//
// Every node here has a GLSL twin in field_glsl_emitters_convert.cpp that
// branches on the upstream type at compile time exactly as the CPU branches
// on FieldValue::type at run time. The conversion rules live in FieldValue
// (as_color / as_vector / as_texcoord / number); nothing here may invent a
// rule of its own without adding it there and in the transpiler.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

const char *CAT = "Field Convert";

// The lanes a value type actually carries, for the lane-picking converter.
int lane_count(FieldType t) {
  switch (t) {
    case FieldType::Number: return 1;
    case FieldType::TexCoord: return 2;
    default: return 3;
  }
}

} // namespace

// ------------------------------------------------------------ colour split
REGISTER_NODE(
    FieldColorSplit, "Field Convert",
    "Takes a colour apart: luminance, red, green, blue and alpha as numbers",
    [](Node &n) {
      n.add_field_in("color", FieldType::Color, true);
      auto lane = [](int k) {
        return [k](const Node &self, const FieldContext &ctx) {
          float c[4];
          self.in_field("color", ctx, FieldValue::color(0, 0, 0, 1)).as_color(c);
          if (k < 0)
            return FieldValue(0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]);
          return FieldValue(c[k]);
        };
      };
      n.add_field_out("luminance", FieldType::Number, lane(-1));
      n.add_field_out("r", FieldType::Number, lane(0));
      n.add_field_out("g", FieldType::Number, lane(1));
      n.add_field_out("b", FieldType::Number, lane(2));
      n.add_field_out("a", FieldType::Number, lane(3));
    },
    [](Node &) {})

REGISTER_NODE(
    FieldColorCombine, "Field Convert",
    "Builds a colour from red, green, blue and alpha numbers",
    [](Node &n) {
      n.add_field_in("r", FieldType::Number, true);
      n.add_field_in("g", FieldType::Number, true);
      n.add_field_in("b", FieldType::Number, true);
      n.add_field_in("a", FieldType::Number, true);
      add_float(n.attrs, "r", "Red (when unconnected)", 0.5f, 0.f, 1.f);
      add_float(n.attrs, "g", "Green (when unconnected)", 0.5f, 0.f, 1.f);
      add_float(n.attrs, "b", "Blue (when unconnected)", 0.5f, 0.f, 1.f);
      add_float(n.attrs, "a", "Alpha (when unconnected)", 1.f, 0.f, 1.f);
      n.add_field_out("out", FieldType::Color, [](const Node &self,
                                                  const FieldContext &ctx) {
        return FieldValue::color(
            self.in_number("r", ctx, self.attrs.get_f("r", 0.5f)),
            self.in_number("g", ctx, self.attrs.get_f("g", 0.5f)),
            self.in_number("b", ctx, self.attrs.get_f("b", 0.5f)),
            self.in_number("a", ctx, self.attrs.get_f("a", 1.f)));
      });
    },
    [](Node &) {})

// ------------------------------------------------------------ vector split
REGISTER_NODE(
    FieldVectorSplit, "Field Convert",
    "Takes a vector apart: x, y, z and its length as numbers",
    [](Node &n) {
      n.add_field_in("vector", FieldType::Vector, true);
      auto lane = [](int k) {
        return [k](const Node &self, const FieldContext &ctx) {
          float v[3];
          self.in_field("vector", ctx, FieldValue::vector(0, 0, 0)).as_vector(v);
          if (k < 0)
            return FieldValue(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
          return FieldValue(v[k]);
        };
      };
      n.add_field_out("x", FieldType::Number, lane(0));
      n.add_field_out("y", FieldType::Number, lane(1));
      n.add_field_out("z", FieldType::Number, lane(2));
      n.add_field_out("length", FieldType::Number, lane(-1));
    },
    [](Node &) {})

REGISTER_NODE(
    FieldVectorCombine, "Field Convert", "Builds a vector from x, y and z numbers",
    [](Node &n) {
      n.add_field_in("x", FieldType::Number, true);
      n.add_field_in("y", FieldType::Number, true);
      n.add_field_in("z", FieldType::Number, true);
      add_float(n.attrs, "x", "X (when unconnected)", 0.f, -1000.f, 1000.f);
      add_float(n.attrs, "y", "Y (when unconnected)", 0.f, -1000.f, 1000.f);
      add_float(n.attrs, "z", "Z (when unconnected)", 0.f, -1000.f, 1000.f);
      n.add_field_out("out", FieldType::Vector, [](const Node &self,
                                                   const FieldContext &ctx) {
        return FieldValue::vector(
            self.in_number("x", ctx, self.attrs.get_f("x", 0.f)),
            self.in_number("y", ctx, self.attrs.get_f("y", 0.f)),
            self.in_number("z", ctx, self.attrs.get_f("z", 0.f)));
      });
    },
    [](Node &) {})

// --------------------------------------------------------- texcoord split
REGISTER_NODE(
    FieldTexCoordSplit, "Field Convert",
    "Takes texture coordinates apart into u and v numbers",
    [](Node &n) {
      n.add_field_in("uv", FieldType::TexCoord, true);
      auto lane = [](int k) {
        return [k](const Node &self, const FieldContext &ctx) {
          float uv[2];
          self.in_field("uv", ctx, FieldValue::texcoord(0, 0)).as_texcoord(uv);
          return FieldValue(uv[k]);
        };
      };
      n.add_field_out("u", FieldType::Number, lane(0));
      n.add_field_out("v", FieldType::Number, lane(1));
    },
    [](Node &) {})

REGISTER_NODE(
    FieldTexCoordCombine, "Field Convert",
    "Builds texture coordinates from u and v numbers",
    [](Node &n) {
      n.add_field_in("u", FieldType::Number, true);
      n.add_field_in("v", FieldType::Number, true);
      add_float(n.attrs, "u", "U (when unconnected)", 0.f, -64.f, 64.f);
      add_float(n.attrs, "v", "V (when unconnected)", 0.f, -64.f, 64.f);
      n.add_field_out("out", FieldType::TexCoord, [](const Node &self,
                                                     const FieldContext &ctx) {
        return FieldValue::texcoord(
            self.in_number("u", ctx, self.attrs.get_f("u", 0.f)),
            self.in_number("v", ctx, self.attrs.get_f("v", 0.f)));
      });
    },
    [](Node &) {})

// ------------------------------------------------------ explicit adapters
// The implicit conversion is always available by just wiring the link; these
// exist to choose *which* conversion when the default is not the wanted one.
REGISTER_NODE(
    FieldToNumber, "Field Convert",
    "Any value as a number: luminance / length, one lane, alpha, max or average",
    [](Node &n) {
      n.add_field_in("in", FieldType::Number, true);
      add_choice(n.attrs, "mode", "Read as",
                 {"Auto (luminance, length, u)", "First lane (R / X / U)",
                  "Second lane (G / Y / V)", "Third lane (B / Z)", "Alpha",
                  "Largest lane", "Average of lanes"},
                 0)
          .tooltip = "Auto is what an unconverted link does: a colour is its\n"
                     "luminance, a vector its length. The lane modes pick one\n"
                     "component; a type with fewer lanes gives its last one.";
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        FieldValue fv = self.in_field("in", ctx, FieldValue(0.f));
        int mode = self.attrs.get_choice("mode");
        int cnt = lane_count(fv.type);
        switch (mode) {
          case 1: return FieldValue(fv.v[0]);
          case 2: return FieldValue(fv.v[std::min(1, cnt - 1)]);
          case 3: return FieldValue(fv.v[std::min(2, cnt - 1)]);
          case 4: return FieldValue(fv.type == FieldType::Color ? fv.v[3] : 1.f);
          case 5: {
            float m = fv.v[0];
            for (int i = 1; i < cnt; ++i) m = std::max(m, fv.v[i]);
            return FieldValue(m);
          }
          case 6: {
            float s = 0.f;
            for (int i = 0; i < cnt; ++i) s += fv.v[i];
            return FieldValue(s / (float)cnt);
          }
          default: return FieldValue(fv.number());
        }
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldToColor, "Field Convert",
    "Any value as a colour: grey from a number, RGB from a vector, with an alpha",
    [](Node &n) {
      n.add_field_in("in", FieldType::Color, true);
      add_float(n.attrs, "alpha", "Alpha (non-colour inputs)", 1.f, 0.f, 1.f);
      add_bool(n.attrs, "signed_vector", "Vector is -1..1 (remap to 0..1)", true)
          .tooltip = "A direction or normal spans -1..1; on it maps that range\n"
                     "onto 0..1 the way a normal map does.";
      n.add_field_out("out", FieldType::Color, [](const Node &self,
                                                  const FieldContext &ctx) {
        FieldValue fv = self.in_field("in", ctx, FieldValue(0.f));
        float a = self.attrs.get_f("alpha", 1.f);
        switch (fv.type) {
          case FieldType::Color: return fv;
          case FieldType::Vector: {
            bool sg = self.attrs.get_b("signed_vector");
            auto m = [sg](float x) { return sg ? x * 0.5f + 0.5f : x; };
            return FieldValue::color(m(fv.v[0]), m(fv.v[1]), m(fv.v[2]), a);
          }
          case FieldType::TexCoord: return FieldValue::color(fv.v[0], fv.v[1], 0.f, a);
          default: return FieldValue::color(fv.v[0], fv.v[0], fv.v[0], a);
        }
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldToVector, "Field Convert",
    "Any value as a vector: a number broadcast, RGB of a colour, UV on a plane",
    [](Node &n) {
      n.add_field_in("in", FieldType::Vector, true);
      add_choice(n.attrs, "plane", "Texture coordinates lie on",
                 {"XZ (ground)", "XY (front)", "ZY (side)"}, 0);
      n.add_field_out("out", FieldType::Vector, [](const Node &self,
                                                   const FieldContext &ctx) {
        FieldValue fv = self.in_field("in", ctx, FieldValue(0.f));
        if (fv.type == FieldType::TexCoord) {
          float u = fv.v[0], v = fv.v[1];
          switch (self.attrs.get_choice("plane")) {
            case 1: return FieldValue::vector(u, v, 0.f);
            case 2: return FieldValue::vector(0.f, v, u);
            default: return FieldValue::vector(u, 0.f, v);
          }
        }
        float o[3];
        fv.as_vector(o);
        return FieldValue::vector(o[0], o[1], o[2]);
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldToTexCoord, "Field Convert",
    "Any value as texture coordinates: a vector projected on a plane, RG, or n,n",
    [](Node &n) {
      n.add_field_in("in", FieldType::TexCoord, true);
      add_choice(n.attrs, "plane", "Project a vector on",
                 {"XZ (ground)", "XY (front)", "ZY (side)"}, 0);
      n.add_field_out("out", FieldType::TexCoord, [](const Node &self,
                                                     const FieldContext &ctx) {
        FieldValue fv = self.in_field("in", ctx, FieldValue(0.f));
        if (fv.type == FieldType::Vector) {
          switch (self.attrs.get_choice("plane")) {
            case 1: return FieldValue::texcoord(fv.v[0], fv.v[1]);
            case 2: return FieldValue::texcoord(fv.v[2], fv.v[1]);
            default: return FieldValue::texcoord(fv.v[0], fv.v[2]);
          }
        }
        float uv[2];
        fv.as_texcoord(uv);
        return FieldValue::texcoord(uv[0], uv[1]);
      });
    },
    [](Node &) {})

} // namespace gpx
