// Geekatplay TerraForge — field graph to GLSL (P0.2).
//
// The field domain evaluates on the CPU for tests, picking and rasterizing.
// This compiles the same graph to GPU code so a user-authored field can drive
// displacement and shading at whatever detail the camera needs — which is the
// mechanism behind Terragen's displacement and infinite terrain.
//
// The important property is that there is ONE definition of each node's
// behaviour intent, expressed twice (C++ and GLSL), with a test that holds the
// two together. Every field node must have an emitter here; the node contract
// suite fails if one is missing, so a node cannot ship CPU-only and silently
// diverge on the GPU.
#pragma once
#include <string>
#include <vector>

namespace gpx {

class Node;

struct GlslProgram {
  bool ok = false;
  std::string error;
  std::string code;   // prelude + the generated function
  std::string entry;  // name of the generated entry function
  // Uniform names the generated code expects the host to bind. Buffers reached
  // through Sample nodes appear here as sampler2D.
  std::vector<std::string> samplers;
  int node_count = 0;
};

// Compile the field subgraph feeding `out_port` of `node` into a GLSL function
//
//     vec4 <fn_name>(vec3 P, vec3 N, float alt, float slope, float orient,
//                    float t, float lod)
//
// returning the field value the same way FieldValue carries it (x for numbers,
// xyz for vectors and colours, xy for texture coordinates).
GlslProgram field_to_glsl(const Node &node, const std::string &out_port,
                          const std::string &fn_name = "gpx_field");

// True if this node type can be emitted. Used by the contract suite to prove
// the CPU and GPU node sets stay in step.
bool field_glsl_supports(const std::string &node_type);

// The shared prelude: hash, 3D value noise and fBm, mirroring
// gpx::planet::pl_* exactly. Exposed so the renderer can emit it once.
const char *field_glsl_prelude();

} // namespace gpx
