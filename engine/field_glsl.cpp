#include "gpx/field_glsl.hpp"
#include "field_glsl_internal.hpp"
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
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
// Cellular (Worley) noise, mirroring gpx::planet::pl_cell line for line.
// Returns (f1, f2, id): nearest distance, second nearest, and a stable random
// value for the nearest cell.
uint gpxf_hash_bits(vec3 ip, uint seed){
  uvec3 q = uvec3(ivec3(ip));
  uint h = q.x*374761393u + q.y*668265263u + q.z*2147483647u + seed*3266489917u;
  h = (h ^ (h>>13u)) * 1274126177u;
  h ^= h>>16u;
  return h;
}
vec3 gpxf_cell(vec3 p, uint seed, float jitter, int metric){
  vec3 i = floor(p), f = p - i;
  float f1 = 1e9, f2 = 1e9, id = 0.0;
  for (int dz = -1; dz <= 1; ++dz)
  for (int dy = -1; dy <= 1; ++dy)
  for (int dx = -1; dx <= 1; ++dx){
    vec3 d3 = vec3(float(dx), float(dy), float(dz));
    uint h = gpxf_hash_bits(i + d3, seed);
    vec3 o = vec3(float(h & 0x3ffu), float((h>>10u) & 0x3ffu),
                  float((h>>20u) & 0x3ffu)) * (1.0/1023.0);
    vec3 q = d3 + vec3(0.5) + (o - vec3(0.5)) * jitter - f;
    float d;
    if (metric == 1) d = abs(q.x) + abs(q.y) + abs(q.z);
    else if (metric == 2) d = max(abs(q.x), max(abs(q.y), abs(q.z)));
    else d = sqrt(dot(q, q));
    if (d < f1){ f2 = f1; f1 = d; id = float(h & 0xffffffu) * (1.0/16777215.0); }
    else if (d < f2) f2 = d;
  }
  return vec3(f1, f2, id);
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

std::string field_glsl_strip_prelude(const std::string &code) {
  const size_t n = std::strlen(PRELUDE);
  if (code.compare(0, n, PRELUDE) == 0) return code.substr(n);
  return code; // already stripped, or not one of ours
}

// ----------------------------------------------------------------- emitters

// the transpiler internals live in glslgen (field_glsl_internal.hpp); the
// compiler half uses them as its own
using namespace glslgen;


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
