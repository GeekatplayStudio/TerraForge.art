// Geekatplay TerraForge - the node search index.
//
// A vector space over the catalogue: each node is a sparse TF-IDF vector over
// the words in its name, label, category, description and parameter text; a
// query is turned into one the same way and the score is the cosine between
// them. See gpx/node_search.hpp for what this is and is not.
//
// Three things make the results usable rather than merely computable:
//
//   - **fields are weighted.** A word in the display name says far more about
//     a node than the same word buried in a tooltip. Without this, a node
//     with forty parameters outranks the node actually named after the thing
//     you searched for, because it mentions the word more times.
//   - **the query expands through the concept table** before scoring, so
//     "rocks" reaches nodes that only ever say "stone" or "scree".
//   - **IDF is computed over the corpus**, so "terrain" - which nearly every
//     node mentions - stops being evidence, while "caldera" is decisive.
#include "gpx/node_search.hpp"
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace gpx::search {

const std::vector<std::vector<std::string>> &concept_rows();
const std::vector<std::string> &stop_words();

namespace {

// How much a word is worth by where it was found. A name is a claim about
// what the node is; a tooltip is a remark about one of its dials.
constexpr float W_LABEL = 6.f;
constexpr float W_TYPE = 4.f;
constexpr float W_CATEGORY = 2.f;
constexpr float W_DESCRIPTION = 2.f;
constexpr float W_PARAM = 1.f;

// Crude but predictable stemming: enough to join "rocks" to "rock" and
// "eroding" to "erode" without a dictionary. Deliberately not aggressive -
// over-stemming collides unrelated words and there is no way to see it happen.
std::string stem(std::string w) {
  auto ends = [&](const char *s) {
    const size_t n = std::strlen(s);
    return w.size() > n + 2 && w.compare(w.size() - n, n, s) == 0;
  };
  if (ends("ing")) w.resize(w.size() - 3);
  else if (ends("ed")) w.resize(w.size() - 2);
  else if (ends("es")) w.resize(w.size() - 2);
  else if (ends("s") && w[w.size() - 2] != 's') w.resize(w.size() - 1);
  return w;
}

std::vector<std::string> tokenise(const std::string &s) {
  static const std::set<std::string> stops(stop_words().begin(),
                                           stop_words().end());
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 2 && !stops.count(cur)) out.push_back(stem(cur));
    cur.clear();
  };
  for (char c : s) {
    if (std::isalnum((unsigned char)c)) {
      cur += (char)std::tolower((unsigned char)c);
    } else {
      flush();
    }
  }
  flush();
  return out;
}

// A type name is one word to the tokeniser ("fieldstones"), which would match
// nothing. Split the case first so its words count individually.
std::string split_identifier(const std::string &t) {
  std::string o;
  for (size_t i = 0; i < t.size(); ++i) {
    if (i && std::isupper((unsigned char)t[i]) &&
        !std::isupper((unsigned char)t[i - 1]))
      o += ' ';
    o += t[i];
  }
  return o;
}

struct Doc {
  std::string type, label;
  std::unordered_map<std::string, float> tf; // stemmed word -> weight
  float norm = 1.f;
};

struct Index {
  std::vector<Doc> docs;
  std::unordered_map<std::string, float> idf;
  // stemmed word -> every word in the concept rows it belongs to, stemmed
  std::unordered_map<std::string, std::vector<std::string>> concepts;
  // stemmed word -> a real word to show for it. Scoring works on stems, but
  // a stem is not a word: telling the user it "also searched: ston, talu,
  // lak" is worse than telling them nothing.
  std::unordered_map<std::string, std::string> spelling;
};

void add(Doc &d, const std::string &text, float weight) {
  for (const std::string &w : tokenise(text)) d.tf[w] += weight;
}

Index build() {
  Index ix;
  Graph g;
  for (const NodeDef *def : NodeRegistry::instance().all()) {
    Node *n = g.add_node(def->type, 0, 0);
    if (!n) continue;
    Doc d;
    d.type = def->type;
    d.label = node_display_name(def->type);
    add(d, d.label, W_LABEL);
    add(d, split_identifier(def->type), W_TYPE);
    add(d, def->category, W_CATEGORY);
    add(d, def->description, W_DESCRIPTION);
    for (const Attribute &a : n->attrs.items) {
      add(d, a.label, W_PARAM);
      add(d, a.tooltip, W_PARAM);
    }
    for (const Port &p : n->ports) add(d, p.name, W_PARAM);
    ix.docs.push_back(std::move(d));
  }

  // Inverse document frequency: a word every node uses is not evidence.
  std::unordered_map<std::string, int> df;
  for (const Doc &d : ix.docs)
    for (const auto &[w, _] : d.tf) ++df[w];
  const float N = (float)std::max<size_t>(ix.docs.size(), 1);
  for (const auto &[w, c] : df)
    ix.idf[w] = std::log(1.f + N / (float)(1 + c));

  // TF-IDF, then L2-normalise so a wordy node does not outrank a precise one
  // simply for being wordy - which is the whole point of the cosine.
  for (Doc &d : ix.docs) {
    float sum = 0.f;
    for (auto &[w, v] : d.tf) {
      v *= ix.idf[w];
      sum += v * v;
    }
    d.norm = std::sqrt(sum);
    if (d.norm < 1e-9f) d.norm = 1.f;
  }

  for (const auto &row : concept_rows()) {
    std::vector<std::string> stemmed;
    stemmed.reserve(row.size());
    for (const std::string &w : row) {
      const std::string st = stem(w);
      stemmed.push_back(st);
      // the shortest real spelling of a stem reads best: "stone" over
      // "stones", "talus" over "taluses"
      auto it = ix.spelling.find(st);
      if (it == ix.spelling.end() || w.size() < it->second.size())
        ix.spelling[st] = w;
    }
    for (const std::string &w : stemmed) {
      auto &v = ix.concepts[w];
      for (const std::string &o : stemmed)
        if (o != w && std::find(v.begin(), v.end(), o) == v.end())
          v.push_back(o);
    }
  }
  return ix;
}

std::mutex g_mtx;
Index *g_index = nullptr;

const Index &index() {
  if (!g_index) g_index = new Index(build());
  return *g_index;
}

} // namespace

void reset_index() {
  std::lock_guard<std::mutex> lk(g_mtx);
  delete g_index;
  g_index = nullptr;
}

std::vector<std::string> expand_query(const std::string &query) {
  std::lock_guard<std::mutex> lk(g_mtx);
  const Index &ix = index();
  // Shown to the user, so every entry is a word rather than a stem, and each
  // appears once however many rows reached it.
  auto spell = [&](const std::string &st) {
    auto it = ix.spelling.find(st);
    return it == ix.spelling.end() ? st : it->second;
  };
  std::vector<std::string> out;
  auto push = [&](const std::string &w) {
    if (std::find(out.begin(), out.end(), w) == out.end()) out.push_back(w);
  };
  for (const std::string &w : tokenise(query)) {
    push(spell(w));
    auto it = ix.concepts.find(w);
    if (it == ix.concepts.end()) continue;
    for (const std::string &o : it->second) push(spell(o));
  }
  return out;
}

std::vector<Hit> find_nodes(const std::string &query, int limit) {
  std::lock_guard<std::mutex> lk(g_mtx);
  const Index &ix = index();
  const std::vector<std::string> words = tokenise(query);
  if (words.empty()) return {};

  // The query vector. A word the user actually typed counts full; a word the
  // concept table reached for counts less, so an exact hit always outranks an
  // associated one - searching "stone" must put the stone nodes above the
  // gravel ones, not merely among them.
  std::unordered_map<std::string, float> q;
  for (const std::string &w : words) {
    q[w] += 1.f;
    auto it = ix.concepts.find(w);
    if (it == ix.concepts.end()) continue;
    for (const std::string &o : it->second) q[o] += 0.45f;
  }
  float qn = 0.f;
  for (auto &[w, v] : q) {
    auto f = ix.idf.find(w);
    v *= (f == ix.idf.end() ? 0.f : f->second);
    qn += v * v;
  }
  qn = std::sqrt(qn);
  if (qn < 1e-9f) return {};

  std::vector<Hit> hits;
  for (const Doc &d : ix.docs) {
    float dot = 0.f;
    for (const auto &[w, qv] : q) {
      auto it = d.tf.find(w);
      if (it != d.tf.end()) dot += qv * it->second;
    }
    if (dot <= 0.f) continue;
    hits.push_back({d.type, d.label, dot / (qn * d.norm)});
  }
  std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
    if (a.score != b.score) return a.score > b.score;
    return a.type < b.type; // stable and reproducible, never hash order
  });
  if (limit > 0 && (int)hits.size() > limit) hits.resize((size_t)limit);
  return hits;
}

} // namespace gpx::search
