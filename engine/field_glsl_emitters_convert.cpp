// Geekatplay TerraForge — GLSL emitters for the field converters and colour
// nodes (nodes_field_convert.cpp, nodes_field_color.cpp). Split from
// field_glsl_emitters.cpp for the 500-line module rule.
//
// Where the CPU node branches on FieldValue::type at run time, the emitter
// branches on the upstream port's type at compile time (InputFn::type). The
// two therefore take the same path for the same graph, which is what lets
// verify_field_gpu hold them to agreement.
#include "field_glsl_internal.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {
namespace glslgen {

namespace {

std::string num(const std::string &e) { return "vec4(" + e + ", 0.0, 0.0, 1.0)"; }

// one lane of a colour, or its luminance for k < 0
void reg_color_lane(const char *type, const char *port, int k) {
  auto f = [k](const Node &, const InputFn &in, EmitCtx &ctx) {
    std::string c = ctx.declare("vec4", "cs", in("@color", "vec4(0.0, 0.0, 0.0, 1.0)"));
    if (k < 0) return num("dot(" + c + ".rgb, vec3(0.299, 0.587, 0.114))");
    const char *sw[4] = {".x", ".y", ".z", ".w"};
    return num(c + sw[k]);
  };
  if (port) reg_out(type, port, f);
  else reg(type, f);
}

void reg_vector_lane(const char *type, const char *port, int k) {
  auto f = [k](const Node &, const InputFn &in, EmitCtx &ctx) {
    std::string v = ctx.declare("vec3", "vs", in("#vector", "vec3(0.0)"));
    if (k < 0) return num("length(" + v + ")");
    const char *sw[3] = {".x", ".y", ".z"};
    return num(v + sw[k]);
  };
  if (port) reg_out(type, port, f);
  else reg(type, f);
}

void reg_hsv_lane(const char *type, const char *port, int k) {
  auto f = [k](const Node &, const InputFn &in, EmitCtx &ctx) {
    std::string c = ctx.declare("vec4", "ch", in("@color", "vec4(0.0, 0.0, 0.0, 1.0)"));
    std::string h = ctx.declare("vec3", "hsv", "gpxf_rgb2hsv(" + c + ".rgb)");
    const char *sw[3] = {".x", ".y", ".z"};
    return num(h + sw[k]);
  };
  if (port) reg_out(type, port, f);
  else reg(type, f);
}

int lane_count(FieldType t) {
  switch (t) {
    case FieldType::Number: return 1;
    case FieldType::TexCoord: return 2;
    default: return 3;
  }
}

} // namespace

void install_emitters_convert() {
  // ---- colour split / combine
  reg_color_lane("FieldColorSplit", nullptr, -1); // primary = luminance
  reg_color_lane("FieldColorSplit", "luminance", -1);
  reg_color_lane("FieldColorSplit", "r", 0);
  reg_color_lane("FieldColorSplit", "g", 1);
  reg_color_lane("FieldColorSplit", "b", 2);
  reg_color_lane("FieldColorSplit", "a", 3);

  reg("FieldColorCombine", [](const Node &n, const InputFn &in, EmitCtx &) {
    return "vec4(" + in("r", f2s(n.attrs.get_f("r", 0.5f)).c_str()) + ", " +
           in("g", f2s(n.attrs.get_f("g", 0.5f)).c_str()) + ", " +
           in("b", f2s(n.attrs.get_f("b", 0.5f)).c_str()) + ", " +
           in("a", f2s(n.attrs.get_f("a", 1.f)).c_str()) + ")";
  });

  // ---- vector split / combine
  reg_vector_lane("FieldVectorSplit", nullptr, 0); // primary = x
  reg_vector_lane("FieldVectorSplit", "x", 0);
  reg_vector_lane("FieldVectorSplit", "y", 1);
  reg_vector_lane("FieldVectorSplit", "z", 2);
  reg_vector_lane("FieldVectorSplit", "length", -1);

  reg("FieldVectorCombine", [](const Node &n, const InputFn &in, EmitCtx &) {
    return "vec4(" + in("x", f2s(n.attrs.get_f("x", 0.f)).c_str()) + ", " +
           in("y", f2s(n.attrs.get_f("y", 0.f)).c_str()) + ", " +
           in("z", f2s(n.attrs.get_f("z", 0.f)).c_str()) + ", 0.0)";
  });

  // ---- texcoord split / combine
  auto tc_lane = [](int k) {
    return [k](const Node &, const InputFn &in, EmitCtx &ctx) {
      std::string uv = ctx.declare("vec2", "uvs", in("%uv", "vec2(0.0)"));
      return num(uv + (k == 0 ? ".x" : ".y"));
    };
  };
  reg("FieldTexCoordSplit", tc_lane(0));
  reg_out("FieldTexCoordSplit", "u", tc_lane(0));
  reg_out("FieldTexCoordSplit", "v", tc_lane(1));

  reg("FieldTexCoordCombine", [](const Node &n, const InputFn &in, EmitCtx &) {
    return "vec4(" + in("u", f2s(n.attrs.get_f("u", 0.f)).c_str()) + ", " +
           in("v", f2s(n.attrs.get_f("v", 0.f)).c_str()) + ", 0.0, 0.0)";
  });

  // ---- explicit adapters: branch on the upstream type, as the CPU does
  reg("FieldToNumber", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    FieldType t = in.type("in");
    std::string r = ctx.declare("vec4", "raw", in("!in", "vec4(0.0, 0.0, 0.0, 1.0)"));
    int cnt = lane_count(t);
    const char *sw[4] = {".x", ".y", ".z", ".w"};
    switch (n.attrs.get_choice("mode")) {
      case 1: return num(r + ".x");
      case 2: return num(r + sw[std::min(1, cnt - 1)]);
      case 3: return num(r + sw[std::min(2, cnt - 1)]);
      case 4: return num(t == FieldType::Color ? r + ".w" : std::string("1.0"));
      case 5: {
        std::string m = r + ".x";
        for (int i = 1; i < cnt; ++i) m = "max(" + m + ", " + r + sw[i] + ")";
        return num(m);
      }
      case 6: {
        std::string s = r + ".x";
        for (int i = 1; i < cnt; ++i) s += " + " + r + sw[i];
        return num("(" + s + ") / " + f2s((float)cnt));
      }
      default: return num(in("in", "0.0")); // the scalar conversion = number()
    }
  });

  reg("FieldToColor", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    FieldType t = in.type("in");
    std::string a = f2s(n.attrs.get_f("alpha", 1.f));
    std::string r = ctx.declare("vec4", "raw", in("!in", "vec4(0.0, 0.0, 0.0, 1.0)"));
    switch (t) {
      case FieldType::Color: return r;
      case FieldType::Vector:
        if (n.attrs.get_b("signed_vector"))
          return "vec4(" + r + ".xyz * 0.5 + 0.5, " + a + ")";
        return "vec4(" + r + ".xyz, " + a + ")";
      case FieldType::TexCoord: return "vec4(" + r + ".xy, 0.0, " + a + ")";
      default: return "vec4(vec3(" + r + ".x), " + a + ")";
    }
  });

  reg("FieldToVector", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    if (in.type("in") == FieldType::TexCoord) {
      std::string r = ctx.declare("vec4", "raw", in("!in", "vec4(0.0)"));
      switch (n.attrs.get_choice("plane")) {
        case 1: return "vec4(" + r + ".x, " + r + ".y, 0.0, 0.0)";
        case 2: return "vec4(0.0, " + r + ".y, " + r + ".x, 0.0)";
        default: return "vec4(" + r + ".x, 0.0, " + r + ".y, 0.0)";
      }
    }
    return "vec4(" + in("#in", "vec3(0.0)") + ", 0.0)"; // = as_vector()
  });

  reg("FieldToTexCoord", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    if (in.type("in") == FieldType::Vector) {
      std::string r = ctx.declare("vec4", "raw", in("!in", "vec4(0.0)"));
      switch (n.attrs.get_choice("plane")) {
        case 1: return "vec4(" + r + ".x, " + r + ".y, 0.0, 0.0)";
        case 2: return "vec4(" + r + ".z, " + r + ".y, 0.0, 0.0)";
        default: return "vec4(" + r + ".x, " + r + ".z, 0.0, 0.0)";
      }
    }
    return "vec4(" + in("%in", "vec2(0.0)") + ", 0.0, 0.0)"; // = as_texcoord()
  });

  // ---- animation (nodes_animation.cpp). The graph clock is `t` in every
  // generated shader, the same value FieldTime reads.
  reg("Oscillator", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string p = ctx.declare(
        "float", "ph",
        in("time", "t") + " * " + f2s(n.attrs.get_f("frequency", 0.5f)) + " + " +
            f2s(n.attrs.get_f("phase", 0.f)));
    p = ctx.declare("float", "pf", "fract(" + p + ")");
    std::string w;
    switch (n.attrs.get_choice("shape")) {
      case 1: w = "(1.0 - abs(" + p + " * 4.0 - 2.0))"; break;
      case 2: w = "(" + p + " < 0.5 ? 1.0 : -1.0)"; break;
      case 3: w = "(" + p + " * 2.0 - 1.0)"; break;
      default: w = "sin(" + p + " * 6.2831853)"; break;
    }
    return num(w + " * " + f2s(n.attrs.get_f("amplitude", 1.f)) + " + " +
               f2s(n.attrs.get_f("offset", 0.f)));
  });

  reg("TimeRemap", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string t = ctx.declare(
        "float", "tr",
        in("time", "t") + " * " + f2s(n.attrs.get_f("speed", 1.f)) + " + " +
            f2s(n.attrs.get_f("offset", 0.f)));
    float loop = n.attrs.get_f("loop", 0.f);
    if (loop > 1e-6f) {
      if (n.attrs.get_b("pingpong")) {
        std::string p = ctx.declare("float", "tp",
                                    "fract(" + t + " / " + f2s(2.f * loop) + ")");
        t = ctx.declare("float", "tq",
                        "(" + p + " < 0.5 ? " + p + " * 2.0 : 2.0 - " + p +
                            " * 2.0) * " + f2s(loop));
      } else {
        t = ctx.declare("float", "tq",
                        "fract(" + t + " / " + f2s(loop) + ") * " + f2s(loop));
      }
    }
    return num(t);
  });

  // ---- colour blend (moved here from field_glsl_emitters.cpp)
  reg("FieldColorMix", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string a = ctx.declare("vec4", "ca", in("@a", "vec4(0.0, 0.0, 0.0, 1.0)"));
    std::string b = ctx.declare("vec4", "cb", in("@b", "vec4(1.0, 1.0, 1.0, 1.0)"));
    std::string t = ctx.declare(
        "float", "ct",
        "clamp(" + in("factor", f2s(n.attrs.get_f("amount", 0.5f)).c_str()) +
            ", 0.0, 1.0)");
    std::string v;
    switch (n.attrs.get_choice("mode")) {
      case 1: v = "(" + a + ".rgb + " + b + ".rgb)"; break;
      case 2: v = "(" + a + ".rgb * " + b + ".rgb)"; break;
      case 3: v = "(1.0 - (1.0 - " + a + ".rgb) * (1.0 - " + b + ".rgb))"; break;
      case 4:
        v = "mix(2.0 * " + a + ".rgb * " + b + ".rgb, 1.0 - 2.0 * (1.0 - " + a +
            ".rgb) * (1.0 - " + b + ".rgb), step(vec3(0.5), " + a + ".rgb))";
        break;
      case 5: v = "min(" + a + ".rgb, " + b + ".rgb)"; break;
      case 6: v = "max(" + a + ".rgb, " + b + ".rgb)"; break;
      default: v = b + ".rgb"; break;
    }
    return "vec4(mix(" + a + ".rgb, " + v + ", " + t + "), mix(" + a + ".a, " +
           b + ".a, " + t + "))";
  });

  // ---- HSV
  reg_hsv_lane("FieldColorHSV", nullptr, 0); // primary = hue
  reg_hsv_lane("FieldColorHSV", "h", 0);
  reg_hsv_lane("FieldColorHSV", "s", 1);
  reg_hsv_lane("FieldColorHSV", "v", 2);

  reg("FieldColorFromHSV", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string hsv = ctx.declare(
        "vec3", "hsv",
        "vec3(" + in("h", f2s(n.attrs.get_f("h", 0.1f)).c_str()) + ", " +
            in("s", f2s(n.attrs.get_f("s", 0.4f)).c_str()) + ", " +
            in("v", f2s(n.attrs.get_f("v", 0.6f)).c_str()) + ")");
    return "vec4(gpxf_hsv2rgb(" + hsv + "), " +
           in("a", f2s(n.attrs.get_f("a", 1.f)).c_str()) + ")";
  });

  // Same order as the CPU node: hue, saturation, contrast, brightness, gamma,
  // invert. Alpha passes through.
  reg("FieldColorAdjust", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string c = ctx.declare("vec4", "ca", in("@color", "vec4(0.5, 0.5, 0.5, 1.0)"));
    std::string rgb = ctx.declare("vec3", "crgb", c + ".rgb");
    float hue = n.attrs.get_f("hue", 0.f);
    if (hue != 0.f)
      rgb = ctx.declare("vec3", "chue",
                        "gpxf_hsv2rgb(gpxf_rgb2hsv(" + rgb + ") + vec3(" +
                            f2s(hue / 360.f) + ", 0.0, 0.0))");
    std::string lum = ctx.declare("float", "clum",
                                  "dot(" + rgb + ", vec3(0.299, 0.587, 0.114))");
    std::string x = ctx.declare(
        "vec3", "cx",
        "vec3(" + lum + ") + (" + rgb + " - vec3(" + lum + ")) * " +
            f2s(n.attrs.get_f("saturation", 1.f)));
    x = ctx.declare("vec3", "cc",
                    "(" + x + " - 0.5) * " + f2s(n.attrs.get_f("contrast", 1.f)) +
                        " + 0.5 + " + f2s(n.attrs.get_f("brightness", 0.f)));
    float inv_gamma = 1.f / std::max(n.attrs.get_f("gamma", 1.f), 1e-3f);
    x = ctx.declare("vec3", "cg",
                    "pow(max(" + x + ", vec3(0.0)), vec3(" + f2s(inv_gamma) + "))");
    if (n.attrs.get_b("invert")) x = "(vec3(1.0) - " + x + ")";
    return "vec4(" + x + ", " + c + ".a)";
  });
}

} // namespace glslgen
} // namespace gpx
