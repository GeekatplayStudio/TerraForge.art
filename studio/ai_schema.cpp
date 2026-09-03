// Geekatplay TerraForge - the action-document schema the AI is shown:
// every operation the assistant may emit, with its fields, one per
// domain. Split from ai_assist.cpp for the 500-line module rule.
#include "ai_assist.hpp"
#include <string>

namespace studio {

// ---------------------------------------------------------------- schema
std::string ai_action_schema(AiDomain domain) {
  std::string s =
      "Reply with ONLY a JSON object: {\"actions\":[ ... ]}.\n"
      "Every action has \"op\". Supported operations:\n";
  switch (domain) {
    case AiDomain::Camera:
      s += R"(- {"op":"add_camera","name":"Hero","focal_mm":50,"format":"Full frame 35mm",
   "aperture":2.8,"shutter":0.008,"iso":200,"film":"Kodak Portra 400",
   "look_at":"terrain"|"origin"|[x,y,z], "distance":2.0, "height":0.6,
   "azimuth_deg":210, "activate":true}
- {"op":"set_camera","name":"Hero", ...same fields...}   (edits the selected
   or named camera instead of creating one)
Sensor formats: Full frame 35mm, APS-C, Super 35 (cine), Micro Four Thirds,
16mm film, 65mm / IMAX, Large format 4x5.
Film stocks: Digital (neutral), Kodak Portra 400, Kodak Kodachrome 64,
Kodak Vision3 500T, Fuji Ektachrome-style, Ilford HP5 (B&W).
"cinematic" implies a wide sensor (Super 35 or 65mm), a fast aperture
(f/2 - f/4) and a film stock rather than Digital.
shutter is in seconds (1/125 = 0.008).
EXPOSURE: the scene is lit like open daylight, which needs about EV100 13
(f/8, 1/125s, ISO 100). Keep aperture^2 / shutter / (iso/100) near 8000 or
the image blows out. So a shallow cinematic f/2.8 needs a fast shutter
(about 1/1000) at ISO 100 - do not combine a wide aperture with a high ISO
and a slow shutter unless the user asks for a night or interior shot.
The world is a unit tile: terrain spans x 0..1, z 0..1, height around 0..0.25.
"in front of the terrain" means a position outside the tile looking at its
centre, e.g. eye [0.5, 0.35, 1.9] with look_at "terrain".)";
      break;
    case AiDomain::World:
      s += R"(- {"op":"set_sun","azimuth_deg":220,"altitude_deg":12,"intensity":3.0,
   "color":[1,0.85,0.6]}
- {"op":"set_sky","density":1.2,"ambient":0.7,"zenith":[r,g,b],"horizon":[r,g,b]}
- {"op":"set_fog","type":"off"|"haze"|"fog"|"pollution","density":1.2,
   "level":0.3,"color":[r,g,b]}
- {"op":"set_clouds","enabled":true,"type":"stratus"|"cumulus"|"cumulonimbus",
   "coverage":0.6,"density":1.2,"altitude":1.4,"thickness":0.8,"wind_speed":0.03}
- {"op":"set_water","enabled":true,"level":0.1,"deep":[r,g,b],"shallow":[r,g,b],
   "foam":true})";
      break;
    case AiDomain::Render:
      s += R"(- {"op":"set_render","engine":"mitsuba"|"cycles"|"luxcore"|"viewport",
   "width":1920,"height":1080,"samples":256,"output":"shot.png"}
- {"op":"render"}   (starts the render immediately))";
      break;
    case AiDomain::Object:
      s += R"(- {"op":"place_object","name":"Rock","position":[x,y,z],"scale":0.1,
   "rotation_deg":30}
- {"op":"select","name":"Terrain"}
- {"op":"run_macro","path":"C:/macros/dusk_forest.json"}
   (applies a saved action document - any of these ops, batched)
- {"op":"add_light","name":"Lantern","position":[x,y,z],"color":[r,g,b],
   "intensity":2.0,"reach":0.4}   (a point light; set_light edits by name)
- {"op":"add_primitive","kind":"cube"|"sphere"|"plane"|"cylinder"|"cone",
   "name":"Box","position":[x,y,z],"scale":0.1,"color":[r,g,b]}
- {"op":"import_object","path":"C:/models/rock.obj","name":"Rock",
   "position":[x,y,z],"scale":0.1}
- {"op":"set_scatter","object":"Rock","node":"ScatterPoints","size":0.5,
   "jitter":0.4,"seed":7,"sway":0.1}
   (copies of the mesh appear at every point of the Points node's cloud,
   standing on the terrain; node "" or 0 unbinds)
- {"op":"assign_material","node":"MaterialOutput","object":"Terrain"}
   (binds a MaterialOutput to an object; omit object for the terrain)
- {"op":"add_planet","name":"Mars","radius":3.5,"relief":0.03,"seed":42,
   "position":[x,y,z],"sea_level":0,"snow_line":0.9,"atmosphere":0.3,
   "rock_low":[0.45,0.25,0.15],"rock_high":[0.6,0.4,0.3],
   "atmo_color":[0.9,0.6,0.4]}
   (planets are procedural and free: any number is fine. sea_level 0 = dry
    world; the home terrain tile is at the origin, keep planets 8+ units away.
    "surface_node":"new" gives the planet its own SurfaceDisplacement field
    graph to shape it, like a Terragen planet's terrain network; or name an
    existing SurfaceDisplacement node / id)
- {"op":"set_planet","name":"Mars", ...same fields...}
- {"op":"set_viewport","planet_radius":1275}   (the HOME planet the terrain
   tile lies on, in tile units: 1275 = Earth at a 5 km tile, 0.0002 = a 1 m
   globe made from the heightmap, 0 = flat world)
- {"op":"add_infinite_terrain","planet":"Mars","style":"mountains"|"hills"|"dunes",
   "scale":5,"amplitude":1.0,"coverage":0.5,"seed":7}
   (omit "planet" to extend the home ground plane to the horizon instead;
    layers stack, so add several with different styles and coverages)
The world is a unit tile: terrain spans x 0..1, z 0..1.)";
      break;
    default:
      s += R"(- {"op":"graph","spec":{ ...node graph in the standard node JSON... }}
Use this to build terrain or material node graphs.
For "eroded terrain with materials that follow the erosion": ErosionLayers
erodes and outputs masks bedrock/scree/soil/grass/sediment/riverbed/snow
(+ wetness, flow, and two packed splat textures). Wire the masks into a
MaterialStack (mask k + albedo k per layer, from TextureFile / PBRMaterial /
FlatColor), its albedo and roughness into a MaterialOutput, then
{"op":"assign_material","node":"MaterialOutput"}. The erosion node then shows
in both the Terrain and Materials workspaces.)";
      break;
  }
  s += R"(
Available in every domain:
- {"op":"undo","steps":1}   (revert the last change, including your own)
- {"op":"redo","steps":1}
)";
  s += "\nOmit any field you do not want to change. Return only JSON.";
  return s;
}

} // namespace studio
