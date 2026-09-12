// Geekatplay TerraForge - the plant nodes' registration.
//
// Every plant node declares its parameters from the schema tables
// (engine/plant/plant_schema.hpp), its Plant ports from the kind's slot
// counts, and its field inputs from the table's `field_input` flags. Only
// the species root computes: it grows the individual (gpx/plant.hpp) and
// stores the mesh for the studio (plant_mesh_store). Every other plant node
// is data the root reads, so its compute is empty - the same as a
// MaterialLayer under a MaterialOutput.
//
// The two field nodes (PlantVariable, PlantVector) evaluate on the CPU from
// FieldContext::plant; their GLSL emitters return 0 because a plant is
// never grown on the GPU (engine/field_glsl_emitters_plant.cpp).
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/plant.hpp"
#include "plant/plant_schema.hpp"
#include <memory>

namespace gpx {

namespace {

using plant::Kind;

FieldValue variable_value(const Node &nd, const FieldContext &c) {
  const PlantVars *v = c.plant;
  float x = 0.f;
  if (v) {
    switch (nd.attrs.get_choice("which")) {
      case 0: x = v->primal; break;
      case 1: x = v->section_angle; break;
      case 2: x = v->radial; break;
      case 3: x = v->age; break;
      case 4: x = v->maturity; break;
      case 5: x = v->health; break;
      case 6: x = v->season; break;
      case 7: x = v->time; break;
      case 8: x = (float)v->depth; break;
      case 9: x = v->dist_root; break;
      case 10: x = v->height_frac; break;
      case 11: x = v->iteration; break;
      case 12: x = v->parent_primal; break;
      case 13: x = v->parent_radius; break;
      case 14: x = v->parent_length; break;
      case 15: x = v->parent_remaining; break;
      case 16: x = v->parent_tilt; break;
      case 17: x = v->length; break;
      case 18: x = v->radius; break;
      case 19: x = v->azimuth; break;
      case 20: x = v->rnd_instance; break;
      case 21: x = v->rnd_plant; break;
      case 22: x = (float)v->lod; break;
      case 23: x = (float)v->lod_max; break;
      case 24: x = v->pruned ? 1.f : 0.f; break;
      case 25: x = v->prune_ratio; break;
      default: break;
    }
  }
  return FieldValue(x * nd.attrs.get_f("scale", 1.f) + nd.attrs.get_f("offset", 0.f));
}

FieldValue vector_value(const Node &nd, const FieldContext &c) {
  const PlantVars *v = c.plant;
  if (!v) return FieldValue::vector(0.f, 0.f, 0.f);
  const float *p = v->parent_dir;
  switch (nd.attrs.get_choice("which")) {
    case 0: p = v->pos; break;
    case 1: p = v->dir; break;
    case 2: p = v->axis_dir; break;
    case 3: p = v->radial_dir; break;
    default: break;
  }
  return FieldValue::vector(p[0], p[1], p[2]);
}

void plant_ports(Node &n, Kind k) {
  if (k == Kind::Species) {
    n.add_in("trunk", DataType::Plant, false);
    for (int i = 1; i <= 4; ++i) n.add_in("bias " + std::to_string(i), DataType::Plant, true);
    n.add_out("plant", DataType::Plant); // a species can be a part of another (a component)
    return;
  }
  if (k == Kind::Variable) {
    n.add_field_out("value", FieldType::Number, variable_value);
    return;
  }
  if (k == Kind::Vector) {
    n.add_field_out("vector", FieldType::Vector, vector_value);
    return;
  }
  // parts, selectors, loops, biases and materials all hand themselves to a parent
  n.add_out("plant", DataType::Plant);
  for (int i = 1; i <= plant::child_slots(k); ++i) n.add_in("child " + std::to_string(i), DataType::Plant, true);
  if (k == Kind::Repeat) n.add_in("tail", DataType::Plant, true);
  for (int i = 1; i <= plant::material_slots(k); ++i)
    n.add_in(i == 1 ? std::string("material") : "material " + std::to_string(i), DataType::Plant, true);
  if (k == Kind::Segment) {
    n.add_in("cap material", DataType::Plant, true);
    n.add_in("blade material", DataType::Plant, true);
    n.add_in("blade material 2", DataType::Plant, true);
  }
}

void compute_species(Node &n) {
  if (!n.graph) return;
  // A species with nothing on its trunk grows nothing. That is where every
  // species starts in the editor, so it is an empty plant, not an error.
  if (!n.graph->upstream_node(n, "trunk")) {
    plant_mesh_forget(n.id);
    return;
  }
  PlantBuildOptions o;
  o.seed = n.attrs.get_seed("seed");
  if (o.seed == 0) o.seed = 1;
  if (n.attrs.get_b("time_from_scene", true)) o.time = n.graph->time;
  PlantMesh mesh;
  std::string err;
  if (!plant_build(*n.graph, n, o, mesh, err)) {
    n.error = err;
    plant_mesh_forget(n.id);
    return;
  }
  if (!mesh.warnings.empty()) n.error = mesh.warnings;
  plant_mesh_store(n.id, std::make_shared<const PlantMesh>(std::move(mesh)));
}

struct PlantRegistrar {
  PlantRegistrar() {
    for (int i = 0; i < (int)Kind::COUNT; ++i) {
      const Kind k = (Kind)i;
      NodeDef d;
      d.type = plant::kind_type(k);
      d.category = "Plant";
      d.description = plant::kind_description(k);
      d.setup = [k](Node &n) {
        plant::declare(n, k);
        plant_ports(n, k);
      };
      if (k == Kind::Species) d.compute = compute_species;
      else d.compute = [](Node &) {};
      NodeRegistry::instance().reg(std::move(d));
    }
  }
};
static PlantRegistrar reg_plant_nodes;

} // namespace
} // namespace gpx
