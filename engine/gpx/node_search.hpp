// Geekatplay TerraForge - finding a node by describing it.
//
// The library's filter was a substring match on the type name, which finds a
// node only if you already know what it is called. The two things that need
// to find a node are a person who knows what they want ("rocks", "make the
// hills wear down") and the assistant turning a sentence into a graph. Both
// need the same thing: a search that answers from meaning rather than
// spelling.
//
// What this is, stated plainly: **a vector space over the node catalogue**.
// Every node becomes a sparse TF-IDF vector over the words in its name,
// label, category, description and parameter text; a query becomes a vector
// the same way; the score is the cosine between them. A curated concept table
// expands the query first, so "rocks" reaches the nodes that say "stone",
// "boulder", "scree" and "talus" without any of them sharing a word with it.
//
// What it is not: it is not a neural embedding, so it generalises only as far
// as the concept table takes it. That is a deliberate trade - it is
// deterministic, it needs no model, no network and no Python, it builds in
// milliseconds from the registry itself, and it can be tested. `find_nodes`
// is the seam an embedding backend would slot behind if that ever changes.
#pragma once
#include <string>
#include <vector>

namespace gpx::search {

struct Hit {
  std::string type;  // the registry identity, for actually adding the node
  std::string label; // what the user reads
  float score = 0.f; // cosine, 0..1
};

// Best matches for a natural-language query, strongest first. An empty query
// returns nothing rather than everything: "no filter" is the caller's state
// to represent, not a search result.
std::vector<Hit> find_nodes(const std::string &query, int limit = 12);

// Every word the query expanded into, for explaining a result to the user
// ("also searched: stone, boulder, scree"). Exposed for the tests too.
std::vector<std::string> expand_query(const std::string &query);

// Drops the cached index. The catalogue is fixed at run time, so this exists
// for the tests rather than for the application.
void reset_index();

} // namespace gpx::search
