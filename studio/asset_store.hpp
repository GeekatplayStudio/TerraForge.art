// Geekatplay TerraForge - the asset manager's studio side: one index for the
// application, the roots it watches, its file on disk, and what "open"
// means for each kind of asset.
//
// The engine's AssetIndex (engine/gpx/asset_index.hpp) knows folders and
// words. This layer knows the application: that the material library, the
// saved layouts and the user's mesh folders are the default roots, that the
// index lives beside the material library, that opening a material means
// loading it into the graph and opening a mesh means importing it.
#pragma once
#include "gpx/asset_index.hpp"
#include <string>
#include <vector>

namespace studio {

struct App;

// The application's index. First use loads the file (or scans the default
// roots when there is none).
gpx::AssetIndex &asset_index();

// Where the index file lives: <LOCALAPPDATA>/GeekatplayTerraForge/assets.json.
std::string asset_index_file();

// Rescan every root and save. Returns the record count.
size_t asset_rescan();

// Save the index (tags, notes, roots) now.
bool asset_save();

// Add a folder to watch. `kind` is material, texture, mesh, layout, macro or
// other; extensions default per kind when empty. Rescans. False when the
// folder does not exist.
bool asset_add_root(const std::string &path, const std::string &kind,
                    std::string &err);
bool asset_remove_root(const std::string &path);

// The default extensions of a kind, lower-case without the dot.
std::vector<std::string> asset_kind_extensions(const std::string &kind);

// Open an asset in the application: a material is loaded into the project
// (and opened in the studio), a mesh is imported as an object, a layout is
// applied, a texture is reported by path for the caller to use. False with
// `err` set when the kind cannot be opened.
bool asset_open(App &a, const std::string &id, std::string &err);

// A GL texture for the asset's thumbnail, loaded on first use and kept.
// Zero when it has none.
unsigned asset_thumb_texture(const gpx::AssetRecord &r);

// Drop every cached thumbnail texture (after a rescan, so replaced files
// show their new picture).
void asset_thumbs_clear();

// The Assets tab of the Material Browser (panel_assets.cpp).
void draw_assets_tab(App &a, float cell);
// A scripted search shows its results: the browser selects the Assets tab
// on the next frame, with the query in the box.
void assets_tab_show(const std::string &query);
bool assets_tab_take_focus();

} // namespace studio
