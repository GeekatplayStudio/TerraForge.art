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
- {"op":"view_to_camera","view":1,"camera":"Hero","name":"Hero","activate":true,"lens":true}
   (a viewport's point of view written into a camera - the named one, or a new
    one called `name` when there is none. lens (default true) carries the
    framing over as well as the place: a view looking through another camera
    hands its whole lens across, a free view sets the focal length that frames
    the same picture. lens false moves only the eye and the aim.)
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
- set_sun also takes "size_deg" (the sun's angular width, 0.53 is our own Sun), "glow" (the aureole the air makes round it, 0..4) and "glow_size" (how far that halo spreads, degrees).
- {"op":"add_air_layer","kind":"cloud"|"fog","atmosphere":<index or name>,
   "set":{"altitude":2.4,"thickness":0.3,"coverage":0.35}}
   (A BAND OF THE ATMOSPHERE as a thing in the scene, listed in the Objects
    tree under an Atmosphere. A sky is several at once - low stratus under
    cumulus under a high veil; haze to the horizon under a fog lying in the
    valley under a brown layer over a town - and every band renders with the
    others, their optical depths adding the way air's does. An Atmosphere may
    itself be a child of a Planet, so a band can belong to that planet's sky
    rather than to the world the camera stands on. A cloud band takes type,
    coverage, density, altitude, thickness; a fog band takes type, density,
    level, falloff, colour, scattering and drift. A cloud band may also be
    shaped by a picture: set "shape_map" to an image path and the layer takes
    its shape from it - white is cloud, black is clear sky, mid grey leaves
    the coverage to decide - with "shape_amount" 0..1 for how completely the
    picture decides, "shape_size_km" for how wide it is laid, "shape_x_km" /
    "shape_z_km" to slide it off the middle of the land, and "shape_tiled" to
    repeat it to the horizon. This is how to put one cloud in a named place,
    lay a front along a coast, or match a photograph.)
- {"op":"air_layers"}   (every band, what it is, and what it hangs under)
- {"op":"set_wind","speed_ms":7,"direction_deg":210,"gust_strength":0.5,
   "gust_frequency":0.12,"gust_size_m":250,"turbulence_deg":12,"shear":2.5}
   (THE SCENE'S WIND, which the clouds, the sea, the fog and every plant read:
    one speed and one direction for the whole world, so the cloud shadows, the
    waves and the trees all go the same way. speed_ms is at ten metres up the
    way weather is quoted - 2 a breath, 5 stirs leaves, 11 shakes branches, 20
    a gale; direction_deg is the way it blows toward. Gusts are the wind
    arriving in waves: how much harder, how often, and how large one is as it
    crosses the ground. shear is how much faster the air moves at cloud
    height. "clouds_follow"/"water_follow" false cut those loose onto their
    own settings.)
- {"op":"set_fog","type":"off"|"haze"|"fog"|"pollution","density":1.2,
   "level":0.3,"color":[r,g,b]}
- set_fog also takes "falloff", "absorb":[r,g,b], "sun_scatter", "albedo" (scattering albedo 0..1), "anisotropy" (HG g, -0.95..0.95), "heterogeneity" (0..1, noise-broken fog) and "steps" (1..64 ray-march samples; 1 = closed form). The fog is a participating medium: Beer-Lambert extinction, single scattering, self-shadowed when marched.
- {"op":"set_clouds","enabled":true,"type":"stratus"|"cumulus"|"cumulonimbus",
   "coverage":0.6,"density":1.2,"altitude":1.4,"thickness":0.8,"wind_speed":0.03}
- Any number of cloud layers: add_node "CloudLayer" per layer (type, coverage, density, altitude, thickness, enabled...), chained clouds -> clouds into AtmosphereSettings. The first CloudLayer drives the main cloud settings; each further one is its own layer.
- set_clouds also takes a second layer: "layer2":true, "layer2_type", "layer2_coverage", "layer2_density", "layer2_altitude" (0.2..4), "layer2_thickness"
- {"op":"set_viewport","view":1,"animate_plants":true}
   (whether the wind moves the plants in that window. Off in every window to
    begin with: a swaying crown never settles, and placing a tree or framing
    a shot is easier against a still picture.)
- {"op":"set_viewport","shading":"ids","id_mode":0|1}  (ID colours: one flat colour per object (0) or per material (1)); set_viewport also takes "terrain_shape":0|1|2 (square, round, rectangle) and "terrain_aspect" (the rectangle's depth over width)
- {"op":"set_water","enabled":true,"level":0.1,"deep":[r,g,b],"shallow":[r,g,b],
   "foam":true,"displaced":true,"wind_speed":4,"wind_dir":30,"choppiness":0.5,
   "wave_height":1,"wave_scale":1,"agitation":1,"clarity_m":18,
   "coast_foam":0.6,"foam_depth_m":1.5,"crest_foam":0.35,"crest_coverage":0.4}
   (one sea over the whole world, like Vue's water plane; wind_speed m/s sets the
   wave sizes - 4 a lake breeze, 15 a gale, 0 a mirror; displaced makes them real
   geometry; coast foam gathers where the water is shallower than foam_depth_m))";
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
    case AiDomain::Plant:
      s += R"(Plants are grown from rules. A species is a subgraph of Plant nodes whose
PlantSpecies root grows one individual per seed, with an age, a season, a health
and a wind. Parts link into the part they grow on (a leaf's "plant" output into a
branch's "child 1" input, the trunk into the root's "trunk"); PlantMaterial nodes
link into a part's "material" input.
- {"op":"plant_species_new","words":"old weeping willow in autumn","name":"Willow",
   "place":true,"position":[x,y,z],"size":1.0,"scatter":false,"count":300,
   "individuals":1,"ai":false}
   (a whole species from a plant's name and modifiers - well over a hundred plants
    and every archetype are known without a model; "ai":true asks the text model
    to describe the plant first. Instead of words, "description" takes the object
    plant_species_describe returns, and "archetype" one of broadleaf_tree,
    conifer, palm, shrub, fern, grass_tuft, flowering_plant, cactus_columnar,
    cactus_paddle, succulent_rosette, vine, bamboo, reed, mushroom, dead_tree,
    weeping_tree, bonsai, ground_cover, with "height_m")
- {"op":"plant_species_describe","words":"date palm"}   (the description the words
   give, in "reply": archetype, height_m, crown, bark, leaf shape and colours,
   flowers, fruit and their seasons - change it and pass it to plant_species_new)
- {"op":"plant_species_set","species":"Willow","age":40,"max_age":120,"health":0.3,
   "season":0.75,"seed":12,"wind_strength":0.4,"wind_direction":90,"detail":1,
   "receive_wind":true}   (health 0 dying .. 1 thriving; season 0 midwinter,
   0.25 spring, 0.5 midsummer, 0.75 autumn; detail is the meshing boost -3..3)
- {"op":"plant_species_variation","species":"Willow","seed":-1,"flag":false}
   (another individual of the same species: seed -1 picks a new one; flag keeps
    the current seed in the species' flagged list)
- {"op":"plant_species_individuals","species":"Willow","count":3}   (more
   individuals sharing the species' parts, each its own seed and object)
- {"op":"spray_biome","biome":"pine_wood"}
   (A WHOLE BIOME as a stack of populations, in the order ground actually
    assembles: boulders first, then what stands on them, then what lives
    among that, then the cover, then the litter over everything. Each layer is
    wired to whichever already-placed group its strongest rule is about, so a
    pine wood comes out as moss and lichen ON the boulders, needle litter
    under the conifers, fungi on the dead wood, ferns out from the trees and
    no grass at all. One painted brush drives all of them: paint once and the
    whole wood appears. Then say what stands for each group with spray_add.
    This is the way to populate ground - spray_new is the manual version.)
- {"op":"spray_biomes","family":"forest"}
   (every biome, or every one of a family: forest, field, farm, desert,
    water, underwater, alien, built. Each lists the groups it is made of.)
- {"op":"spray_groups"}
   (the vocabulary and the rules: every group, what tier it is placed in, how
    much ground it takes, and every rule about how it behaves near another
    group, with the observation each came from. THE RULES ARE ABOUT GROUPS,
    not about models - so a new model assigned to a group is placed correctly
    by every rule and in every biome, including ones written before it.)
- {"op":"spray_add","group":"conifer","plant":"scots pine","percent":70}
   ("group" puts a thing in the layer that stands for that group, wherever
    that layer sits in the stack - the rules about where it goes have already
    been applied. Left out, the group is guessed from the name, so
    "mossy_boulder_02" is a boulder. It guesses the NOUN, not the adjective.)
- {"op":"spray_new","density_ha":90,"spacing_m":4,"clumping":0.4,
   "components":[{"plant":"gorse","percent":55,"scale":1,"size_variation":0.4},
                 {"object":"Boulder","percent":30,"scale":1.6,"size_variation":0.8,"lean":1},
                 {"plant":"foxglove","percent":15}]}
   (THE SPRAY BRUSH: a painted population of several kinds at once. It makes a
    MaskPaint node feeding an EcosystemLayer - select the mask node and paint
    in the viewport with the terrain brushes to say WHERE, thickest where the
    stroke is heaviest. Each component says WHAT: a plant grown from its name,
    or "object" naming a mesh already in the scene (a rock, an imported
    model). "percent" is its share of the whole against the others - they are
    weights, so 55/30/15 and 0.55/0.3/0.15 place the same mix. "scale" is its
    size, "size_variation" how much that size varies for this kind alone (0
    takes the layer's; rocks vary far more than nursery trees), "lean" how far
    it tips with the slope (0 stands straight up, 1 lies along the ground - a
    boulder sits on a hillside, a tree stands out of it).)
- {"op":"spray_add","spray":<node id>,"plant":"bracken","percent":20}
   (another kind in the mix; eight at most. Leave out "spray" when there is
    only one.)
- {"op":"spray_list"}   (every spray, its components and each one's share as a
   percentage of the whole)
- {"op":"spray_set","spray":<id>,"slot":1,"percent":40,"scale":2,
   "size_variation":0.6,"lean":0.8}
   (change one kind. Without "slot" it changes the brush itself:
    density_ha, spacing_m, clumping, size_variation.)
- {"op":"plant_forest","words":"scots pine","individuals":5,"area_m":1500,
   "spacing_m":9,"clumping":0.6,"altitude_lo":0.02,"altitude_hi":0.75,
   "max_slope_deg":34,"size_variation":0.35,"unbounded":true}
   (A WOOD of one kind, and the way to make one: the species is grown
    `individuals` times, each with its own seed so no two trees are alike,
    and an EcosystemLayer splits its points among them - so the wood costs
    what those few trees cost, however many copies stand in it. Scattering a
    single plant instead gives a plantation of visible clones. The rules come
    with it: an altitude band keeps them out of the water, a slope band off
    the cliffs, and clumping gathers them into stands with clearings. Pass
    "species" instead of "words" to make a wood of a species already grown.)
- {"op":"plant_part_add","species":"Willow","parent":"trunk","preset":"branch","slot":0}
   (parent: "trunk", "root", a node id or a node type such as "PlantSegment";
    presets: trunk, branch, stem, palm, twig, leaf, billboard leaf, growth,
    flower, cutout leaf, fruit, bark material, leaf material; slot 0 = first free)
- {"op":"plant_species_preset","species":"Willow","action":"store"|"apply"|"delete"|"list","name":"Winter veteran"}
- {"op":"plant_species_save","species":"Willow","name":"Weeping willow","group":"trees","note":"..."}
   (into the plant library, where Add and Scatter place it like any plant)
- {"op":"plant_species_load","plant":"species/weeping_willow","place":true,"position":[x,y,z]}
- {"op":"plant_species_list"}   (the species in the graph and the library, the
   archetypes and the part presets, in "reply")
- {"op":"plant_species_export","species":"Willow","path":"C:/out/willow.glb"}   (.glb, .gltf or .obj)
Every part's parameters are node attributes. set_attr on a Plant node takes a
number for a value, [value, spread] for a value with a random spread, or
{"value":v,"spread":s,"spread_mode":0|1|2,"scope":0|1|2|3,"curve_along":"d:0,1,0,1|1;0,1,0,0;1,0.2,0,0"}
(spread_mode absolute, relative, gaussian; scope each time, per primitive, per
plant, per ancestor); a curve attribute takes [[x,y],[x,y],...].
PlantSegment: length, radius, radius_profile, tropism, axis_bend, perturb_strength,
flare_number, blade_number, count, count_mode, start, end, angle, coil,
arrangement, positioning, per_whorl, pruning, cut_probability. PlantLeaf: length,
width, orientation, mesh_kind, midrib_angle, curvature_h, shift_hue. PlantFlower:
radius, length, lobes, profile_mode. PlantGrowth: iterations, internode,
angle_with_parent, apical, gravitropism_influence, phototropism. PlantMaterial:
color, source (0 colour or pictures, 1 leaf, 2 bark, 3 petal), leaf_shape,
bark_kind, color_map, alpha_map, season_count. Seasons group on leaf-like parts:
presence_season, tint_season, presence_health, tint_health.)";
      break;
    case AiDomain::Object:
      s += R"(- {"op":"place_object","name":"Rock","position":[x,y,z],"scale":0.1,
   "rotation_deg":30}
- {"op":"select","name":"Terrain"}
- {"op":"run_macro","path":"C:/macros/dusk_forest.json"}
   (applies a saved action document - any of these ops, batched)
- {"op":"add_light","name":"Lantern","position":[x,y,z],"color":[r,g,b],
   "intensity":2.0,"reach":0.4}   (a point light; set_light edits by name)
- {"op":"add_primitive","kind":"cube"|"sphere"|"plane"|"cylinder"|"cone"|"pine"|"juniper"|"palm"|"fern"|"grass"|"bush"|"boulder",
   "name":"Box","position":[x,y,z],"scale":0.1,"color":[r,g,b],"detail":24,"seed":0}
   (detail 3..512 is the segment count round a round primitive and the grid
   size across a flat one; raise it before putting a displacement material
   on the object, since a displacement can only move vertices that exist.
   The plants and the boulder are built from their kind with bark and
   foliage colours and come at their real size (pine ~22 m, juniper ~6 m,
   palm ~16 m, fern ~1.6 m, grass ~0.8 m, bush ~2.5 m, boulder ~3 m); omit
   "scale" and "color" to keep them, and scatter them with set_scatter;
   "seed" makes another individual of the same kind, 0 the usual one)
- {"op":"plant_library","query":"fern","group":"trees","source":"polyhaven","rescan":false}
   (the Plants workspace's library in "reply": the built-in plants, the free CC0
   plants downloaded from Poly Haven and the user's recorded models, each with
   its id, group - trees, shrubs, ground cover, flowers, grass, deadwood, rocks -
   height in metres, licence and, for a set, its variants)
- {"op":"plant_add","plant":"polyhaven/fern_02","variant":"b","position":[x,y,z],
   "size":1.0,"heading_deg":30,"scatter":false,"count":300,"name":"Fern"}
   (a library plant into the scene at its real size, standing on the ground:
   under the view's pivot without "position"; size multiplies its own size;
   scatter also binds it to a new ScatterPoints node with count copies)
- {"op":"plant_fetch","ids":["fern_02","shrub_03"],"res":"1k"}  (downloads the free
   CC0 plants - the curated set when ids is omitted - in the background;
   {"op":"plant_fetch","cancel":true} stops it)
- {"op":"plant_record","path":"C:/models/oak.fbx","name":"My oak","height_m":12}
   (a model the user owns into the library, left where it is; height_m sets its
   size, 0 reads its units from the file)
- {"op":"plant_species_new","words":"old oak","place":true,"position":[x,y,z],"scatter":false}
   (a plant grown from rules rather than a model: its species' parts, age, season,
    health and wind are all editable; the Plants workspace's assistant knows the rest)
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
   tile, displayed and graph, in heightmap units and metres - in "reply";
   "drawn_m" adds the micro-relief the viewport draws over "placed_m")
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
   Preview, Material Editor, Plants)
- {"op":"set_camera","name":"Camera 1","optics":true,"vignette":1.0,
   "chromatic":0.5,"flare":true,"flare_strength":0.7,"motion_blur":0.3,
   "distortion":0.05}
   (the optical simulation: distortion follows the focal length unless
   "distortion" is given, vignetting follows the aperture scaled by the
   amount, chromatic aberration and flare are off at 0/false. Setting any
   of them turns the simulation on. It applies to the viewport, the Preview
   panel and captured images alike. The flare's parts: "flare_style" 0
   classic ghosts, 1 cinematic, 2 anamorphic; "flare_rays", "flare_streak",
   "flare_ghosts" and "flare_halo" 0..2 each; and their shapes: "flare_core"
   0..2, "flare_ray_count" 4..64, "flare_ray_length" and "flare_streak_length"
   (1 as drawn), "flare_streak_tint":[r,g,b], "flare_halo_radius" (frame
   heights, 0.19), "flare_ghost_count" 0..12, "flare_blades" 5..9 (the ghosts'
   polygon), "flare_chroma" 0..2 (colour parting), "flare_seed". Bloom, the
   glow a lens spreads round everything bright: "bloom" 0..4 (0 off),
   "bloom_threshold" 0..0.99 (from how bright, of the picture's white) and
   "bloom_size" 0..1 (how far it spreads))
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
   "clouds":0.2,"rock_low":[0.45,0.25,0.15],"rock_high":[0.6,0.4,0.3],
   "atmo_color":[0.9,0.6,0.4]}
   (planets are procedural and free: any number is fine. sea_level 0 = dry
    world; clouds 0-1 is the cloud cover seen from space (default 0.4, none
    without an atmosphere); the home terrain tile is at the origin, keep
    planets 8+ units away.
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
    where it is teal and where it is red), "turbulence":0 (filaments torn by a
    finer warp), "lanes":0 (thin dust ridges across the glow), "core_glow":0
    (the gas round the hot stars burned toward white), "source_stars":0.6 (the
    hot stars drawn, 0 hides them), "color3":[r,g,b] (the barely-lit
    outskirts), and "palette":"auto" to take both colours from the realism dial)
- {"op":"space_preset","name":"night"|"hubble"|"cinema"|"deep_field"|"nursery"|"void"|"nebula_cinema"}
   (a whole sky at once: the star field, the milky band and the palette, and
    the ones that want nebulas scatter them too)
- {"op":"space_populate","count":4,"seed":1,"style":"mixed"|"nebulas"|"galaxies"|"dark"}
   (scatters that many nebulas and galaxies over the sky, spread apart and
    varied, coloured from the realism dial; replaces what a previous
    populate made and leaves anything placed by hand. Eight at most.)
- {"op":"set_space","on":true,"brightness":1,"realism":0.65,"glow":0.5,"quality":1,
   "star_spikes":0.45,"star_halo":0.6,"star_clump":0.55,"galaxy_grain":0.8,
   "stars":true,"star_density":0.5,"star_brightness":1,"star_size":1,
   "star_temperature":0.6,"star_seed":1,"star_spike_points":4,"star_spike_angle":0,
   "star_spike_chroma":0,"star_saturation":1,"star_glow":0,"star_bright_share":0.5,
   "star_clusters":0,"star_cluster_size":1.5,"galaxy":true,"galaxy_intensity":0.7,
   "galaxy_width":14,"galaxy_yaw":35,"galaxy_pitch":55,"galaxy_core":0,"galaxy_dust":0.7,
   "galaxy_color":[1,0.95,0.9],"galaxy_seed":1}
   (the star field and the galaxy band - the Milky Way - behind the air; the
    atmosphere is a layer of "set_sky" "height_m" (100000 = 100 km) over the
    world's surface whatever its shape, and beyond it is this. star_bright_share
    0-1: how many stars are bright ones; star_clusters 0-1: how many star
    clusters - loose blue open ones and tight yellow globulars - at
    star_cluster_size degrees across)
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
- {"op":"set_workspace","workspace":"terrain"|"materials"|"atmosphere"|"render"|"objects"|"lighting"|"cameras"|"animation"|"plants"}
- {"op":"evaluate"}   (recompute the graph now)  {"op":"capture","path":"D:/out/view.png","width":1280,"height":720,"camera":"Hero"}
   (the first viewport's own point of view when no camera is named; with one,
    that camera's picture, which is how a script photographs a named view)
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
