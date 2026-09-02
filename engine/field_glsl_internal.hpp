// Geekatplay TerraForge - the transpiler's internals, shared between the
// compiler (field_glsl.cpp) and the per-node emitters
// (field_glsl_emitters.cpp). Private to those two: nothing outside the
// transpiler includes this, which is why it is not under gpx/.
//
// The inline function-local statics (emitters()) are shared across the two
// translation units by C++17's inline-variable rules, so both sides see one
// registry.
#pragma once
#include "gpx/node_graph.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace gpx {
namespace glslgen {

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

inline std::string f2s(float v) {
  std::ostringstream o;
  o.precision(9);
  o << std::showpoint << v;
  std::string s = o.str();
  // GLSL needs a decimal point on float literals
  if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
    s += ".0";
  return s;
}

inline std::map<std::string, Emitter> &emitters() {
  static std::map<std::string, Emitter> m;
  return m;
}

// A node's primary output. field_glsl_supports() looks for exactly this, so a
// node with no plain registration counts as unsupported however many secondary
// outputs it declares.
inline void reg(const char *type,
                std::function<std::string(const Node &, const InputFn &, EmitCtx &)> f) {
  emitters()[type] = Emitter{std::move(f)};
}

// A secondary output. Nodes like Displace (height and offset) and ComputeNormal
// (normal and slope) genuinely produce different values per port, so the port
// is part of the lookup and part of the cache key.
inline void reg_out(const char *type, const char *port,
                    std::function<std::string(const Node &, const InputFn &, EmitCtx &)> f) {
  emitters()[std::string(type) + "\x1f" + port] = Emitter{std::move(f)};
}

// Defined in field_glsl_emitters.cpp; registers every node's emitter once.
void install_emitters();

} // namespace glslgen
} // namespace gpx
