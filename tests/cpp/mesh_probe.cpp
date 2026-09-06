// Geekatplay TerraForge - mesh_probe: load model files with the engine's
// readers and report what came out. A diagnostic for "import not working":
//   mesh_probe <file> [<file> ...]
// prints vertices, faces, uvs, parts and pictures for each, or the reader's
// error, and exits non-zero if any failed.
#include "gpx/mesh_io.hpp"
#include <chrono>
#include <cstdio>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: mesh_probe <model file> ...\n");
    return 2;
  }
  int failures = 0;
  for (int i = 1; i < argc; ++i) {
    gpx::TriMesh m;
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = gpx::mesh_load(argv[i], m, err);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s\n", argv[i]);
    if (!ok) {
      std::printf("  FAILED: %s\n", err.c_str());
      ++failures;
      continue;
    }
    std::printf("  %zu vertices, %zu faces, uv %s, %zu parts, %zu pictures, %.0f ms\n", m.vert_count(),
                m.face_count(), m.uv.empty() ? "no" : "yes", m.parts.size(), m.images.size(), ms);
    for (size_t p = 0; p < m.parts.size() && p < 12; ++p)
      std::printf("    part '%s': %u faces, image %d, colour %.2f %.2f %.2f\n", m.parts[p].name.c_str(),
                  m.parts[p].face_count, m.parts[p].image, m.parts[p].color[0], m.parts[p].color[1],
                  m.parts[p].color[2]);
    for (size_t k = 0; k < m.images.size() && k < 12; ++k)
      std::printf("    image '%s': %zu bytes embedded%s%s\n", m.images[k].name.c_str(), m.images[k].bytes.size(),
                  m.images[k].path.empty() ? "" : ", file ", m.images[k].path.c_str());
    float lo[3], hi[3];
    if (gpx::mesh_bounds(m, lo, hi))
      std::printf("    bounds %.3f..%.3f  %.3f..%.3f  %.3f..%.3f\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
  }
  return failures ? 1 : 0;
}
