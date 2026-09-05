// Geekatplay TerraForge - the Material Studio and Browser as operations, so
// the assistant, the Python API and MCP can do what the two panels do.
#include "app.hpp"
#include "material_library.hpp"
#include "material_ui.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <json.hpp>
#include <string>

using nlohmann::json;

namespace studio {

bool ai_set_attr_value(gpx::Attribute &at, const json &v); // ai_ops_graph.cpp

namespace {

gpx::Node *resolve_material(App &a, const json &act, std::string &err) {
  // by name first, then by id; with neither, the one open in the studio
  if (act.contains("material")) {
    const json &m = act["material"];
    if (m.is_number_unsigned()) {
      gpx::Node *n = a.graph.find_node(m.get<uint64_t>());
      if (n && n->type == "MaterialOutput") return n;
    } else if (m.is_string()) {
      const std::string want = m.get<std::string>();
      for (auto &n : a.graph.nodes)
        if (n->type == "MaterialOutput" && n->attrs.get_s("name") == want)
          return n.get();
    }
    err = "no material called '" + act["material"].dump() + "'";
    return nullptr;
  }
  gpx::Node *n = a.graph.find_node(material_studio().material);
  if (n && n->type == "MaterialOutput") return n;
  err = "no material is open in the studio; pass 'material'";
  return nullptr;
}

} // namespace

int ai_material_op(App &a, const std::string &op, const json &act,
                   std::string &err) {
  if (op == "open_material") {
    gpx::Node *m = resolve_material(a, act, err);
    if (!m) return 0;
    a.show_material_studio = true;
    a.workspace = WS_MATERIALS;
    // A script switching materials does not get a modal to answer, so the
    // pending edits are left in the graph (never lost) and the switch goes
    // through.
    material_studio().material = 0;
    material_studio_open(a, m->id);
    a.status = "opened '" + m->attrs.get_s("name") + "' in the material studio";
    return 1;
  }

  if (op == "list_materials") {
    std::string line;
    for (const MatEntry &m : collect_materials(a)) {
      gpx::Node *n = a.graph.find_node(m.id);
      line += (line.empty() ? "" : ", ") + m.name + " (" +
              material_type_name(material_type_of(a.graph, n)) + ")";
    }
    a.status = line.empty() ? "no materials in the project" : "materials: " + line;
    return 1;
  }

  if (op == "set_material_type") {
    gpx::Node *m = resolve_material(a, act, err);
    if (!m) return 0;
    std::string want = act.value("type", std::string());
    for (auto &c : want) c = (char)tolower(c);
    int type = -1;
    for (int t = 0; t < MAT_TYPE_COUNT; ++t) {
      std::string nm = material_type_name(t);
      for (auto &c : nm) c = (char)tolower(c);
      if (nm == want || nm.rfind(want, 0) == 0) type = t;
    }
    if (type < 0) {
      err = "set_material_type: 'type' must be one of simple, pbr, mixed, "
            "layered, distribution, effector";
      return 0;
    }
    undo_push(a, std::string("material type: ") + material_type_name(type));
    material_set_type(a, m, type);
    a.status = "'" + m->attrs.get_s("name") + "' is now " + material_type_name(type);
    return 1;
  }

  if (op == "set_material") {
    // one property of the material by its attribute key: ior, tint,
    // roughness, luminous... anything material_params_declare puts there
    gpx::Node *m = resolve_material(a, act, err);
    if (!m) return 0;
    std::string key = act.value("key", std::string());
    gpx::Attribute *at = m->attrs.find(key);
    if (!at || !act.contains("value")) {
      err = "set_material needs 'key' (a MaterialOutput property) and 'value'";
      return 0;
    }
    undo_push(a, "material " + key);
    if (!ai_set_attr_value(*at, act["value"])) {
      err = "set_material: '" + key + "' cannot take " + act["value"].dump();
      return 0;
    }
    a.graph.mark_dirty(m->id);
    a.request_eval();
    a.uploaded_serial = 0;
    a.status = "'" + m->attrs.get_s("name") + "' " + key + " = " + act["value"].dump();
    return 1;
  }

  if (op == "save_material") {
    gpx::Node *m = resolve_material(a, act, err);
    if (!m) return 0;
    std::string path = material_library_save(a, m->id, err);
    if (path.empty()) return 0;
    if (material_studio().material == m->id) material_studio_mark_saved(a);
    a.status = "saved " + path;
    return 1;
  }

  if (op == "load_material") {
    std::string name = act.value("name", std::string());
    if (name.empty()) {
      err = "load_material needs the library material's 'name'";
      return 0;
    }
    for (LibraryMaterial &lm : material_library())
      if (lm.name == name) {
        unsigned long long id = material_library_load(a, lm, err);
        if (!id) return 0;
        a.graph_layout_serial++;
        a.request_eval();
        if (act.value("open", true)) {
          material_studio().material = 0;
          material_studio_open(a, id);
        }
        if (act.value("assign", false)) {
          SceneState &sc = scene();
          if (sc.selected >= 0 && sc.selected < (int)sc.objects.size())
            sc.objects[(size_t)sc.selected].material_node = id;
        }
        a.status = "loaded '" + name + "' from the library";
        return 1;
      }
    err = "no library material called '" + name + "'";
    return 0;
  }

  return -1;
}

} // namespace studio
