// Geekatplay TerraForge — PBRMaterial node: downloads CC0 photoscanned PBR
// material sets from the ambientCG public API (https://docs.ambientcg.com),
// caches them locally, and outputs albedo/normal/roughness/AO textures.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "miniz/miniz.h"
#include "stb_image.h" // implementation lives in nodes_materials.cpp

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <urlmon.h>
#endif

namespace fs = std::filesystem;

namespace gpx {

static fs::path material_cache_dir() {
#ifdef _WIN32
  const char *base = std::getenv("LOCALAPPDATA");
  fs::path dir = base ? fs::path(base) : fs::temp_directory_path();
#else
  const char *base = std::getenv("HOME");
  fs::path dir = base ? fs::path(base) / ".cache" : fs::temp_directory_path();
#endif
  dir /= "GeekatplayTerraForge";
  dir /= "materials";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

static bool download_url(const std::string &url, const fs::path &dest) {
#ifdef _WIN32
  return URLDownloadToFileA(nullptr, url.c_str(), dest.string().c_str(), 0,
                            nullptr) == S_OK;
#else
  std::string cmd = "curl -L -s -o \"" + dest.string() + "\" \"" + url + "\"";
  return std::system(cmd.c_str()) == 0;
#endif
}

// extract a zip into a directory; returns extracted file names
static bool extract_zip(const fs::path &zip, const fs::path &into,
                        std::string &err) {
  mz_zip_archive za{};
  if (!mz_zip_reader_init_file(&za, zip.string().c_str(), 0)) {
    err = "zip open failed";
    return false;
  }
  int count = (int)mz_zip_reader_get_num_files(&za);
  for (int i = 0; i < count; ++i) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&za, i, &st)) continue;
    if (mz_zip_reader_is_file_a_directory(&za, i)) continue;
    fs::path out = into / fs::path(st.m_filename).filename();
    mz_zip_reader_extract_to_file(&za, i, out.string().c_str(), 0);
  }
  mz_zip_reader_end(&za);
  return true;
}

// find the map file for a channel in an extracted material folder
static fs::path find_map(const fs::path &dir, const std::vector<std::string> &keys) {
  std::error_code ec;
  for (auto &e : fs::directory_iterator(dir, ec)) {
    std::string name = e.path().filename().string();
    std::string lower = name;
    for (auto &c : lower) c = (char)tolower(c);
    for (const auto &k : keys)
      if (lower.find(k) != std::string::npos &&
          (lower.ends_with(".jpg") || lower.ends_with(".png")))
        return e.path();
  }
  return {};
}

static bool load_into(const fs::path &file, TextureRGBA &out, int mapping,
                      float tiles) {
  if (file.empty()) return false;
  int iw, ih, comp;
  unsigned char *data = stbi_load(file.string().c_str(), &iw, &ih, &comp, 4);
  if (!data) return false;
  parallel_rows(out.h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < out.w; ++x) {
        float u = x / float(out.w), v = y / float(out.h);
        if (mapping == 1) {
          u *= tiles;
          v *= tiles;
          u -= std::floor(u);
          v -= std::floor(v);
        }
        int sx = std::min((int)(u * iw), iw - 1);
        int sy = std::min((int)(v * ih), ih - 1);
        const unsigned char *sp = &data[((size_t)sy * iw + sx) * 4];
        float *dp = out.px(x, y);
        for (int k = 0; k < 4; ++k) dp[k] = sp[k] / 255.f;
      }
  });
  stbi_image_free(data);
  return true;
}

REGISTER_NODE(
    PBRMaterial, "Material",
    "Download a CC0 photoscanned PBR material set from ambientCG (albedo/normal/roughness/AO)",
    [](Node &n) {
      n.add_out("albedo", DataType::Texture);
      n.add_out("normal", DataType::Texture);
      n.add_out("roughness", DataType::Texture);
      n.add_out("ao", DataType::Texture);
      add_text(n.attrs, "asset", "ambientCG asset ID", "Rock035");
      add_choice(n.attrs, "resolution", "Resolution", {"1K", "2K", "4K", "8K"}, 1);
      add_choice(n.attrs, "mapping", "Mapping", {"Stretch", "Tile"}, 1);
      add_float(n.attrs, "tiles", "Tiles across", 8.f, 1.f, 64.f);
    },
    [](Node &n) {
      std::string asset = n.attrs.get_s("asset");
      if (asset.empty()) {
        n.error = "set an ambientCG asset ID (browse ambientcg.com)";
        return;
      }
      const char *res_names[4] = {"1K", "2K", "4K", "8K"};
      std::string res = res_names[std::clamp(n.attrs.get_choice("resolution"), 0, 3)];
      std::string file_id = asset + "_" + res + "-JPG";
      fs::path dir = material_cache_dir() / file_id;
      fs::path marker = dir / ".complete";
      if (!fs::exists(marker)) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path zip = dir / (file_id + ".zip");
        std::string url = "https://ambientcg.com/get?file=" + file_id + ".zip";
        if (!download_url(url, zip)) {
          n.error = "download failed: " + url;
          return;
        }
        std::string err;
        if (!extract_zip(zip, dir, err)) {
          n.error = err;
          return;
        }
        fs::remove(zip, ec);
        std::FILE *f = std::fopen(marker.string().c_str(), "w");
        if (f) std::fclose(f);
      }
      int mapping = n.attrs.get_choice("mapping");
      float tiles = n.attrs.get_f("tiles", 8.f);
      TextureRGBA &alb = n.out_tex("albedo");
      TextureRGBA &nrm = n.out_tex("normal");
      TextureRGBA &rgh = n.out_tex("roughness");
      TextureRGBA &ao = n.out_tex("ao");
      bool ok = load_into(find_map(dir, {"_color", "color."}), alb, mapping, tiles);
      load_into(find_map(dir, {"normalgl"}), nrm, mapping, tiles);
      load_into(find_map(dir, {"roughness"}), rgh, mapping, tiles);
      load_into(find_map(dir, {"ambientocclusion", "_ao"}), ao, mapping, tiles);
      if (!ok) n.error = "no color map found in " + file_id;
    })

} // namespace gpx
