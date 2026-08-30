// Geekatplay Studio — graph <-> JSON project files
#pragma once
#include "gpx/node_graph.hpp"
#include <string>

namespace gpx {

std::string graph_to_json(const Graph &g);
bool graph_from_json(Graph &g, const std::string &json_text, std::string &err);
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
                        std::string *environment_json_out = nullptr);

// Text catalog of all registered nodes (types, ports, attributes with
// ranges) — used as the AI system prompt so the model knows the schema.
std::string registry_catalog_for_ai();

} // namespace gpx
