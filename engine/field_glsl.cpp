#include "gpx/field_glsl.hpp"
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace gpx {

// ----------------------------------------------------------------- prelude
// A line-for-line mirror of gpx::planet::pl_hash / pl_vnoise / pl_fbm. These
// two implementations must agree, which is what the CPU-vs-GPU test checks;
// keeping them adjacent in the codebase is deliberate.
static const char *PRELUDE = R"GLSL(
float gpxf_hash(vec3 ip, uint seed){
  uvec3 q = uvec3(ivec3(ip));
  uint h = q.x*374761393u + q.y*668265263u + q.z*2147483647u + seed*3266489917u;
  h = (h ^ (h>>13u)) * 1274126177u;
  h ^= h>>16u;
  return float(h & 0xffffffu) / 16777215.0;
}
float gpxf_vnoise(vec3 p, uint seed){
  vec3 i = floor(p), f = fract(p);
  f = f*f*(3.0-2.0*f);
  float c000=gpxf_hash(i,seed),             c100=gpxf_hash(i+vec3(1,0,0),seed);
  float c010=gpxf_hash(i+vec3(0,1,0),seed), c110=gpxf_hash(i+vec3(1,1,0),seed);
  float c001=gpxf_hash(i+vec3(0,0,1),seed), c101=gpxf_hash(i+vec3(1,0,1),seed);
  float c011=gpxf_hash(i+vec3(0,1,1),seed), c111=gpxf_hash(i+vec3(1,1,1),seed);
  float x00=mix(c000,c100,f.x), x10=mix(c010,c110,f.x);
  float x01=mix(c001,c101,f.x), x11=mix(c011,c111,f.x);
  return mix(mix(x00,x10,f.y), mix(x01,x11,f.y), f.z);
}
float gpxf_fbm(vec3 p, uint seed, int octaves, int type){
  float sum=0.0, amp=1.0, norm=0.0;
  vec3 q = p;
  for (int i = 0; i < 12; ++i){
    if (i >= octaves) break;
    float n = gpxf_vnoise(q, seed + uint(i)*101u);
    if (type == 1) n = 1.0 - abs(n*2.0-1.0);
    else if (type == 2) n = abs(n*2.0-1.0);
    sum += n * amp;
    norm += amp;
    amp *= 0.5;
    q *= 2.03;
  }
  float v = norm > 0.0 ? sum/norm : 0.0;
  if (type == 1) v = v*v;
  return v - 0.5;
}
// guarded division: the CPU side returns 0 rather than NaN, so must this
float gpxf_div(float a, float b){ return abs(b) > 1e-9 ? a/b : 0.0; }
// Soft membership of a band. Deliberately not smoothstep: smoothstep(e,e,x) is
// undefined when the edges coincide, which is exactly what zero fuzziness asks
// for. Mirrors band() in nodes_field_material.cpp.
float gpxf_band(float x, float lo, float hi, float fuzz){
  if (fuzz <= 1e-6) return (x >= lo && x <= hi) ? 1.0 : 0.0;
  float a = clamp((x - (lo - fuzz)) / (2.0 * fuzz), 0.0, 1.0);
  float b = clamp(((hi + fuzz) - x) / (2.0 * fuzz), 0.0, 1.0);
  a = a*a*(3.0-2.0*a);
  b = b*b*(3.0-2.0*b);
  return min(a, b);
}
)GLSL";

const char *field_glsl_prelude() { return PRELUDE; }

// ----------------------------------------------------------------- emitters
namespace {

struct EmitCtx {
  std::ostringstream body;
  // Node id alone is not a sufficient key. Redirect and Compute Normal ask for
  // the *same* upstream subtree at a *different* evaluation point, and those
  // are genuinely different values. The key is therefore the node plus the
  // scope it is being evaluated in, so a shared subexpression is still emitted
  // once per distinct point rather than once overall.
  std::map<std::string, std::string> var_of;
  std::vector<std::string> samplers;
  int counter = 0;
  std::string error;

  // The current evaluation point. Emitters must read the point through these
  // rather than hard-coding "P"/"alt"/"lod", or they will silently ignore a
  // redirect or a quality boost.
  std::string pos = "P";
  std::string alt = "alt";
  std::string lod = "lod";

  std::string key(uint64_t id, const std::string &out_port) const {
    return pos + "\x1f" + alt + "\x1f" + lod + "\x1f" + std::to_string(id) +
           "\x1f" + out_port;
  }
  std::string fresh(const char *tag) {
    return std::string("v_") + tag + "_" + std::to_string(counter++);
  }

  // Declare a variable from an expression, and hand back its name.
  //
  // Always use this rather than streaming into `body` directly. Resolving an
  // input appends that subtree's own declarations to this same buffer, so
  //
  //     body << "float " << v << " = " << in("a", "0.0") << ";\n";
  //
  // splices those declarations into the middle of the line being written and
  // produces a shader that does not compile. A function call evaluates its
  // arguments first, so by the time declare() appends anything, everything the
  // expression needed is already in place. That ordering is the whole point.
  std::string declare(const char *type, const char *tag, const std::string &expr) {
    std::string v = fresh(tag);
    body << "  " << type << " " << v << " = " << expr << ";\n";
    return v;
  }
};

// How a node's inputs are reached: either an upstream variable, or a literal.
// `at` is the same thing evaluated somewhere else, which is what makes warping
// and finite-difference normals expressible on the GPU.
struct InputFn {
  std::function<std::string(const char *, const char *)> f;
  std::function<std::string(const char *, const std::string &,
                            const std::string &, const std::string &,
                            const char *)>
      f_at;
  std::string operator()(const char *port, const char *fallback) const {
    return f(port, fallback);
  }
  // Evaluate `port` somewhere else: at an explicit position, altitude and
  // detail budget. This is what Redirect, Displace and ComputeNormal need.
  std::string at(const char *port, const std::string &pos_expr,
                 const std::string &alt_expr, const std::string &lod_expr,
                 const char *fallback) const {
    return f_at(port, pos_expr, alt_expr, lod_expr, fallback);
  }
};

struct Emitter {
  // name, node, resolve(input port, fallback glsl) -> glsl expr, ctx -> vec4 expr
  std::function<std::string(const Node &, const InputFn &, EmitCtx &)> emit;
};

static std::string f2s(float v) {
  std::ostringstream o;
  o.precision(9);
  o << std::showpoint << v;
  std::string s = o.str();
  // GLSL needs a decimal point on float literals
  if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
    s += ".0";
  return s;
}

static std::map<std::string, Emitter> &emitters() {
  static std::map<std::string, Emitter> m;
  return m;
}

// A node's primary output. field_glsl_supports() looks for exactly this, so a
// node with no plain registration counts as unsupported however many secondary
// outputs it declares.
static void reg(const char *type,
                std::function<std::string(const Node &, const InputFn &, EmitCtx &)> f) {
  emitters()[type] = Emitter{std::move(f)};
}

// A secondary output. Nodes like Displace (height and offset) and ComputeNormal
// (normal and slope) genuinely produce different values per port, so the port
// is part of the lookup and part of the cache key.
static void reg_out(const char *type, const char *port,
                    std::function<std::string(const Node &, const InputFn &, EmitCtx &)> f) {
  emitters()[std::string(type) + "\x1f" + port] = Emitter{std::move(f)};
}

// Registering happens once, on first use. Each emitter returns a GLSL
// expression producing a vec4 laid out exactly like FieldValue.
static void install_emitters() {
  static bool done = false;
  if (done) return;
  done = true;

  // ---- inputs
  reg("FieldPosition", [](const Node &, const InputFn &, EmitCtx &ctx) {
    return "vec4(" + ctx.pos + ", 0.0)";
  });
  reg("FieldNormal", [](const Node &, const InputFn &, EmitCtx &) {
    return std::string("vec4(N, 0.0)");
  });
  reg("FieldAltitude", [](const Node &, const InputFn &, EmitCtx &ctx) {
    return "vec4(" + ctx.alt + ", 0.0, 0.0, 1.0)";
  });
  reg("FieldSlope", [](const Node &, const InputFn &, EmitCtx &) {
    return std::string("vec4(slope, 0.0, 0.0, 1.0)");
  });
  reg("FieldOrientation", [](const Node &, const InputFn &, EmitCtx &) {
    return std::string("vec4(orient, 0.0, 0.0, 1.0)");
  });
  reg("FieldTime", [](const Node &, const InputFn &, EmitCtx &) {
    return std::string("vec4(t, 0.0, 0.0, 1.0)");
  });

  reg("FieldConstant", [](const Node &n, const InputFn &, EmitCtx &) {
    return "vec4(" + f2s(n.attrs.get_f("value", 0.5f)) + ", 0.0, 0.0, 1.0)";
  });
  reg("FieldColorConstant", [](const Node &n, const InputFn &, EmitCtx &) {
    const Attribute *a = n.attrs.find("color");
    float c[4] = {1, 1, 1, 1};
    if (a) for (int i = 0; i < 4; ++i) c[i] = a->col[i];
    return "vec4(" + f2s(c[0]) + ", " + f2s(c[1]) + ", " + f2s(c[2]) + ", " +
           f2s(c[3]) + ")";
  });

  // ---- math
  reg("FieldMath", [](const Node &n, const InputFn &in, EmitCtx &) {
    std::string a = in("a", f2s(n.attrs.get_f("a_default", 0.f)).c_str());
    std::string b = in("b", f2s(n.attrs.get_f("b_default", 1.f)).c_str());
    std::string e;
    switch (n.attrs.get_choice("op")) {
      case 0: e = "(" + a + " + " + b + ")"; break;
      case 1: e = "(" + a + " - " + b + ")"; break;
      case 2: e = "(" + a + " * " + b + ")"; break;
      case 3: e = "gpxf_div(" + a + ", " + b + ")"; break;
      case 4: e = "min(" + a + ", " + b + ")"; break;
      case 5: e = "max(" + a + ", " + b + ")"; break;
      case 6: e = "pow(max(" + a + ", 0.0), " + b + ")"; break;
      case 7: e = "(abs(" + b + ") > 1e-9 ? mod(" + a + ", " + b + ") : 0.0)"; break;
      default: e = "abs(" + a + " - " + b + ")"; break;
    }
    return "vec4(" + e + ", 0.0, 0.0, 1.0)";
  });

  reg("FieldTrig", [](const Node &n, const InputFn &in, EmitCtx &) {
    bool deg = n.attrs.get_b("degrees");
    std::string x = "(" + in("in", "0.0") + " * " + f2s(n.attrs.get_f("scale", 1.f)) + ")";
    if (deg) x = "(" + x + " * 0.017453293)";
    int fn = n.attrs.get_choice("fn");
    std::string e;
    switch (fn) {
      case 0: e = "sin(" + x + ")"; break;
      case 1: e = "cos(" + x + ")"; break;
      case 2: e = "clamp(tan(" + x + "), -1e4, 1e4)"; break;
      case 3: e = "asin(clamp(" + x + ", -1.0, 1.0))"; break;
      case 4: e = "acos(clamp(" + x + ", -1.0, 1.0))"; break;
      case 5: e = "atan(" + x + ")"; break;
      case 6: e = "sinh(clamp(" + x + ", -20.0, 20.0))"; break;
      case 7: e = "cosh(clamp(" + x + ", -20.0, 20.0))"; break;
      default: e = "tanh(" + x + ")"; break;
    }
    if (deg && fn >= 3 && fn <= 5) e = "(" + e + " * 57.29578)";
    return "vec4(" + e + ", 0.0, 0.0, 1.0)";
  });

  reg("FieldRemap", [](const Node &n, const InputFn &in, EmitCtx &) {
    float f0, f1, t0, t1;
    n.attrs.get_range("from", f0, f1);
    n.attrs.get_range("to", t0, t1);
    float d = f1 - f0;
    std::string x = in("in", "0.0");
    std::string t = std::fabs(d) > 1e-9f
                        ? "((" + x + " - " + f2s(f0) + ") / " + f2s(d) + ")"
                        : std::string("0.0");
    std::string e = "(" + f2s(t0) + " + " + t + " * " + f2s(t1 - t0) + ")";
    if (n.attrs.get_b("clamp", true))
      e = "clamp(" + e + ", " + f2s(std::min(t0, t1)) + ", " +
          f2s(std::max(t0, t1)) + ")";
    return "vec4(" + e + ", 0.0, 0.0, 1.0)";
  });

  reg("FieldCurve", [](const Node &n, const InputFn &in, EmitCtx &) {
    std::string x = in("in", "0.0");
    float e0, e1;
    n.attrs.get_range("edges", e0, e1);
    float amt = n.attrs.get_f("amount", 1.f);
    std::string e;
    switch (n.attrs.get_choice("shape")) {
      case 0: e = "pow(max(" + x + ", 0.0), " + f2s(amt) + ")"; break;
      case 1: {
        std::string t = std::fabs(e1 - e0) > 1e-9f
                            ? "clamp((" + x + " - " + f2s(e0) + ") / " +
                                  f2s(e1 - e0) + ", 0.0, 1.0)"
                            : std::string("0.0");
        e = "(" + t + " * " + t + " * (3.0 - 2.0 * " + t + "))";
      } break;
      case 2: e = "(" + x + " >= " + f2s(e0) + " ? 1.0 : 0.0)"; break;
      case 3: {
        float b = std::clamp(amt / 8.f, 0.001f, 0.999f);
        std::string t = "clamp(" + x + ", 0.0, 1.0)";
        e = "(" + t + " / ((" + f2s(1.f / b - 2.f) + ") * (1.0 - " + t + ") + 1.0))";
      } break;
      default: e = "(1.0 - " + x + ")"; break;
    }
    return "vec4(" + e + ", 0.0, 0.0, 1.0)";
  });

  reg("FieldMix", [](const Node &n, const InputFn &in, EmitCtx &) {
    std::string a = in("a", "0.0");
    std::string b = in("b", "1.0");
    std::string t = "clamp(" + in("factor", f2s(n.attrs.get_f("amount", 0.5f)).c_str()) +
                    ", 0.0, 1.0)";
    return "vec4(mix(" + a + ", " + b + ", " + t + "), 0.0, 0.0, 1.0)";
  });

  reg("FieldVectorOp", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    // vector inputs need the xyz of the upstream vec4, so ask for the raw form
    std::string a = in("#a", ctx.pos.c_str());
    std::string b = in("#b", "vec3(0.0, 1.0, 0.0)");
    std::string e;
    switch (n.attrs.get_choice("op")) {
      case 0: e = "length(" + a + ")"; break;
      case 1: e = "dot(" + a + ", " + b + ")"; break;
      case 2: e = "distance(" + a + ", " + b + ")"; break;
      case 3: e = "(length(" + a + ") > 1e-9 ? normalize(" + a + ").x : 0.0)"; break;
      default: e = "cross(" + a + ", " + b + ").x"; break;
    }
    return "vec4(" + e + ", 0.0, 0.0, 1.0)";
  });

  // ---- noise
  reg("FieldNoise", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string p = in("#position", ctx.pos.c_str());
    float f = n.attrs.get_f("frequency", 3.f);
    int oct = n.attrs.get_i("octaves", 6);
    std::string octs =
        "min(" + std::to_string(oct) + ", max(1, int(" + ctx.lod + ")))";
    std::string e = "gpxf_fbm(" + p + " * " + f2s(f) + ", " +
                    std::to_string(n.attrs.get_seed("seed")) + "u, " + octs +
                    ", " + std::to_string(n.attrs.get_choice("type")) + ")";
    e = "(" + e + " * " + f2s(n.attrs.get_f("amplitude", 1.f)) + " + " +
        f2s(n.attrs.get_f("offset", 0.f)) + ")";
    return "vec4(" + e + ", 0.0, 0.0, 1.0)";
  });

  // ---- colour
  reg("FieldGradient", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string x = in("in", "0.5");
    float lo, hi;
    n.attrs.get_range("range", lo, hi);
    float d = hi - lo;
    std::string t = std::fabs(d) > 1e-9f
                        ? "clamp((" + x + " - " + f2s(lo) + ") / " + f2s(d) +
                              ", 0.0, 1.0)"
                        : std::string("0.0");
    const Attribute *g = n.attrs.find("gradient");
    if (!g || g->stops.empty())
      return "vec4(vec3(" + t + "), 1.0)";
    // unrolled ramp: stops are few and this keeps the shader branch-light
    std::string tv = ctx.declare("float", "t", t);
    std::string col = "vec4(" + f2s(g->stops[0].r) + ", " + f2s(g->stops[0].g) +
                      ", " + f2s(g->stops[0].b) + ", " + f2s(g->stops[0].a) + ")";
    for (size_t i = 1; i < g->stops.size(); ++i) {
      const auto &s0 = g->stops[i - 1];
      const auto &s1 = g->stops[i];
      float span = s1.t - s0.t;
      std::string k = span > 1e-9f
                          ? "clamp((" + tv + " - " + f2s(s0.t) + ") / " +
                                f2s(span) + ", 0.0, 1.0)"
                          : std::string("1.0");
      std::string c1 = "vec4(" + f2s(s1.r) + ", " + f2s(s1.g) + ", " +
                       f2s(s1.b) + ", " + f2s(s1.a) + ")";
      col = "mix(" + col + ", " + c1 + ", " + k + ")";
    }
    return col;
  });

  // ---- displacement family (P1)
  // These are the emitters the scoped cache exists for: each one evaluates a
  // subtree somewhere other than the point it was asked about.

  reg("FieldRedirect", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string off = in("#redirect", "vec3(0.0)");
    float s = n.attrs.get_f("strength", 1.f);
    std::string sc = "vec3(" + f2s(n.attrs.get_f("scale_x", 1.f) * s) + ", " +
                     f2s(n.attrs.get_f("scale_y", 1.f) * s) + ", " +
                     f2s(n.attrs.get_f("scale_z", 1.f) * s) + ")";
    std::string base =
        n.attrs.get_choice("mode") == 1 ? std::string("vec3(0.0)") : ctx.pos;
    std::string pv = ctx.declare("vec3", "rp", base + " + " + off + " * " + sc);
    std::string av = ctx.declare(
        "float", "ra", ctx.alt + " + (" + pv + ".y - " + ctx.pos + ".y)");
    std::string r = in.at("input", pv, av, ctx.lod, "0.0");
    return "vec4(" + r + ", 0.0, 0.0, 1.0)";
  });

  // The amount a Displace node moves by, shared between its two outputs so the
  // height and the offset can never disagree.
  auto displace_amount = [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    int q = n.attrs.get_i("quality", 0);
    std::string lodx =
        q > 0 ? "clamp(" + ctx.lod + " + " + f2s((float)q) + ", 1.0, 16.0)"
              : ctx.lod;
    std::string a = in.at("amount", ctx.pos, ctx.alt, lodx, "0.0");
    float sm = std::clamp(n.attrs.get_f("smoothing", 0.f), 0.f, 1.f);
    if (sm > 1e-6f) {
      float r = std::max(n.attrs.get_f("smooth_radius", 0.01f), 1e-6f);
      std::string cv = ctx.declare("float", "dc", a);
      const float off[4][2] = {{r, 0}, {-r, 0}, {0, r}, {0, -r}};
      std::string sum;
      for (const auto &o : off) {
        std::string p = "(" + ctx.pos + " + vec3(" + f2s(o[0]) + ", 0.0, " +
                        f2s(o[1]) + "))";
        std::string s = in.at("amount", p, ctx.alt, lodx, "0.0");
        sum = sum.empty() ? s : sum + " + " + s;
      }
      a = "(" + cv + " + ((" + sum + ") * 0.25 - " + cv + ") * " + f2s(sm) + ")";
    }
    std::string d = "(" + a + " * " + f2s(n.attrs.get_f("depth", 1.f) *
                                          (n.attrs.get_choice("depth_mode") == 1
                                               ? n.attrs.get_f("relative_size", 1.f)
                                               : 1.f)) +
                    ")";
    if (n.attrs.get_b("outwards_only")) d = "max(" + d + ", 0.0)";
    return d;
  };
  // Where it moves to. Must mirror displace_direction() in nodes_displace.cpp.
  auto displace_dir = [](const Node &n, const InputFn &in, EmitCtx &) {
    switch (n.attrs.get_choice("dir_mode")) {
      case 1: return std::string("vec3(0.0, 1.0, 0.0)");
      case 2: {
        std::string v = in("#direction", "vec3(0.0, 1.0, 0.0)");
        return "(length(" + v + ") > 1e-9 ? normalize(" + v +
               ") : vec3(0.0, 1.0, 0.0))";
      }
      case 3: {
        float x = n.attrs.get_f("dir_x", 0.f), y = n.attrs.get_f("dir_y", 1.f),
              z = n.attrs.get_f("dir_z", 0.f);
        float l = std::sqrt(x * x + y * y + z * z);
        if (l < 1e-9f) return std::string("vec3(0.0, 1.0, 0.0)");
        return "vec3(" + f2s(x / l) + ", " + f2s(y / l) + ", " + f2s(z / l) + ")";
      }
      default: return std::string("N");
    }
  };
  reg("FieldDisplace", [displace_amount, displace_dir](const Node &n,
                                                       const InputFn &in,
                                                       EmitCtx &ctx) {
    std::string d = displace_amount(n, in, ctx);
    std::string dir = displace_dir(n, in, ctx);
    return "vec4(" + ctx.alt + " + (" + dir + ").y * " + d + ", 0.0, 0.0, 1.0)";
  });
  reg_out("FieldDisplace", "offset",
          [displace_amount, displace_dir](const Node &n, const InputFn &in,
                                          EmitCtx &ctx) {
            std::string d = displace_amount(n, in, ctx);
            std::string dir = displace_dir(n, in, ctx);
            return "vec4((" + dir + ") * " + d + ", 0.0)";
          });

  // Central differences around the point. Both outputs derive from the same
  // two gradients, so slope and normal always describe the same surface.
  auto normal_gradients = [](const Node &n, const InputFn &in, EmitCtx &ctx,
                             std::string &gx, std::string &gz, std::string &e) {
    float ef = std::max(n.attrs.get_f("epsilon", 0.01f), 1e-6f);
    e = f2s(ef);
    std::string s = f2s(n.attrs.get_f("strength", 1.f));
    auto at = [&](float dx, float dz) {
      std::string p = "(" + ctx.pos + " + vec3(" + f2s(dx) + ", 0.0, " +
                      f2s(dz) + "))";
      return in.at("height", p, ctx.alt, ctx.lod, "0.0");
    };
    // Resolve all four samples BEFORE streaming anything: each at() call
    // appends its own declarations to the same body, and interleaving that
    // with a << chain splices them into the middle of the line being written.
    std::string xp = at(ef, 0), xm = at(-ef, 0);
    std::string zp = at(0, ef), zm = at(0, -ef);
    gx = ctx.declare("float", "gx", "(" + xp + " - " + xm + ") * " + s);
    gz = ctx.declare("float", "gz", "(" + zp + " - " + zm + ") * " + s);
  };
  reg("FieldComputeNormal", [normal_gradients](const Node &n, const InputFn &in,
                                               EmitCtx &ctx) {
    std::string gx, gz, e;
    normal_gradients(n, in, ctx, gx, gz, e);
    std::string v = "vec3(-" + gx + ", 2.0 * " + e + ", -" + gz + ")";
    std::string nv = "(length(" + v + ") > 1e-9 ? normalize(" + v +
                     ") : vec3(0.0, 1.0, 0.0))";
    if (n.attrs.get_b("flip")) nv = "(-" + nv + ")";
    return "vec4(" + nv + ", 0.0)";
  });
  reg_out("FieldComputeNormal", "slope",
          [normal_gradients](const Node &n, const InputFn &in, EmitCtx &ctx) {
            std::string gx, gz, e;
            normal_gradients(n, in, ctx, gx, gz, e);
            std::string ny = "(2.0 * " + e + ")";
            std::string l = "length(vec3(" + gx + ", " + ny + ", " + gz + "))";
            return "vec4(" + l + " > 1e-9 ? " + ny + " / " + l +
                   " : 1.0, 0.0, 0.0, 1.0)";
          });

  reg("FieldTexCoord", [](const Node &n, const InputFn &, EmitCtx &ctx) {
    std::string u, v;
    switch (n.attrs.get_choice("plane")) {
      case 1: u = ctx.pos + ".x"; v = ctx.pos + ".y"; break;
      case 2: u = ctx.pos + ".z"; v = ctx.pos + ".y"; break;
      default: u = ctx.pos + ".x"; v = ctx.pos + ".z"; break;
    }
    float a = n.attrs.get_f("angle", 0.f) * 0.017453293f;
    float ca = std::cos(a), sa = std::sin(a);
    float sx, sy, ox, oy;
    n.attrs.get_vec2("scale", sx, sy);
    n.attrs.get_vec2("offset", ox, oy);
    std::string ru = "(" + u + " * " + f2s(ca) + " - " + v + " * " + f2s(sa) + ")";
    std::string rv = "(" + u + " * " + f2s(sa) + " + " + v + " * " + f2s(ca) + ")";
    return "vec4(" + ru + " * " + f2s(sx) + " + " + f2s(ox) + ", " + rv + " * " +
           f2s(sy) + " + " + f2s(oy) + ", 0.0, 0.0)";
  });

  // Must mirror the mask lambda in nodes_displace.cpp exactly.
  auto zone_mask = [](const Node &n, EmitCtx &ctx) {
    float cx, cz;
    n.attrs.get_vec2("center", cx, cz);
    float cy = n.attrs.get_f("center_y", 0.f);
    float size = std::max(n.attrs.get_f("size", 1.f), 1e-6f);
    std::string dx = "(" + ctx.pos + ".x - " + f2s(cx) + ")";
    std::string dy = n.attrs.get_b("flat", true)
                         ? std::string("0.0")
                         : "(" + ctx.pos + ".y - " + f2s(cy) + ")";
    std::string dz = "(" + ctx.pos + ".z - " + f2s(cz) + ")";
    std::string d = n.attrs.get_choice("shape") == 1
                        ? "max(abs(" + dx + "), max(abs(" + dy + "), abs(" + dz +
                              ")))"
                        : "length(vec3(" + dx + ", " + dy + ", " + dz + "))";
    float fade = std::clamp(n.attrs.get_f("fade", 0.25f), 0.f, 1.f) * size;
    std::string dv = ctx.declare("float", "zd", d);
    if (fade <= 1e-9f)
      return "(" + dv + " < " + f2s(size) + " ? 1.0 : 0.0)";
    std::string t = "clamp((" + f2s(size) + " - " + dv + ") / " + f2s(fade) +
                    ", 0.0, 1.0)";
    std::string tv = ctx.declare("float", "zt", t);
    return "(" + tv + " * " + tv + " * (3.0 - 2.0 * " + tv + "))";
  };
  reg("FieldZone", [zone_mask](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string m = zone_mask(n, ctx);
    std::string a = in("inside", "1.0");
    std::string b = in("outside", "0.0");
    return "vec4(mix(" + b + ", " + a + ", " + m + "), 0.0, 0.0, 1.0)";
  });
  reg_out("FieldZone", "mask", [zone_mask](const Node &n, const InputFn &,
                                           EmitCtx &ctx) {
    return "vec4(" + zone_mask(n, ctx) + ", 0.0, 0.0, 1.0)";
  });

  // ---- materials (P2)
  reg("FieldDistribution", [](const Node &n, const InputFn &in, EmitCtx &ctx) {
    std::string m = "1.0";
    auto crit = [&](const char *flag, const char *rangek, const char *fuzzk,
                    const char *port, const std::string &fallback) {
      if (!n.attrs.get_b(flag, std::string(flag) == "use_altitude")) return;
      float lo, hi;
      n.attrs.get_range(rangek, lo, hi);
      std::string x = in(port, fallback.c_str());
      m = m + " * gpxf_band(" + x + ", " + f2s(lo) + ", " + f2s(hi) + ", " +
          f2s(n.attrs.get_f(fuzzk, 0.1f)) + ")";
    };
    crit("use_altitude", "altitude", "altitude_fuzz", "altitude", ctx.alt);
    crit("use_slope", "slope", "slope_fuzz", "slope", "slope");
    crit("use_orientation", "orientation", "orientation_fuzz", "orientation",
         "orient");
    std::string e = "(" + m + ")";
    if (n.attrs.get_b("invert")) e = "(1.0 - " + e + ")";
    return "vec4(" + e + ", 0.0, 0.0, 1.0)";
  });

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

  // ---- bridge back to a buffer
  reg("Sample", [](const Node &n, const InputFn &, EmitCtx &ctx) {
    // the host binds the heightmap this node reads; name it after the node so
    // several Sample nodes in one graph stay distinct
    std::string s = "u_gpxf_tex_" + std::to_string(n.id);
    ctx.samplers.push_back(s);
    float cx, cy;
    n.attrs.get_vec2("center", cx, cy);
    float size = n.attrs.get_f("size", 1.f);
    if (std::fabs(size) < 1e-9f) size = 1.f;
    std::string uv = "((" + ctx.pos + ".xz - vec2(" + f2s(cx) + ", " + f2s(cy) +
                     ")) / " + f2s(size) + " + 0.5)";
    uv = n.attrs.get_b("tile") ? "fract(" + uv + ")" : "clamp(" + uv + ", 0.0, 1.0)";
    return "vec4(texture(" + s + ", " + uv + ").r * " +
           f2s(n.attrs.get_f("scale", 1.f)) + ", 0.0, 0.0, 1.0)";
  });
}

} // namespace

bool field_glsl_supports(const std::string &node_type) {
  install_emitters();
  return emitters().count(node_type) > 0;
}

// -------------------------------------------------------------- compilation
static bool emit_node(const Node &n, const std::string &out_port, EmitCtx &ctx,
                      std::set<uint64_t> &visiting);

// Reading a vec4 as a scalar, exactly as FieldValue::number() does on the CPU.
// The permissive conversions are the point — a user may wire a colour into a
// number slot — so the two implementations have to agree on what that means,
// or a graph will look different on the GPU for no visible reason.
static std::string as_number(const std::string &v, FieldType t) {
  switch (t) {
    case FieldType::Color:
      return "dot(" + v + ".rgb, vec3(0.299, 0.587, 0.114))"; // luminance
    case FieldType::Vector:
      return "length(" + v + ".xyz)";
    default:
      return v + ".x";
  }
}
// The same for FieldValue::as_vector(): a vector or colour passes through, and
// anything scalar broadcasts to all three components rather than leaving two
// of them zero.
static std::string as_vec3(const std::string &v, FieldType t) {
  if (t == FieldType::Vector || t == FieldType::Color) return v + ".xyz";
  return "vec3(" + v + ".x)";
}
// And FieldValue::as_color(): a colour keeps its alpha, anything else becomes
// grey at full opacity. Forcing alpha to 1 for a real colour would quietly
// throw away transparency.
static std::string as_vec4(const std::string &v, FieldType t) {
  if (t == FieldType::Color) return v;
  return "vec4(vec3(" + as_number(v, t) + "), 1.0)";
}

// Resolve one input port to a GLSL expression. A leading '#' asks for the vec3
// form (vector inputs) and '@' for the vec4 form (colour inputs); otherwise the
// scalar form. The prefix picks which FieldValue conversion to mirror.
static std::string resolve_input(const Node &n, EmitCtx &ctx,
                                 std::set<uint64_t> &visiting, const char *port,
                                 const char *fallback) {
  bool want_vec3 = port[0] == '#';
  bool want_vec4 = port[0] == '@';
  std::string pname = (want_vec3 || want_vec4) ? port + 1 : port;
  // Which output feeds us matters: a node may produce several, and they are
  // different values, not different views of one.
  const Port *src_port = n.graph ? n.graph->upstream(n, pname) : nullptr;
  const Node *src = n.graph ? n.graph->upstream_node(n, pname) : nullptr;
  if (!src || !src_port) return fallback;
  if (!emit_node(*src, src_port->name, ctx, visiting)) return fallback;
  auto it = ctx.var_of.find(ctx.key(src->id, src_port->name));
  if (it == ctx.var_of.end()) return fallback;
  if (want_vec3) return as_vec3(it->second, src_port->field_type);
  if (want_vec4) return as_vec4(it->second, src_port->field_type);
  return as_number(it->second, src_port->field_type);
}

// The same, but evaluated somewhere else. Everything upstream is re-emitted
// under the new point, because under a redirect it genuinely is a different
// value; the scoped cache keeps a subtree shared within one point.
static std::string resolve_input_at(const Node &n, EmitCtx &ctx,
                                    std::set<uint64_t> &visiting,
                                    const char *port, const std::string &pos,
                                    const std::string &alt,
                                    const std::string &lod,
                                    const char *fallback) {
  std::string save_pos = ctx.pos, save_alt = ctx.alt, save_lod = ctx.lod;
  ctx.pos = pos;
  ctx.alt = alt;
  ctx.lod = lod;
  std::string r = resolve_input(n, ctx, visiting, port, fallback);
  ctx.pos = save_pos;
  ctx.alt = save_alt;
  ctx.lod = save_lod;
  return r;
}

static bool emit_node(const Node &n, const std::string &out_port, EmitCtx &ctx,
                      std::set<uint64_t> &visiting) {
  const std::string k = ctx.key(n.id, out_port);
  if (ctx.var_of.count(k)) return true; // already emitted here; reuse the value
  if (visiting.count(n.id)) {
    ctx.error = "cycle through node " + n.type;
    return false;
  }
  install_emitters();
  // the port-specific emitter if there is one, otherwise the node's primary
  auto e = emitters().find(n.type + "\x1f" + out_port);
  if (e == emitters().end()) e = emitters().find(n.type);
  if (e == emitters().end()) {
    ctx.error = "node type '" + n.type + "' has no GLSL emitter";
    return false;
  }
  visiting.insert(n.id);
  InputFn in;
  in.f = [&](const char *port, const char *fallback) {
    return resolve_input(n, ctx, visiting, port, fallback);
  };
  in.f_at = [&](const char *port, const std::string &pos, const std::string &alt,
                const std::string &lod, const char *fallback) {
    return resolve_input_at(n, ctx, visiting, port, pos, alt, lod, fallback);
  };
  std::string expr = e->second.emit(n, in, ctx);
  visiting.erase(n.id);
  if (!ctx.error.empty()) return false;
  std::string var = ctx.fresh("n");
  ctx.body << "  vec4 " << var << " = " << expr << ";\n";
  // key under the scope that was current when the emitter *started*: an
  // emitter may have moved the point while resolving its own inputs
  ctx.var_of[k] = var;
  return true;
}

GlslProgram field_to_glsl(const Node &node, const std::string &out_port,
                          const std::string &fn_name) {
  GlslProgram prog;
  prog.entry = fn_name;
  install_emitters();

  EmitCtx ctx;
  std::set<uint64_t> visiting;
  // default to the node's first field output when the caller did not name one
  std::string port = out_port;
  if (port.empty()) {
    for (const Port &p : node.ports)
      if (p.dir == PortDir::Out && p.type == DataType::Field) {
        port = p.name;
        break;
      }
  }
  if (!emit_node(node, port, ctx, visiting)) {
    prog.error = ctx.error.empty() ? "could not emit graph" : ctx.error;
    return prog;
  }
  auto it = ctx.var_of.find(ctx.key(node.id, port));
  if (it == ctx.var_of.end()) {
    prog.error = "output node produced no value";
    return prog;
  }

  std::ostringstream out;
  out << PRELUDE;
  for (const std::string &s : ctx.samplers)
    out << "uniform sampler2D " << s << ";\n";
  out << "\nvec4 " << fn_name
      << "(vec3 P, vec3 N, float alt, float slope, float orient, float t, "
         "float lod){\n";
  out << ctx.body.str();
  out << "  return " << it->second << ";\n}\n";

  prog.code = out.str();
  prog.samplers = ctx.samplers;
  prog.node_count = (int)ctx.var_of.size();
  prog.ok = true;
  return prog;
}

} // namespace gpx
