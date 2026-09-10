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
- {"op":"view_to_camera","view":1,"camera":"Hero","name":"Hero","activate":true}
   (a viewport's point of view written into a camera - the named one, or a new
    one called `name` when there is none)
- {"op":"camera_to_view","camera":"Hero","view":2,"link":true}   (link: the view
   looks through the camera from now on; link false: the free orbit moves there)
- {"op":"render","camera":"Hero","preset":"Final 4K"}   (the active camera when
   no name; a preset applied first when given)
- {"op":"render_preset","action":"save","name":"Final 4K","camera":"Hero",
   "engine":"cycles","width":3840,"height":2160,"samples":512}
   (action save | apply (camera name, "all", or the active one) | delete | list;
    presets are saved with the project and listed in the Render menu)
- {"op":"render_batch","cameras":["Hero","Wide"],"preset":"Final 4K"}   (every
   camera when the list is omitted; each renders in turn to its own file)
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
- set_fog also takes "falloff", "absorb":[r,g,b], "sun_scatter", "albedo" (scattering albedo 0..1), "anisotropy" (HG g, -0.95..0.95), "heterogeneity" (0..1, noise-broken fog) and "steps" (1..64 ray-march samples; 1 = closed form). The fog is a participating medium: Beer-Lambert extinction, single scattering, self-shadowed when marched.
- {"op":"set_clouds","enabled":true,"type":"stratus"|"cumulus"|"cumulonimbus",
   "coverage":0.6,"density":1.2,"altitude":1.4,"thickness":0.8,"wind_speed":0.03}
- Any number of cloud layers: add_node "CloudLayer" per layer (type, coverage, density, altitude, thickness, enabled...), chained clouds -> clouds into AtmosphereSettings. The first CloudLayer drives the main cloud settings; each further one is its own layer.
- set_clouds also takes a second layer: "layer2":true, "layer2_type", "layer2_coverage", "layer2_density", "layer2_altitude" (0.2..4), "layer2_thickness"
- {"op":"set_viewport","shading":"ids","id_mode":0|1}  (ID colours: one flat colour per object (0) or per material (1)); set_viewport also takes "terrain_shape":0|1|2 (square, round, rectangle) and "terrain_aspect" (the rectangle's depth over width)
- {"op":"set_water","enabled":true,"level":0.1,"deep":[r,g,b],"shallow":[r,g,b],
   "foam":true})";
      break;
    case AiDomain::Render:
      s += R"(- {"op":"set_render","engine":"mitsuba"|"cycles"|"luxcore"|"viewport",
   "width":1920,"height":1080,"samples":256,"output":"shot.png"}
- {"op":"render"}   (starts the render immediately)
- {"op":"list_settings"}  (every saved render/world setting by name with its value, in "reply")
- {"op":"set_setting","key":"cloud_coverage","value":0.7}  (any of those settings by its name; colours as [r,g,b])
- {"op":"crash_reports"}   (every crash report, hang report and session killed without a clean exit in logs/, newest first, with whether each was dealt with, in "reply")
- {"op":"crash_mark_fixed","file":"hang_20260908_120000.txt","note":"double lock in the browser"}  (closes one report; it leaves the startup warning)
- {"op":"terrain_clip","low_m":120,"high_m":900,"low_mode":"hole"|"flatten","high_mode":"flatten"|"hole","softness":0.02}
   (Vue's clipping altitudes: a TerrainClip node in front of the Terrain Output; "clear":true removes it; "low"/"high" take 0..1 of the height range instead of metres)
- {"op":"terrain_effect","effect":"dissolve","hardness":0.6,"iterations":1}
   (one pass of a Vue terrain-editor effect in front of the output: erosion diffusive, thermal, glaciation, wind, dissolve, alluvium, fluvial, river valley; global grit, gravel, pebbles, stones, peaks, fir trees, plateaus, terraces, stairs, craters, sharpen, cracks; hardness is Vue's Rock hardness 0..1)
- {"op":"add_component","kind":"terrain"|"infinite_terrain"|"planet"|"moon"|"nebula"|"dark_nebula"|"galaxy"|"elliptical_galaxy"|"planetary_nebula"|"atmosphere"|"cloud_layer"|"sun"|"water"|"light"|"camera"|"cube"|"sphere"|"plane"|"cylinder"|"cone"|"scatter"|"ecosystem"|"material","name":"...","path":"C:/mesh.obj"}
   (the whole component: object, driving node and material; "terrain" is a further heightfield tile with its own Noise -> Terrain Output chain, standing beside the ones there are - move it with place_object; import_mesh takes a path)
- {"op":"delete_object","name":"Terrain 2"}   (any asset and everything under it; the Add tile puts one back)
- {"op":"terrain_style","name":"Canyon"}   (Mountain, Ridged peaks, Eroded mountain, Canyon, Mounds, Dunes, Iceberg, Lunar, Realistic mountain range: a fresh chain wired to the output)
- {"op":"terrain_global","action":"invert"|"zero_edges"|"smooth_all"|"halve"|"double"|"reset_sculpt"|"remove_effects"}
- {"op":"terrain_import_picture","path":"C:/dem.png","mode":"blend"|"add"|"subtract"|"multiply"|"min"|"max","proportion":0.6}   (Vue's Picture button)
- {"op":"set_sculpt","active":true,"tool":"raise"|"plateau"|"flatten"|"altitude"|"smooth"|"terrace"|"noise"|"erase"|"shade","radius":0.08,"flow":0.6,"falloff":2,"invert":false,"altitude_m":300,"constrain_clip":true}
- {"op":"perf_report"}   (the performance watcher: frame phases, event rates, memory and findings about work done for nothing, in "reply"; also logs/perf_watch.json every 10 s)
- {"op":"render_passes","path":"shot.png","width":1920,"height":1080,
   "format":0|1|2,"passes":["depth","normal","albedo","object_id","direct",
   "shadow","ambient","specular","atmosphere","environment","position","water_mask"]}
   (viewport engine: beauty as PNG/EXR/HDR plus one linear EXR per pass)
- RenderBackdrop / RenderPasses / RenderOutput / PostProcess nodes drive the
   render editor from the graph: add_node them and set_attr their fields)";
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
   "name":"Box","position":[x,y,z],"scale":0.1,"color":[r,g,b],"detail":24}
   (detail 3..512 is the segment count round a round primitive and the grid
   size across a flat one; raise it before putting a displacement material
   on the object, since a displacement can only move vertices that exist)
- {"op":"combine_objects","mode":"union"|"intersect"|"difference",
   "objects":["Wall","Arch"]}
   (constructive solid geometry: the meshes are combined into one, each
   object's own transform baked in first. The first named object is the one
   that survives, keeping its name and material. Omit "objects" to use the
   current selection. Difference subtracts the later ones from the first,
   which is how a doorway, a window or a cave mouth is cut)
- {"op":"metaball","objects":["Blob 1","Blob 2"],"smoothness":1.0,
   "detail":64}
   (melts the objects into ONE smooth surface: each contributes a ball at its
   centre, sized by its own bounds, and neighbouring balls merge rather than
   intersect. smoothness 0 keeps them nearly separate, 2 pours them together.
   detail is the voxel count across the longest side - raise it for a finer
   surface, at a cubic cost. An object with a NEGATIVE scale carves instead
   of adding, which is how a hollow or a tunnel is made. The result is a
   solid, so it can go straight into combine_objects)
- {"op":"import_object","path":"C:/models/rock.obj","name":"Rock",
   "position":[x,y,z],"scale":0.1}
- {"op":"set_scatter","object":"Rock","node":"ScatterPoints","size":0.5,
   "jitter":0.4,"seed":7,"sway":0.1,"species":1}
   (copies of the mesh appear at every point of the Points node's cloud,
   standing on the terrain; node "" or 0 unbinds; for an EcosystemLayer,
   species 1..8 says which of its species this mesh is, 0 = all)
- An ecosystem: add_node "EcosystemLayer" in the material stack (density per
   hectare, presence by mask/altitude/slope/orientation, species, affinity
   and repulsion against its "below" points input, decay near objects from
   TerrainImprint's "objects" output), then set_scatter a mesh per species.
   See examples/macros/ecosystem_layers.json.
- {"op":"place_on_terrain","object":"House"}  (the object stands on the terrain
   and the ground moulds to its base through a TerrainImprint node)
- {"op":"set_ground","object":"House","lock":true,"offset_m":0,"margin_m":0.5,
   "blend_m":6,"sink_m":0.3,"lift_m":0,"dig_m":0}
   (offset_m is the height over the surface, negative for below it - the same
   number as the object's Y, so setting either moves it. sunk_m sinks the
   object INTO the ground by that many metres below the highest ground under
   its base, and the ground does not react - that is the control for the gap
   under an object on a slope, and for burying one. margin_m
   is the flat patch past the walls, blend_m how far the ground responds,
   sink_m a dead band before digging starts. lift_m and dig_m cap how far the
   ground may travel to meet the object: both 0 and it holds the height you
   gave it - hanging in the air, or buried with the ground closed over it -
   which is how you get an arch, a bridge deck or a half-sunk ruin. Omit them
   for the old unlimited behaviour, where the ground always follows)
- {"op":"find_nodes","query":"rocks","limit":8}
   (WHICH node does the thing you are describing. Searches by meaning, not
   spelling: "rocks" finds the stone nodes, "wear the mountains down" finds
   the erosion ones. Returns type, display name, score and category in
   "reply", strongest first. Reach for this before add_node whenever you
   know the effect you want but not the node that produces it)
- {"op":"probe_height","x":0.5,"z":0.5}  (the ground's height at a point of the
   tile, displayed and graph, in heightmap units and metres - in "reply")
- {"op":"points_stats","node":"trees"}  (how many instances a Points node or an
   EcosystemLayer placed, per species, and their mean scale - in "reply")
- {"op":"save_node_preview","node":"scatter","path":"out.png"}
   (the 112 px thumbnail from that node's card, written out. WHAT DID THAT
   NODE ACTUALLY MAKE - a scatter that clumped, a mask covering the wrong
   half, a fractal at the wrong scale. Far cheaper than a render, and it
   answers the question the parameters cannot. The node must have been
   evaluated first)
- {"op":"assign_material","node":"MaterialOutput","object":"Terrain"}
   (binds a MaterialOutput to an object; omit object for the terrain)
- {"op":"show_panel","panel":"Material Editor","visible":true}
   (Library, Nodes, Properties, Viewport, Toolbar, Console, Timeline,
   Preview, Material Editor)
- {"op":"set_camera","name":"Camera 1","optics":true,"vignette":1.0,
   "chromatic":0.5,"flare":true,"flare_strength":0.7,"motion_blur":0.3,
   "distortion":0.05}
   (the optical simulation: distortion follows the focal length unless
   "distortion" is given, vignetting follows the aperture scaled by the
   amount, chromatic aberration and flare are off at 0/false. Setting any
   of them turns the simulation on. It applies to the viewport, the Preview
   panel and captured images alike)
- {"op":"open_material","material":"Mossy rock"}  (by name or node id; opens
   the Material Studio in the Materials workspace)
- {"op":"list_materials"}  (every project material with its type)
- {"op":"set_material_type","material":"Mossy rock","type":"layered"}
   (simple | pbr | mixed | layered | distribution | effector - scaffolds the
   nodes the type needs and keeps what was connected)
- {"op":"set_material","material":"Mossy rock","key":"ior","value":1.52}
   (any property of the material: tint [r,g,b,a], color_gain, saturation,
   map_scale, normal_strength, bump_depth, displacement, highlight_model
   (0 GGX, 1 Phong), specular, roughness, highlight_color, transparency,
   ior, reflect_with_angle, reflection, reflect_min, reflect_angle,
   metallic, translucency, sss, sss_depth, sss_color, backlight, diffuse,
   ambient, luminous, luminous_color, contrast, cast_shadows,
   color_reflected, color_transmitted)
- {"op":"save_material","material":"Mossy rock"}  (to the library, with thumbnail)
- {"op":"load_material","name":"Mossy rock","open":true,"assign":false}
- {"op":"preset_material","name":"Copper","object":"Cube"}  (a base material, made in the graph: Basic Color, Mirror, Wax, Slime, Metal, Copper, Gold, Iron, Glass, Water, Ice, Smoke, Glow, Ground, Sand, Rock, Snow, Swamp, Displacement rock, Displacement ground; "object" also assigns it)
- {"op":"ai_generate_texture","prompt":"wet mossy granite","apply":true,
   "material":"Mossy rock","channel":"base color","provider":"comfyui","width":1024,"seed":0}
   (a seamless tileable texture; apply connects it to the material's channel)
- {"op":"ai_generate_skydome","prompt":"sunset over a fjord","apply":true}
   (a 2:1 equirectangular sky; apply sets it as the backdrop)
- {"op":"ai_generate_image","prompt":"...","width":1024,"height":1024}
- {"op":"ai_generate_model","prompt":"a weathered boulder","image":"C:/ref.png","import":true,
   "provider":"meshy"}  (text- or image-to-3D; import places it in the scene)
- {"op":"ai_describe","prompt":"a foggy fjord at dawn...","scope":"scene"|"terrain"|"atmosphere"}
   (the text model plans actions from the description and they are applied)
- {"op":"ai_ask","prompt":"...","system":"...","image":"..."}  (the text model answers in the status)
- {"op":"ai_jobs"} / {"op":"ai_job_cancel","id":3}
- {"op":"config_set_service","service":"openai","key":"sk-...","endpoint":"","model":"gpt-4o-mini","enabled":true}
- {"op":"config_set_defaults","text_provider":"openai","image_provider":"comfyui","model_provider":"meshy",
   "comfy_url":"http://127.0.0.1:8188","comfy_install":"D:/ComfyUI"}
- {"op":"config_status"} / {"op":"config_check_comfy"}
- {"op":"asset_search","query":"mossy rock","kind":"material","limit":20}
   (the asset index: materials, meshes, textures, layouts found by name,
   folder, tag or note; ids are kind/relative-path)
- {"op":"asset_open","id":"material/Moss_Rock_02.gpxmat"}  (a material loads
   and opens in the studio, a mesh imports, a layout applies)
- {"op":"asset_tag","id":"...","tag":"lichen"}  / asset_untag / 
  {"op":"asset_note","id":"...","text":"..."}
- {"op":"asset_trash","id":"..."} / asset_restore  (moves, never deletes)
- {"op":"asset_rescan"} / {"op":"asset_roots"} /
  {"op":"asset_add_root","path":"D:/models","kind":"mesh"} / asset_remove_root
- {"op":"place_object","name":"rock","position":[x,y,z],"scale":0.1,"heading_deg":30,
   "pitch_deg":0,"bank_deg":0,"squeeze":[1,0.5,1],"twist":[0,90,0],"bend":30,"bend_axis":0,
   "skew":[0.2,0,0],"taper":-0.5,"show_gizmo":true}
   (the whole transform and the deformers of a scene object, by name; "Terrain" is one:
   its position is an offset in tile units, squeeze [2,1,1] doubles its width, heading turns
   it, pitch/bank tilt it, twist/bend/skew/taper deform the whole tile)
- {"op":"import_mesh","path":"C:/models/part.stl"}
   (OBJ, STL, PLY or OFF; the file's own coordinates are kept and the object
   is sized by its transform, then analysed straight away)
- {"op":"mesh_analyse"}
   (the selected mesh: 11 checks, a 0-100 readiness score and a verdict,
   each issue with a count, a severity and where it is on the model)
- {"op":"mesh_repair","fill_holes":true,"drop_small_shells":false,"passes":3}
   (cleanup, winding, inside-out, hole filling - repeated until the mesh
   stops changing, then measured against a fresh analysis; undoable)
- {"op":"mesh_reduce","faces":5000}  or  {"op":"mesh_reduce","fraction":0.25}
   (quadric decimation; reports the worst deviation it measured)
- {"op":"mesh_split"}
   (every disconnected shell becomes its own object)
- {"op":"mesh_export","path":"out.stl","ascii":false,"apply_transform":false}
   (STL, OBJ, PLY or OFF; says so when the file is not a closed solid)
- {"op":"arrange_views","count":4}
   (the viewport area becomes 1..8 cells; nothing else on screen moves)
- {"op":"add_view"} / {"op":"add_view","split":true,"vertical":false}
   (another 3D viewport beside the one last worked in - as a tab, or
   splitting it; "view":3 opens that particular slot)
- {"op":"close_view","view":2}
   (the last remaining viewport is never closed)
- {"op":"save_layout","name":"Modelling"} / {"op":"load_layout","name":"Modelling"}
   (a layout is every window's place, the open viewports and what each one
   shows - never the scene; also delete_layout, list_layouts, reset_layout)
- {"op":"set_locked","object":"Camera 1","locked":true}
   (locks an object in place: no gizmo, no dragging; false frees it)
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
   globe made from the heightmap, 2e-8 = a 0.1 mm globe, 1e12 = a giant,
   0 = flat world)
- {"op":"set_viewport","world":"ring"}   (the world's shape: "globe" the planet;
   "ring" a ring world - a cylinder of planet_radius curving along the tile's
   east-west, ground on the inside, sun on the axis, the far side overhead;
   "dyson" a Dyson sphere - ground on the inside of the globe, sun at the centre;
   "flat" a flat world - a plane world_width across, cut to "world_outline":"disc"|"square",
   nothing beyond its edge.
   The parts a preset sets: "world_shape":"globe"|"ring"|"flat", "world_inside":true,
   "world_width":400 (a ring's width or a flat world's size, tile units),
   "world_sun_inside":true. "world_thickness":0.5 (tile units) makes the world a body:
   the other face lies that far below the ground and a ring or a flat world shows the
   rim wall between its faces; 0 is a skin. The atmosphere, the cloud layers and the
   water lie on the surface whatever its shape - on a ring the clouds are a band round
   the inside of the ring.)
- {"op":"place_object","name":"Terrain 2","side":"inside"}   (which face of the
   world a tile or a surface layer stands on: "world", "outside", "inside" -
   one shell with ground on both faces)
- {"op":"set_viewport","place_on_planet":true,"place_edge":0.1,
   "place_flatten":1.0,"place_presence":0.04,"place_ground":0.14,
   "place_gradient":1.0,"place_mode":0}
   (place_edge: how far the join reaches, fraction of the tile; place_gradient: its curve,
    below 1 the tile holds its ground and drops at the rim, above 1 it gives way from further
    in; place_mode 0 blends the tile's features and lets the planet through where the tile
    is flat, 1 blends the whole tile, 2 is zero edge: the whole tile with its rim brought to
    the planet's ground seamlessly, 3 clips low: the tile stands only where it is higher
    than the planet's ground, its low edges cut away, 4 clips high: only where it is lower,
    its high edges cut away; a heightmap into Terrain output's "blend mask" port
    multiplies the join, 1 tile, 0 planet)
   (how the terrain tile sits on the planet: the planet's relief shows
    through where the tile is flat, is levelled under the tile's features
    (flatten 1) or kept beneath them (0), feathered over place_edge of the
    tile; a feature is anything further than place_presence from the tile's
    ground level, so a hole dug below ground becomes a basin the water fills;
    place_ground is the planet's own ground level in heightmap units (water
    is at water_level, 0.08 by default), and the tile's ground settles to it)
- {"op":"add_moon","name":"Luna","radius":1.5,"seed":3,"position":[x,y,z]}
   (a small airless cratered world in the sky; set_planet edits it)
- {"op":"add_nebula","name":"Orion","type":"nebula"|"dark"|"galaxy"|"elliptical"|"planetary",
   "azimuth":40,"elevation":35,"size_deg":24,"tilt_deg":50,"rotation_deg":0,"seed":7,
   "brightness":1.0,"density":0.5,"detail":0.5,"arms":2,"color1":[r,g,b],"color2":[r,g,b]}
   (a thing in deep space, at infinity, seen where the atmosphere lets space
    through: at night, from high up, beyond a ring's rim; any number of them)
- {"op":"set_nebula","name":"Orion", ...same fields...}
   (clouds also take "dust":0.55, "warp":0.6 (how far it is pulled out of a
    ball), "glow":0.4, "sources":3 (hot stars inside, whose glare decides
    where it is teal and where it is red), and "palette":"auto" to take both
    colours from the realism dial)
- {"op":"space_preset","name":"night"|"hubble"|"cinema"|"deep_field"|"nursery"|"void"}
   (a whole sky at once: the star field, the milky band and the palette, and
    the ones that want nebulas scatter them too)
- {"op":"space_populate","count":4,"seed":1,"style":"mixed"|"nebulas"|"galaxies"|"dark"}
   (scatters that many nebulas and galaxies over the sky, spread apart and
    varied, coloured from the realism dial; replaces what a previous
    populate made and leaves anything placed by hand. Eight at most.)
- {"op":"set_space","on":true,"brightness":1,"realism":0.65,"glow":0.5,"quality":1,
   "star_spikes":0.45,"star_halo":0.6,"star_clump":0.55,"galaxy_grain":0.8,
   "stars":true,"star_density":0.5,"star_brightness":1,"star_size":1,
   "star_temperature":0.6,"star_seed":1,"galaxy":true,"galaxy_intensity":0.7,
   "galaxy_width":14,"galaxy_yaw":35,"galaxy_pitch":55,"galaxy_core":0,"galaxy_dust":0.7,
   "galaxy_color":[1,0.95,0.9],"galaxy_seed":1}
   (the star field and the galaxy band - the Milky Way - behind the air; the
    atmosphere is a layer of "set_sky" "height_m" (100000 = 100 km) over the
    world's surface whatever its shape, and beyond it is this)
- {"op":"add_infinite_terrain","planet":"Mars","style":"terrain"|"mountains"|"hills"|"dunes"|"craters",
   "scale":5,"amplitude":1.0,"coverage":0.5,"seed":7}
   (omit "planet" to extend the home ground plane to the horizon instead;
    layers stack, so add several with different styles and coverages;
    "terrain" is the realistic landscape: eroded ridges, hills, plateaus,
    valleys and lowland lakes in one layer - what a new scene starts with)
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
Animation (docs/ANIMATION.md). Nothing moves unless it has a key. A track is
addressed by "object"+"prop" (pos, rot, scale, scl, color, visible,
deform.twist/bend/shear/taper, light.intensity/radius/cone, cam.eye/target/
focal_mm/aperture..., planet.*, surf.*; "comp":"x"|"y"|"z" for one axis), by
"world" (sun_azimuth, sun_altitude, sun_intensity, sun_color, hour, exposure,
fog_density, fog_level, water_level, cloud_coverage, cloud_wind_dir...), by
"node"+"attr", or by "track" id from keys. Times: "frame" (or "time" seconds).
- {"op":"set_key","object":"Rock","prop":"pos","frame":0,"value":[0.5,0.05,0.5]}
- {"op":"set_key","object":"Rock","prop":"rot","comp":"y","frame":48,"value":180,"ease":"easy"}
- {"op":"set_key","world":"sun_altitude","frame":0,"value":5,"interp":"linear"}
- {"op":"set_key","node":"Noise","attr":"seed","frame":24,"value":3,"interp":"step"}
- {"op":"remove_key",...same address...,"frame":48}   {"op":"remove_animation",...}
- {"op":"keys"}  (every track and its keys, in the state's "reply")
- {"op":"set_frame","frame":12}  {"op":"play"}  {"op":"play","playing":false}  {"op":"stop"}
- {"op":"set_range","fps":30,"start":0,"end":120,"loop":"loop"|"once"|"pingpong","autokey":true}
- {"op":"set_extrapolation","object":"Windmill","prop":"rot","mode":"cycle"|"cycle_offset"|"pingpong"|"linear"|"constant"}
- {"op":"set_ease","object":"Rock","prop":"pos","ease":"easy"|"in"|"out"|"linear"|"hold"}
- {"op":"set_expression","object":"Buoy","prop":"pos","comp":"y","expr":"value + sin(t*2)*0.02"}
- {"op":"add_modifier","world":"sun_intensity","type":"noise"|"oscillator"|"offset"|"limit"|"smooth","amplitude":0.2,"frequency":1}
- {"op":"bake"|"simplify"|"snap_keys"|"mirror_keys"|"retime", ...address..., "factor":2}
- {"op":"add_marker","frame":60,"name":"impact"}  {"op":"key_transform"}
- {"op":"playblast","dir":"D:/out/blast","width":1280,"height":720}
- {"op":"render_sequence","dir":"D:/out/shot","fps":30,"width":1920,"height":1080}

The graph, by single steps (the "graph" op builds a whole graph at once):
- {"op":"add_node","type":"Noise","alias":"n1","x":40,"y":40,"attrs":{"octaves":8}}
- {"op":"connect","from":"n1","to":"out","from_port":"output","to_port":"heightmap"}
- {"op":"disconnect","to":"out","to_port":"heightmap"}
- {"op":"set_attr","node":"n1","key":"octaves","value":9}   (or "attrs":{...})
- {"op":"delete_node","node":"n1"}  {"op":"bypass","node":"n1","bypass":true}
- {"op":"move_node","node":"n1","x":300,"y":80}  {"op":"clear_graph"}
- {"op":"set_resolution","resolution":1024}   (the heightmap size; 256..8192)
- {"op":"view_node","node":"n1"}   (pin the 3D views to this node's output; omit
   node to follow the Terrain output again)
- {"op":"select_node","node":"n1","properties":true}  {"op":"open_node_editor","domain":"materials"}
- {"op":"set_workspace","workspace":"terrain"|"materials"|"atmosphere"|"render"|"objects"|"lighting"|"cameras"|"animation"}
- {"op":"evaluate"}   (recompute the graph now)  {"op":"capture","path":"D:/out/view.png","width":1280,"height":720}
Project, painting, meshes, diagnostics:
- {"op":"save_project","path":"D:/scenes/valley.gpxt"}  {"op":"open_project","path":"..."}
- {"op":"paint_save","path":"D:/out/paint.png"}  {"op":"paint_load","path":"..."}  {"op":"paint_clear"}
- {"op":"export_instances","object":"Rock","path":"D:/out/rocks.csv"}   (every scattered copy's transform)
- {"op":"mesh_retopo","faces":5000}   (quad-dominant remesh of the selected mesh)
- {"op":"mesh_solidify"}   (give an open surface a thickness)   {"op":"mesh_analyze"} = mesh_analyse
- {"op":"set_time","time":2.5}  {"op":"set_camera_key","camera":"Hero","time":2.5}
- {"op":"remove_track", ...address...}  {"op":"set_interp", ...address...,"frame":24,"interp":"bezier"|"linear"|"step","ease":"in"|"out"|"inout"}
- {"op":"remove_marker","name":"impact"}  {"op":"clear_modifiers", ...address...}
- {"op":"verify_field_gpu"}  {"op":"verify_accel"}   (CPU/GPU agreement reports; the
   first also writes api/field_gpu_report.txt)
- {"op":"set_light","name":"Lamp","position":[x,y,z],"color":[r,g,b],"intensity":2,"reach":0.4,
   "type":"point"|"spot","cone":40,"heading_deg":90,"pitch_deg":-30}
- {"op":"list_layouts"}  {"op":"delete_layout","name":"night work"}  {"op":"reset_layout"}
- {"op":"asset_untag","id":"mesh/rocks/boulder.obj","tag":"granite"}  {"op":"asset_restore","id":"..."}
- {"op":"asset_remove_root","path":"D:/assets"}

Available in every domain:
- {"op":"undo","steps":1}   (revert the last change, including your own)
- {"op":"redo","steps":1}
)";
  s += "\nOmit any field you do not want to change. Return only JSON.";
  return s;
}

} // namespace studio
