#include "gpx/field_glsl.hpp"
#include "gpx/node_graph.hpp"
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
)GLSL";

const char *field_glsl_prelude() { return PRELUDE; }

// ----------------------------------------------------------------- emitters
namespace {

struct EmitCtx {
  std::ostringstream body;
  std::map<uint64_t, std::string> var_of; // node id -> its output variable
  std::vector<std::string> samplers;
  int counter = 0;
  std::string error;

  std::string fresh(const char *tag) {
    return std::string("v_") + tag + "_" + std::to_string(counter++);
  }
};

// how a node's inputs are reached: either an upstream variable, or a literal
using InputFn = std::function<std::string(const char *, const char *)>;

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

static void reg(const char *type,
                std::function<std::string(const Node &, const InputFn &, EmitCtx &)> f) {
  emitters()[type] = Emitter{std::move(f)};
}

// Registering happens once, on first use. Each emitter returns a GLSL
// expression producing a vec4 laid out exactly like FieldValue.
static void install_emitters() {
  static bool done = false;
  if (done) return;
  done = true;

  // ---- inputs
  reg("FieldPosition", [](const Node &, const InputFn &, EmitCtx &) {
    return std::string("vec4(P, 0.0)");
  });
  reg("FieldNormal", [](const Node &, const InputFn &, EmitCtx &) {
    return std::string("vec4(N, 0.0)");
  });
  reg("FieldAltitude", [](const Node &, const InputFn &, EmitCtx &) {
    return std::string("vec4(alt, 0.0, 0.0, 1.0)");
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

  reg("FieldVectorOp", [](const Node &n, const InputFn &in, EmitCtx &) {
    // vector inputs need the xyz of the upstream vec4, so ask for the raw form
    std::string a = in("#a", "P");
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
  reg("FieldNoise", [](const Node &n, const InputFn &in, EmitCtx &) {
    std::string p = in("#position", "P");
    float f = n.attrs.get_f("frequency", 3.f);
    int oct = n.attrs.get_i("octaves", 6);
    std::string octs = "min(" + std::to_string(oct) + ", max(1, int(lod)))";
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
    std::string tv = ctx.fresh("t");
    ctx.body << "  float " << tv << " = " << t << ";\n";
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
    std::string uv = "((P.xz - vec2(" + f2s(cx) + ", " + f2s(cy) + ")) / " +
                     f2s(size) + " + 0.5)";
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
static bool emit_node(const Node &n, EmitCtx &ctx, std::set<uint64_t> &visiting);

// Resolve one input port to a GLSL expression. A leading '#' asks for the
// vec3 form (for vector inputs) rather than the scalar form.
static std::string resolve_input(const Node &n, EmitCtx &ctx,
                                 std::set<uint64_t> &visiting, const char *port,
                                 const char *fallback) {
  bool want_vec3 = port[0] == '#';
  std::string pname = want_vec3 ? port + 1 : port;
  const Node *src = n.graph ? n.graph->upstream_node(n, pname) : nullptr;
  if (!src) return fallback;
  if (!emit_node(*src, ctx, visiting)) return fallback;
  auto it = ctx.var_of.find(src->id);
  if (it == ctx.var_of.end()) return fallback;
  return want_vec3 ? it->second + ".xyz" : it->second + ".x";
}

static bool emit_node(const Node &n, EmitCtx &ctx, std::set<uint64_t> &visiting) {
  if (ctx.var_of.count(n.id)) return true; // already emitted; reuse the value
  if (visiting.count(n.id)) {
    ctx.error = "cycle through node " + n.type;
    return false;
  }
  install_emitters();
  auto e = emitters().find(n.type);
  if (e == emitters().end()) {
    ctx.error = "node type '" + n.type + "' has no GLSL emitter";
    return false;
  }
  visiting.insert(n.id);
  InputFn in = [&](const char *port, const char *fallback) {
    return resolve_input(n, ctx, visiting, port, fallback);
  };
  std::string expr = e->second.emit(n, in, ctx);
  visiting.erase(n.id);
  if (!ctx.error.empty()) return false;
  std::string var = ctx.fresh("n");
  ctx.body << "  vec4 " << var << " = " << expr << ";\n";
  ctx.var_of[n.id] = var;
  return true;
}

GlslProgram field_to_glsl(const Node &node, const std::string &out_port,
                          const std::string &fn_name) {
  GlslProgram prog;
  prog.entry = fn_name;
  install_emitters();

  EmitCtx ctx;
  std::set<uint64_t> visiting;
  if (!emit_node(node, ctx, visiting)) {
    prog.error = ctx.error.empty() ? "could not emit graph" : ctx.error;
    return prog;
  }
  auto it = ctx.var_of.find(node.id);
  if (it == ctx.var_of.end()) {
    prog.error = "output node produced no value";
    return prog;
  }
  (void)out_port; // a node's field outputs share one expression today

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
