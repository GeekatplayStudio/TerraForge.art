// Geekatplay TerraForge - the GLSL emitters for the two plant field nodes,
// PlantVariable and PlantVector (engine/plant/plant_schema.hpp,
// engine/gpx/plant.hpp).
//
// On the CPU these read FieldContext::plant, which the plant builder sets
// while it is baking a specific primitive's parameters. The GPU field
// evaluator has no such thing: it walks the terrain's height/material graph
// per texel, never per plant primitive, so FieldContext::plant is null on
// that path too. The CPU code for both nodes already falls back to 0 when
// plant is null (see engine/nodes/nodes_plant.cpp), so emitting a constant
// here is not an approximation - it is the same answer the CPU gives on
// every path the GPU could possibly be asked to agree with, which is what
// keeps the CPU/GPU agreement check (studio/field_gpu_check.cpp) holding.
//
// PlantVariable folds its Scale/Offset into the constant: the variable
// itself is 0, so the node's output is scale*0 + offset, i.e. just offset.
#include "field_glsl_internal.hpp"

namespace gpx {
namespace glslgen {

void install_emitters_plant() {
  reg("PlantVariable", [](const Node &n, const InputFn &, EmitCtx &) {
    return "vec4(" + f2s(n.attrs.get_f("offset", 0.f)) + ", 0.0, 0.0, 1.0)";
  });
  reg("PlantVector", [](const Node &, const InputFn &, EmitCtx &) {
    return std::string("vec4(0.0, 0.0, 0.0, 0.0)");
  });
}

} // namespace glslgen
} // namespace gpx
