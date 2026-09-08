// Geekatplay TerraForge - ask the node catalogue what a phrase finds.
//
// Node search generalises exactly as far as the concept table in
// engine/node_search_terms.cpp does, and the only way to know whether a
// phrase reaches the node somebody had in mind is to ask it. Doing that
// through the UI means launching the application and typing; this prints the
// ranking for any number of phrases in one go, which is what makes widening
// the vocabulary a measurable change rather than a hopeful one.
//
//   query_search "paint on terrain" "wear down the mountains"
//
// With no arguments it runs a standing set of phrases that have been wrong
// before, so a regression shows up without anyone having to remember them.
#include "gpx/node_search.hpp"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  std::vector<std::string> queries;
  for (int i = 1; i < argc; ++i) queries.push_back(argv[i]);
  if (queries.empty())
    queries = {"paint", "paint on terrain", "draw", "brush", "sculpt",
               "rocks", "wear down the mountains", "clouds at sunset",
               "make an island", "scatter trees"};

  for (const std::string &q : queries) {
    std::printf("\n== %s ==\n", q.c_str());
    const auto hits = gpx::search::find_nodes(q, 8);
    if (hits.empty()) {
      std::printf("  (nothing)\n");
      continue;
    }
    for (const auto &h : hits)
      std::printf("  %-22s %-30s %.3f\n", h.type.c_str(), h.label.c_str(),
                  h.score);
    const auto words = gpx::search::expand_query(q);
    if (words.size() > 1) {
      std::printf("  also searched:");
      for (size_t i = 1; i < words.size() && i < 12; ++i)
        std::printf(" %s", words[i].c_str());
      std::printf("\n");
    }
  }
  return 0;
}
