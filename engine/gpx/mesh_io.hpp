// Geekatplay TerraForge - reading and writing meshes.
//
// The formats a terrain and print workflow actually needs, each written by
// hand so there is no dependency and no licence to inherit: OBJ (the DCC
// lingua franca), STL (what every slicer eats), PLY (scanners) and OFF.
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
// Binary glTF, geometry only (mesh_io_gltf.cpp): what the 3D generation
// services return. Node transforms applied, every primitive welded into one.
bool mesh_load_glb(const std::string &path, TriMesh &out, std::string &err);

// Save by extension (.stl .obj .ply .off). `ascii_stl` writes the readable
// STL variant, which triples the file size and is what some old tools want.
bool mesh_save(const std::string &path, const TriMesh &m, std::string &err,
               bool ascii_stl = false);

// The extensions we read and write, for file dialogs and error messages.
const std::vector<std::string> &mesh_load_formats();
const std::vector<std::string> &mesh_save_formats();

} // namespace gpx
