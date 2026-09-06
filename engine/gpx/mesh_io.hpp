// Geekatplay TerraForge - reading and writing meshes.
//
// The formats a terrain and print workflow actually needs, each written by
// hand so there is no dependency and no licence to inherit: OBJ (the DCC
// lingua franca, with its MTL pictures), FBX and glTF (what every DCC and
// every generation service exports, with uvs and textures), STL (what every
// slicer eats), PLY (scanners) and OFF.
//
// Everything is triangulated on the way in. Faces with more than three
// vertices are fanned, which is exact for the convex polygons DCC exporters
// emit and close enough for anything else that a repair pass then measures.
#pragma once
#include "mesh.hpp"
#include <string>
#include <vector>

namespace gpx {

// Load by extension. Returns false with `err` set on anything it cannot read.
bool mesh_load(const std::string &path, TriMesh &out, std::string &err);
// glTF (mesh_io_gltf.cpp): binary .glb, what the 3D generation services
// return, and JSON .gltf with its buffers beside it. Node transforms
// applied, every primitive one part, with its uvs and base-colour picture.
bool mesh_load_glb(const std::string &path, TriMesh &out, std::string &err);
bool mesh_load_gltf(const std::string &path, TriMesh &out, std::string &err);
// Binary FBX (mesh_io_fbx.cpp): every geometry with its model transform,
// uvs, per-polygon materials and their diffuse pictures.
bool mesh_load_fbx(const std::string &path, TriMesh &out, std::string &err);

// Save by extension (.stl .obj .ply .off). `ascii_stl` writes the readable
// STL variant, which triples the file size and is what some old tools want.
bool mesh_save(const std::string &path, const TriMesh &m, std::string &err,
               bool ascii_stl = false);

// The extensions we read and write, for file dialogs and error messages.
const std::vector<std::string> &mesh_load_formats();
const std::vector<std::string> &mesh_save_formats();

} // namespace gpx
