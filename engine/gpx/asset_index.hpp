// Geekatplay TerraForge - the asset index: everything the studio can load,
// findable.
//
// Materials, texture sets, meshes, layouts, macros, saved nodes - they all
// live in folders, and a folder is not a library until you can ask it a
// question. This index walks the roots it is given, records what it finds
// with the words that describe it, and answers searches by meaning-of-words
// rather than exact name: "mossy rock" finds "Moss_Rock_02" and a material
// tagged "lichen boulder".
//
// The search is a vector search in the honest sense: every record becomes a
// TF-IDF vector over its tokens and a query is scored by cosine similarity.
// No model, no service, no dependency - deterministic, a few microseconds
// per record, and good for the few thousand assets a studio holds. Records
// carry an `embedding` slot so an external model (the MCP side has one) can
// add semantic vectors later without changing a caller.
//
// Nothing here deletes. Trash moves a file to a trash folder beside its root
// and marks the record; that is the ComfyUI Asset Vault rule, kept.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gpx {

struct AssetRecord {
  std::string id;          // stable: kind + "/" + relative path
  std::string kind;        // material, texture, mesh, layout, macro, node, other
  std::string path;        // absolute
  std::string name;        // display name (file stem, prettified)
  std::string thumb;       // a PNG beside it, if any
  std::vector<std::string> tags;
  std::string description; // free text the user wrote
  uint64_t size = 0;
  int64_t mtime = 0;       // seconds
  bool trashed = false;
  // optional semantic vector from an external model; empty means none
  std::vector<float> embedding;
};

struct AssetHit {
  size_t index;
  float score;
};

struct AssetRoot {
  std::string path;
  std::string kind;
  std::vector<std::string> extensions; // lower-case, without the dot
};

class AssetIndex {
public:
  std::vector<AssetRecord> records;
  std::vector<AssetRoot> roots;

  // Walk every root; keeps tags and descriptions of records already known,
  // drops records whose files are gone. Returns how many records exist.
  size_t scan();

  // Search by words. `kind` empty means every kind. Trashed records are
  // returned only when asked for.
  std::vector<AssetHit> search(const std::string &query, size_t limit = 50,
                               const std::string &kind = "",
                               bool include_trashed = false) const;

  const AssetRecord *find(const std::string &id) const;
  AssetRecord *find(const std::string &id);

  // Tags and notes, kept across rescans.
  bool add_tag(const std::string &id, const std::string &tag);
  bool remove_tag(const std::string &id, const std::string &tag);
  bool set_description(const std::string &id, const std::string &text);

  // Move the file (and its thumbnail) to <root>/trash and mark the record.
  // `restore` brings it back. Neither deletes anything.
  bool trash(const std::string &id);
  bool restore(const std::string &id);

  // The index on disk, one JSON file. Tags, descriptions and embeddings are
  // what it exists to keep; the scan can always rebuild the rest.
  bool save(const std::string &file) const;
  bool load(const std::string &file);

  // The words an asset is known by - exposed so a caller can show why a hit
  // matched.
  static std::vector<std::string> tokens_of(const AssetRecord &r);

private:
  // TF-IDF vectors, rebuilt after scan/load/tag; a sparse map per record.
  struct Vec {
    std::vector<uint32_t> term;
    std::vector<float> weight;
    float norm = 0.f;
  };
  std::vector<Vec> vectors_;
  std::vector<std::string> vocab_;
  std::vector<float> idf_;
  void rebuild_vectors();
  uint32_t term_id(const std::string &t) const;
};

// The tokens of any text: lower-cased, split on punctuation and on
// CamelCase, digits kept as their own tokens. Public because the search
// and the tests both need to agree on it.
std::vector<std::string> asset_tokenize(const std::string &text);

// A file name made readable: "Moss_Rock_02.gpxmat" -> "Moss Rock 02".
std::string asset_pretty_name(const std::string &filename);

} // namespace gpx
