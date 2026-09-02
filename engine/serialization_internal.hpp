// Geekatplay TerraForge - serialization internals shared between the graph
// serializer (serialization.cpp) and the material/library serializer
// (serialization_material.cpp). Private to the two.
#pragma once
#include "gpx/node_graph.hpp"
#include <json.hpp>

namespace gpx {
nlohmann::json attr_to_json(const Attribute &a);
void attr_from_json(Attribute &a, const nlohmann::json &j);
} // namespace gpx
