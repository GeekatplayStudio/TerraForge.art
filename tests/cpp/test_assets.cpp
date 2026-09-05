// Geekatplay TerraForge - the asset index (engine/asset_index.cpp), tested
// against a folder built here so every assertion can name the file it means.
//
// What must hold: a scan finds what is there and nothing else; a search by
// meaning finds "Moss_Rock_02" from "mossy rock" and ranks the exact name
// first; tags and notes change what is found and survive a rescan; trash
// moves rather than deletes and restore brings it back; and the whole thing
// round-trips through its JSON file.
#include "gpx/asset_index.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace gpx;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static void touch(const fs::path &p, const char *text = "x") {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f << text;
}

static bool has(const std::vector<std::string> &v, const char *s) {
  for (const std::string &x : v)
    if (x == s) return true;
  return false;
}

static void test_tokenizer() {
  std::vector<std::string> t = asset_tokenize("Moss_Rock_02.gpxmat");
  check(has(t, "moss") && has(t, "rock") && has(t, "02") && has(t, "gpxmat"),
        "underscores split, digits are their own token");
  t = asset_tokenize("WetSandstoneCliff");
  check(has(t, "wet") && has(t, "sandstone") && has(t, "cliff"),
        "CamelCase splits into words");
  t = asset_tokenize("a b");
  check(t.empty(), "single letters are not words");
  check(asset_pretty_name("Moss_Rock_02.gpxmat") == "Moss Rock 02", "a file name reads as a name");
  check(asset_pretty_name("WetSandstone.png") == "Wet Sandstone", "CamelCase reads as words");
}

static void test_scan_search_tags_trash() {
  fs::path dir = fs::temp_directory_path() / "terraforge_asset_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::path mats = dir / "materials";
  touch(mats / "Moss_Rock_02.gpxmat");
  touch(mats / "Moss_Rock_02.png");
  touch(mats / "Dry_Sand.gpxmat");
  touch(mats / "wet" / "River_Pebbles.gpxmat");
  touch(mats / "notes.txt"); // not an asset
  fs::path meshes = dir / "meshes";
  touch(meshes / "old_boulder.stl");

  AssetIndex ix;
  ix.roots.push_back({mats.string(), "material", {"gpxmat"}});
  ix.roots.push_back({meshes.string(), "mesh", {"stl", "obj"}});
  size_t n = ix.scan();
  check(n == 4, "a scan finds the four assets and skips the text file");
  const AssetRecord *moss = ix.find("material/Moss_Rock_02.gpxmat");
  check(moss != nullptr, "records are keyed by kind and relative path");
  check(moss && moss->name == "Moss Rock 02", "the name is the pretty file stem");
  check(moss && !moss->thumb.empty(), "a PNG beside the file is its thumbnail");
  const AssetRecord *peb = ix.find("material/wet/River_Pebbles.gpxmat");
  check(peb != nullptr, "subfolders are walked");

  // search by meaning
  std::vector<AssetHit> hits = ix.search("mossy rock");
  check(!hits.empty() && ix.records[hits[0].index].id == moss->id,
        "\"mossy rock\" finds Moss Rock 02 first");
  hits = ix.search("boulder");
  check(!hits.empty() && ix.records[hits[0].index].kind == "mesh",
        "\"boulder\" finds the mesh");
  hits = ix.search("wet");
  check(!hits.empty() && ix.records[hits[0].index].id == peb->id,
        "the folder an asset sits in is part of what it is known by");
  hits = ix.search("riv");
  check(!hits.empty() && ix.records[hits[0].index].id == peb->id,
        "a prefix matches while it is still being typed");
  hits = ix.search("mossy rock", 50, "mesh");
  check(hits.empty(), "a kind filter excludes the other kinds");
  hits = ix.search("");
  check(hits.size() == 4, "an empty query lists everything");

  // tags change what is found, and survive a rescan
  check(ix.add_tag(moss->id, "lichen boulder"), "a tag can be added");
  hits = ix.search("lichen");
  check(!hits.empty() && ix.records[hits[0].index].id == moss->id,
        "a tagged word finds the asset");
  check(ix.set_description(peb->id, "smooth stones for a stream bed"), "a note can be written");
  hits = ix.search("stream bed");
  check(!hits.empty() && ix.records[hits[0].index].id == peb->id,
        "words in the note find the asset");
  ix.scan();
  check(ix.find(moss->id) && has(ix.find(moss->id)->tags, "lichen boulder"),
        "tags survive a rescan");
  check(ix.find(peb->id) && ix.find(peb->id)->description == "smooth stones for a stream bed",
        "notes survive a rescan");

  // trash moves, never deletes; restore brings it back
  std::string sand_id = "material/Dry_Sand.gpxmat";
  check(ix.trash(sand_id), "an asset can be trashed");
  check(fs::exists(mats / "trash" / "Dry_Sand.gpxmat"), "the file moved into the trash folder");
  check(!fs::exists(mats / "Dry_Sand.gpxmat"), "and left where it was");
  hits = ix.search("sand");
  check(hits.empty(), "a trashed asset is not found by default");
  hits = ix.search("sand", 50, "", true);
  check(!hits.empty(), "but is found when trash is included");
  check(ix.restore(sand_id), "an asset can be restored");
  check(fs::exists(mats / "Dry_Sand.gpxmat"), "and its file is back");

  // the index file round-trips what the scan cannot rebuild
  fs::path file = dir / "index.json";
  check(ix.save(file.string()), "the index saves");
  AssetIndex back;
  check(back.load(file.string()), "and loads");
  check(back.records.size() == ix.records.size(), "with every record");
  check(back.find(moss->id) && has(back.find(moss->id)->tags, "lichen boulder"),
        "tags round-trip");
  check(back.roots.size() == 2, "roots round-trip");
  hits = back.search("lichen");
  check(!hits.empty() && back.records[hits[0].index].id == moss->id,
        "a loaded index searches without a rescan");

  // a file that vanishes vanishes from the index on the next scan
  fs::remove(meshes / "old_boulder.stl", ec);
  ix.scan();
  check(ix.find("mesh/old_boulder.stl") == nullptr, "a deleted file leaves the index");
  fs::remove_all(dir, ec);
}

int main() {
  test_tokenizer();
  test_scan_search_tags_trash();
  if (failures) {
    std::printf("%d asset check(s) failed\n", failures);
    return 1;
  }
  std::printf("asset tests passed\n");
  return 0;
}
