// Geekatplay TerraForge — scene and environment persistence.
//
// A project file used to carry the node graph plus planets and infinite
// surfaces, and nothing else. Everything else silently reset on load:
// imported meshes vanished outright (with their transforms and material
// bindings), every camera beyond the default was gone along with its lens and
// render assignment, and the whole environment — sun, sky, fog, clouds,
// water, world scale — snapped back to defaults. Set up a sunset over a lake
// with a hero camera, save, reload: flat noon over defaults.
//
// These two serializers close that. scene_to_json writes the objects array
// verbatim — builtins included, in order — so parent indices, layer indices
// and the selection survive without any remapping. environment_to_json walks
// a single field table shared by both directions, so a field cannot round-trip
// asymmetrically.
//
// Node-id bindings (a mesh's material, the terrain material override, the
// normal/roughness/displacement maps) need the graph loader's id map: node
// ids are reassigned on load, so the file's ids are only meaningful through
// the same translation the links go through.
#pragma once
#include <cstdint>
#include <json.hpp>
#include <map>
#include <string>

namespace studio {

using GraphIdMap = std::map<uint64_t, uint64_t>; // file node id -> live id

// The whole scene: objects (all of them, in order), layers, selection, the
// active and last-used camera.
nlohmann::json scene_to_json();
// Rebuilds the scene from that. Meshes are re-imported from their recorded
// file path; one that has gone missing keeps its object (so the transform and
// material binding survive) and reports through `warnings`, one line per
// problem. Node-id bindings are translated through `idmap`.
void scene_from_json(const nlohmann::json &j, const GraphIdMap &idmap,
                     std::string &warnings);

// Every user-facing environment and render setting, from one field table.
nlohmann::json environment_to_json();
void environment_from_json(const nlohmann::json &j, const GraphIdMap &idmap);

} // namespace studio
