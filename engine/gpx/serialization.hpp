// Geekatplay Studio — graph <-> JSON project files
#pragma once
#include "gpx/node_graph.hpp"
#include <cstdint>
#include <map>
#include <string>

namespace gpx {

std::string graph_to_json(const Graph &g);
// `idmap_out`, when given, receives the file-id -> live-id translation the
// loader built for its own links. Node ids are reassigned on load, so anything
// outside the graph that stored a node id (a mesh's material binding, the
// terrain material override) is meaningless without this map.
bool graph_from_json(Graph &g, const std::string &json_text, std::string &err,
                     std::map<uint64_t, uint64_t> *idmap_out = nullptr);
bool save_project(const Graph &g, const std::string &path);
bool load_project(Graph &g, const std::string &path, std::string &err);

// Build a graph from an AI-generated spec:
// {"nodes":[{"id":"h1","type":"Noise","attrs":{"octaves":10,"type":"Ridged"},
//            "pos":[0,0]}],
//  "links":[["h1","output","e1","input"]],
//  "environment":{...}}   (environment is returned raw for the app to apply)
// Tolerant: unknown node types and attrs are skipped, choice attrs accept
// label strings, links between missing nodes/ports are dropped.
bool graph_from_ai_spec(Graph &g, const std::string &spec_json, std::string &err,
                        std::string *environment_json_out = nullptr,
                        bool merge = false);

// ---- material library ----
// Serializes a MaterialOutput node together with every node upstream of it
// (the whole material subgraph) into a portable JSON document.
std::string material_to_json(const Graph &g, uint64_t material_node_id);
// Instantiates a saved material into the graph with fresh node ids, placed
// around (x, y). Returns the new MaterialOutput id, or 0 on failure.
uint64_t material_from_json(Graph &g, const std::string &json_text,
                            std::string &err, float x = 0, float y = 0);

// Text catalog of all registered nodes (types, ports, attributes with
// ranges) — used as the AI system prompt so the model knows the schema.
// Pass categories to restrict it (e.g. {"Material","Texture"}).
std::string registry_catalog_for_ai(
    const std::vector<std::string> &categories = {});

} // namespace gpx
