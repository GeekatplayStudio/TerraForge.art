// Geekatplay TerraForge - the asset index. See gpx/asset_index.hpp.
#include "gpx/asset_index.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <map>
#include <sstream>
#include <unordered_map>

using nlohmann::json;
namespace fs = std::filesystem;

namespace gpx {

// ------------------------------------------------------------------ words
std::vector<std::string> asset_tokenize(const std::string &text) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 2 || (cur.size() == 1 && std::isdigit((unsigned char)cur[0])))
      out.push_back(cur);
    cur.clear();
  };
  char prev = 0;
  for (char c : text) {
    unsigned char u = (unsigned char)c;
    if (std::isalnum(u)) {
      // a lower-to-upper step splits CamelCase; a letter-to-digit step splits
      // "Rock02" into "rock" and "02"
      bool split = (std::islower((unsigned char)prev) && std::isupper(u)) ||
                   (std::isalpha((unsigned char)prev) && std::isdigit(u)) ||
                   (std::isdigit((unsigned char)prev) && std::isalpha(u));
      if (split) flush();
      cur.push_back((char)std::tolower(u));
    } else {
      flush();
    }
    prev = c;
  }
  flush();
  return out;
}

std::string asset_pretty_name(const std::string &filename) {
  std::string stem = fs::path(filename).stem().string();
  std::string out;
  char prev = 0;
  for (char c : stem) {
    if (c == '_' || c == '-' || c == '.') {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
    } else {
      if (std::islower((unsigned char)prev) && std::isupper((unsigned char)c) &&
          !out.empty() && out.back() != ' ')
        out.push_back(' ');
      out.push_back(c);
    }
    prev = c;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out.empty() ? stem : out;
}

std::vector<std::string> AssetIndex::tokens_of(const AssetRecord &r) {
  std::vector<std::string> t = asset_tokenize(r.name);
  // the folders it sits in say something too - "rocks/wet/Moss_02"
  fs::path p(r.path);
  int depth = 0;
  for (fs::path q = p.parent_path(); !q.empty() && depth < 3;
       q = q.parent_path(), ++depth) {
    std::vector<std::string> d = asset_tokenize(q.filename().string());
    t.insert(t.end(), d.begin(), d.end());
    if (q == q.parent_path()) break;
  }
  for (const std::string &tag : r.tags) {
    std::vector<std::string> d = asset_tokenize(tag);
    t.insert(t.end(), d.begin(), d.end());
  }
  std::vector<std::string> d = asset_tokenize(r.description);
  t.insert(t.end(), d.begin(), d.end());
  t.push_back(r.kind);
  return t;
}

// ------------------------------------------------------------------- scan
size_t AssetIndex::scan() {
  std::unordered_map<std::string, AssetRecord> kept;
  for (AssetRecord &r : records) kept.emplace(r.id, std::move(r));
  records.clear();
  for (const AssetRoot &root : roots) {
    std::error_code ec;
    if (!fs::is_directory(root.path, ec)) continue;
    fs::path rp(root.path);
    for (fs::recursive_directory_iterator it(rp, ec), end; it != end;
         it.increment(ec)) {
      if (ec) break;
      const fs::directory_entry &e = *it;
      if (!e.is_regular_file(ec)) continue;
      // the trash beside a root is indexed as trashed, never as live
      bool in_trash = false;
      for (fs::path q = e.path().parent_path(); q != rp && !q.empty();
           q = q.parent_path())
        if (q.filename() == "trash") { in_trash = true; break; }
      std::string ext = e.path().extension().string();
      if (!ext.empty()) ext.erase(0, 1);
      for (char &c : ext) c = (char)std::tolower((unsigned char)c);
      if (std::find(root.extensions.begin(), root.extensions.end(), ext) ==
          root.extensions.end())
        continue;
      std::string rel = fs::relative(e.path(), rp, ec).generic_string();
      std::string id = root.kind + "/" + rel;
      AssetRecord r;
      auto old = kept.find(id);
      if (old != kept.end()) r = old->second;
      r.id = id;
      r.kind = root.kind;
      r.path = e.path().string();
      r.name = asset_pretty_name(e.path().filename().string());
      r.size = (uint64_t)e.file_size(ec);
      auto ft = e.last_write_time(ec);
      r.mtime = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
                    ft.time_since_epoch())
                    .count();
      r.trashed = in_trash;
      fs::path thumb = e.path();
      thumb.replace_extension(".png");
      r.thumb = (thumb != e.path() && fs::exists(thumb, ec)) ? thumb.string() : "";
      records.push_back(std::move(r));
    }
  }
  std::sort(records.begin(), records.end(),
            [](const AssetRecord &a, const AssetRecord &b) { return a.id < b.id; });
  rebuild_vectors();
  return records.size();
}

// ---------------------------------------------------------------- vectors
uint32_t AssetIndex::term_id(const std::string &t) const {
  auto it = std::lower_bound(vocab_.begin(), vocab_.end(), t);
  if (it == vocab_.end() || *it != t) return UINT32_MAX;
  return (uint32_t)(it - vocab_.begin());
}

void AssetIndex::rebuild_vectors() {
  // vocabulary
  std::vector<std::vector<std::string>> toks(records.size());
  vocab_.clear();
  for (size_t i = 0; i < records.size(); ++i) {
    toks[i] = tokens_of(records[i]);
    vocab_.insert(vocab_.end(), toks[i].begin(), toks[i].end());
  }
  std::sort(vocab_.begin(), vocab_.end());
  vocab_.erase(std::unique(vocab_.begin(), vocab_.end()), vocab_.end());
  // document frequency -> idf
  std::vector<uint32_t> df(vocab_.size(), 0);
  for (const auto &t : toks) {
    std::vector<uint32_t> seen;
    for (const std::string &w : t) {
      uint32_t id = term_id(w);
      if (id == UINT32_MAX) continue;
      if (std::find(seen.begin(), seen.end(), id) == seen.end()) {
        seen.push_back(id);
        ++df[id];
      }
    }
  }
  const double N = (double)std::max<size_t>(records.size(), 1);
  idf_.resize(vocab_.size());
  for (size_t i = 0; i < vocab_.size(); ++i)
    idf_[i] = (float)(std::log((N + 1.0) / (1.0 + df[i])) + 1.0);
  // tf-idf per record
  vectors_.assign(records.size(), Vec());
  for (size_t i = 0; i < records.size(); ++i) {
    std::map<uint32_t, float> tf;
    for (const std::string &w : toks[i]) {
      uint32_t id = term_id(w);
      if (id != UINT32_MAX) tf[id] += 1.f;
    }
    Vec &v = vectors_[i];
    double n2 = 0;
    for (auto &kv : tf) {
      float w = (1.f + std::log(kv.second)) * idf_[kv.first];
      v.term.push_back(kv.first);
      v.weight.push_back(w);
      n2 += (double)w * w;
    }
    v.norm = (float)std::sqrt(n2);
  }
}

std::vector<AssetHit> AssetIndex::search(const std::string &query, size_t limit,
                                         const std::string &kind,
                                         bool include_trashed) const {
  std::vector<AssetHit> hits;
  std::vector<std::string> q = asset_tokenize(query);
  const std::string lower_query = [&] {
    std::string s = query;
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
  }();
  for (size_t i = 0; i < records.size(); ++i) {
    const AssetRecord &r = records[i];
    if (!kind.empty() && r.kind != kind) continue;
    if (r.trashed && !include_trashed) continue;
    float score = 0.f;
    if (q.empty()) {
      score = 1.f; // an empty query lists everything, newest first
    } else if (i < vectors_.size()) {
      const Vec &v = vectors_[i];
      // cosine between the query (each term weight idf, once) and the record
      double dot = 0, qn = 0;
      for (const std::string &w : q) {
        uint32_t id = term_id(w);
        if (id == UINT32_MAX) {
          // A word the index has never seen still matches by prefix: typing
          // "mos" should already be finding "moss".
          for (size_t k = 0; k < v.term.size(); ++k)
            if (vocab_[v.term[k]].rfind(w, 0) == 0) dot += v.weight[k] * 0.5;
          qn += 1.0;
          continue;
        }
        float qw = idf_[id];
        qn += (double)qw * qw;
        auto it = std::lower_bound(v.term.begin(), v.term.end(), id);
        if (it != v.term.end() && *it == id)
          dot += (double)qw * v.weight[it - v.term.begin()];
        else
          for (size_t k = 0; k < v.term.size(); ++k)
            if (vocab_[v.term[k]].rfind(w, 0) == 0) dot += qw * v.weight[k] * 0.5;
      }
      if (v.norm > 0 && qn > 0) score = (float)(dot / (v.norm * std::sqrt(qn)));
      // the whole phrase inside the name outranks any token mix
      std::string lname = r.name;
      for (char &c : lname) c = (char)std::tolower((unsigned char)c);
      if (lname.find(lower_query) != std::string::npos) score += 0.5f;
    }
    if (score > 0.f) hits.push_back({i, score});
  }
  std::stable_sort(hits.begin(), hits.end(), [&](const AssetHit &a, const AssetHit &b) {
    if (a.score != b.score) return a.score > b.score;
    return records[a.index].mtime > records[b.index].mtime;
  });
  if (hits.size() > limit) hits.resize(limit);
  return hits;
}

// ------------------------------------------------------------- record ops
const AssetRecord *AssetIndex::find(const std::string &id) const {
  for (const AssetRecord &r : records)
    if (r.id == id) return &r;
  return nullptr;
}
AssetRecord *AssetIndex::find(const std::string &id) {
  for (AssetRecord &r : records)
    if (r.id == id) return &r;
  return nullptr;
}

bool AssetIndex::add_tag(const std::string &id, const std::string &tag) {
  AssetRecord *r = find(id);
  if (!r || tag.empty()) return false;
  if (std::find(r->tags.begin(), r->tags.end(), tag) == r->tags.end())
    r->tags.push_back(tag);
  rebuild_vectors();
  return true;
}

bool AssetIndex::remove_tag(const std::string &id, const std::string &tag) {
  AssetRecord *r = find(id);
  if (!r) return false;
  auto it = std::find(r->tags.begin(), r->tags.end(), tag);
  if (it == r->tags.end()) return false;
  r->tags.erase(it);
  rebuild_vectors();
  return true;
}

bool AssetIndex::set_description(const std::string &id, const std::string &text) {
  AssetRecord *r = find(id);
  if (!r) return false;
  r->description = text;
  rebuild_vectors();
  return true;
}

namespace {
bool move_beside_root(AssetRecord &r, const std::vector<AssetRoot> &roots,
                      bool into_trash) {
  std::error_code ec;
  fs::path p(r.path);
  const AssetRoot *root = nullptr;
  for (const AssetRoot &rt : roots)
    if (rt.kind == r.kind) root = &rt;
  if (!root) return false;
  fs::path trash = fs::path(root->path) / "trash";
  fs::path dest = into_trash ? trash / p.filename() : fs::path(root->path) / p.filename();
  if (into_trash) fs::create_directories(trash, ec);
  fs::rename(p, dest, ec);
  if (ec) return false;
  if (!r.thumb.empty()) {
    fs::path t(r.thumb);
    fs::path td = dest;
    td.replace_extension(".png");
    fs::rename(t, td, ec);
    r.thumb = ec ? "" : td.string();
  }
  r.path = dest.string();
  r.trashed = into_trash;
  return true;
}
} // namespace

bool AssetIndex::trash(const std::string &id) {
  AssetRecord *r = find(id);
  return r && !r->trashed && move_beside_root(*r, roots, true);
}

bool AssetIndex::restore(const std::string &id) {
  AssetRecord *r = find(id);
  return r && r->trashed && move_beside_root(*r, roots, false);
}

// ------------------------------------------------------------------- disk
bool AssetIndex::save(const std::string &file) const {
  json j;
  j["format"] = "terraforge-assets";
  j["version"] = 1;
  json roots_j = json::array();
  for (const AssetRoot &r : roots)
    roots_j.push_back({{"path", r.path}, {"kind", r.kind}, {"extensions", r.extensions}});
  j["roots"] = roots_j;
  json recs = json::array();
  for (const AssetRecord &r : records) {
    json o = {{"id", r.id},       {"kind", r.kind},   {"path", r.path},
              {"name", r.name},   {"thumb", r.thumb}, {"tags", r.tags},
              {"description", r.description}, {"size", r.size},
              {"mtime", r.mtime}, {"trashed", r.trashed}};
    if (!r.embedding.empty()) o["embedding"] = r.embedding;
    recs.push_back(o);
  }
  j["records"] = recs;
  std::ofstream f(file, std::ios::binary);
  if (!f) return false;
  f << j.dump(1);
  return (bool)f;
}

bool AssetIndex::load(const std::string &file) {
  std::ifstream f(file, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  json j = json::parse(ss.str(), nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  roots.clear();
  for (const auto &r : j.value("roots", json::array())) {
    AssetRoot root;
    root.path = r.value("path", "");
    root.kind = r.value("kind", "other");
    for (const auto &e : r.value("extensions", json::array()))
      if (e.is_string()) root.extensions.push_back(e.get<std::string>());
    roots.push_back(root);
  }
  records.clear();
  for (const auto &o : j.value("records", json::array())) {
    AssetRecord r;
    r.id = o.value("id", "");
    r.kind = o.value("kind", "other");
    r.path = o.value("path", "");
    r.name = o.value("name", "");
    r.thumb = o.value("thumb", "");
    for (const auto &t : o.value("tags", json::array()))
      if (t.is_string()) r.tags.push_back(t.get<std::string>());
    r.description = o.value("description", "");
    r.size = o.value("size", 0ull);
    r.mtime = o.value("mtime", 0ll);
    r.trashed = o.value("trashed", false);
    for (const auto &e : o.value("embedding", json::array()))
      if (e.is_number()) r.embedding.push_back(e.get<float>());
    if (!r.id.empty()) records.push_back(std::move(r));
  }
  rebuild_vectors();
  return true;
}

} // namespace gpx
