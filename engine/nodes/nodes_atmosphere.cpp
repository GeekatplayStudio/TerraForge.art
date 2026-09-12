// Geekatplay TerraForge — atmosphere & render nodes (Terragen-style).
// These nodes carry scene parameters rather than heightmaps: after each
// evaluation the studio reads them and drives the renderer, so the sky,
// clouds, lighting and render setup are all node-driven and saved with the
// project like any other node.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"

namespace gpx {

REGISTER_NODE(
    SunLight, "Atmosphere", "Sun: direction (manual or geographic), color, intensity",
    [](Node &n) {
      n.add_out("sun");
      add_choice(n.attrs, "mode", "Direction mode", {"Manual", "Location & time"}, 0,
                 "Direction")
          .tooltip = "Whether the sun is placed by hand, or worked out from a\n"
                     "place and a time. By date and time is what you want when\n"
                     "the shot has to match a real location and hour.";
      add_float(n.attrs, "azimuth", "Azimuth", 135.f, 0.f, 360.f, "Direction")
          .tooltip = "Which way the sun lies, in degrees around the compass.";
      add_float(n.attrs, "altitude", "Altitude", 35.f, 1.f, 89.f, "Direction")
          .tooltip = "How high the sun stands above the horizon. Low light is\n"
                     "long shadows and warm colour; overhead is short shadows\n"
                     "and flat ground.";
      add_float(n.attrs, "latitude", "Latitude", 40.7f, -89.f, 89.f, "Location")
          .tooltip = "Where on the earth the scene is, north or south. With the\n"
                     "date and hour this decides where the sun actually sits.";
      add_float(n.attrs, "longitude", "Longitude", -111.9f, -180.f, 180.f, "Location")
          .tooltip = "Where on the earth the scene is, east or west.";
      add_float(n.attrs, "utc_offset", "UTC offset", -7.f, -12.f, 14.f, "Location")
          .tooltip = "The time zone, so the hour below means local clock time.";
      add_int(n.attrs, "month", "Month", 6, 1, 12, "Location")
          .tooltip = "The month, which sets how high the sun can climb at this\n"
                     "latitude.";
      add_int(n.attrs, "day", "Day", 21, 1, 31, "Location")
          .tooltip = "The day of the month.";
      add_float(n.attrs, "hour", "Local time", 14.f, 0.f, 24.f, "Location")
          .tooltip = "The hour of the day, local time.";
      add_float(n.attrs, "intensity", "Intensity", 2.6f, 0.2f, 8.f, "Light")
          .tooltip = "How bright the sun is.";
      add_float(n.attrs, "color_r", "Color R", 1.f, 0.f, 1.f, "Light")
          .tooltip = "The red component of the sunlight, linear rather than\n"
                     "sRGB.";
      add_float(n.attrs, "color_g", "Color G", 0.93f, 0.f, 1.f, "Light")
          .tooltip = "The green component of the sunlight, linear rather than\n"
                     "sRGB.";
      add_float(n.attrs, "color_b", "Color B", 0.82f, 0.f, 1.f, "Light")
          .tooltip = "The blue component of the sunlight, linear rather than\n"
                     "sRGB.";
      add_bool(n.attrs, "shadows", "Cast shadows", true, "Light")
          .tooltip = "Whether the sun casts shadows. Turning them off is a great\n"
                     "deal faster and makes the terrain's form much harder to\n"
                     "read.";
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    AtmosphereSettings, "Atmosphere", "Sky colors, density, haze/fog and light absorption",
    [](Node &n) {
      // where the stack of CloudLayer nodes plugs in
      n.add_in("clouds", DataType::Heightmap, true);
      n.add_out("atmosphere");
      add_float(n.attrs, "density", "Atmosphere density", 1.f, 0.05f, 3.f, "Sky")
          .tooltip = "How thick the air is. It reddens the sun near the horizon\n"
                     "and washes distance out to blue - the single strongest cue\n"
                     "of scale in a landscape, because it tells the eye how far\n"
                     "away a ridge is.";
      add_float(n.attrs, "height_km", "Atmosphere height (km)", 100.f, 0.f, 100000.f, "Sky")
          .tooltip = "How high the air reaches over the world's surface, in\n"
                     "kilometres, whatever its shape - a globe, a ring, a flat\n"
                     "world. Beyond it is space: from high enough the sky thins\n"
                     "to stars and the world shows a blue rim. 0 keeps sky\n"
                     "everywhere.";
      add_float(n.attrs, "ambient", "Ambient light", 0.7f, 0.f, 2.f, "Sky")
          .tooltip = "How much light the sky itself throws down. This is what\n"
                     "fills the shadows; too little and they read as black holes\n"
                     "rather than shade.";
      add_float(n.attrs, "zenith_r", "Zenith R", 0.18f, 0.f, 1.f, "Sky")
          .tooltip = "The red component of the sky straight overhead, linear\n"
                     "rather than sRGB.";
      add_float(n.attrs, "zenith_g", "Zenith G", 0.32f, 0.f, 1.f, "Sky")
          .tooltip = "The green component of the sky straight overhead, linear\n"
                     "rather than sRGB.";
      add_float(n.attrs, "zenith_b", "Zenith B", 0.58f, 0.f, 1.f, "Sky")
          .tooltip = "The blue component of the sky straight overhead, linear\n"
                     "rather than sRGB.";
      add_float(n.attrs, "horizon_r", "Horizon R", 0.62f, 0.f, 1.f, "Sky")
          .tooltip = "The red component of the sky at the horizon, linear rather\n"
                     "than sRGB.";
      add_float(n.attrs, "horizon_g", "Horizon G", 0.65f, 0.f, 1.f, "Sky")
          .tooltip = "The green component of the sky at the horizon, linear\n"
                     "rather than sRGB.";
      add_float(n.attrs, "horizon_b", "Horizon B", 0.70f, 0.f, 1.f, "Sky")
          .tooltip = "The blue component of the sky at the horizon, linear\n"
                     "rather than sRGB.";
      add_choice(n.attrs, "fog_type", "Fog type", {"Off", "Haze", "Fog", "Pollution"},
                 1, "Fog")
          .tooltip = "How the fog is distributed. Uniform fills the air evenly;\n"
                     "the height-based kinds pool it in the valleys and leave\n"
                     "the peaks clear, which is what morning mist actually does.";
      add_float(n.attrs, "fog_density", "Fog density", 0.9f, 0.f, 6.f, "Fog")
          .tooltip = "How thick the fog is.";
      add_float(n.attrs, "fog_level", "Fog level", 0.25f, 0.f, 1.f, "Fog")
          .tooltip = "The height the fog sits at, for the height-based kinds.\n"
                     "Below it the air is thick, above it clear.";
      add_float(n.attrs, "fog_falloff", "Vertical falloff", 6.f, 0.5f, 24.f, "Fog")
          .tooltip = "How quickly the fog thins above its level. Sharp gives a\n"
                     "defined top surface with peaks standing out of it; soft\n"
                     "gives a haze that fades away.";
      add_float(n.attrs, "fog_r", "Fog R", 0.55f, 0.f, 1.f, "Fog")
          .tooltip = "The red component of the fog, linear rather than sRGB.";
      add_float(n.attrs, "fog_g", "Fog G", 0.63f, 0.f, 1.f, "Fog")
          .tooltip = "The green component of the fog, linear rather than sRGB.";
      add_float(n.attrs, "fog_b", "Fog B", 0.75f, 0.f, 1.f, "Fog")
          .tooltip = "The blue component of the fog, linear rather than sRGB.";
      add_float(n.attrs, "fog_scatter", "Sun scattering", 0.5f, 0.f, 1.f, "Fog")
          .tooltip = "How much the fog glows toward the sun. This is what makes\n"
                     "looking into a misty sunrise bright and looking away from\n"
                     "it flat, and without it fog reads as grey paint.";
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    CloudLayer, "Atmosphere", "Volumetric cloud layer: type, coverage, altitude, wind",
    [](Node &n) {
      // Layers chain: a CloudLayer takes the layers below it and adds its own,
      // so any number stack toward the AtmosphereSettings' clouds input. Every
      // CloudLayer in the graph is drawn whether or not it is wired - the wire
      // is the order they are shown in, not a switch.
      n.add_in("clouds", DataType::Heightmap, true);
      n.add_out("clouds");
      add_bool(n.attrs, "enabled", "Enabled", true, "Layer")
          .tooltip = "Turns the cloud layer off without losing its settings.";
      add_choice(n.attrs, "type", "Cloud type",
                 {"Stratus", "Cumulus", "Cumulonimbus"}, 1, "Layer")
          .tooltip = "The kind of cloud. Each has its own shape and altitude\n"
                     "behaviour - flat sheets, heaped cumulus, high wisps.";
      add_float(n.attrs, "coverage", "Coverage", 0.55f, 0.f, 1.f, "Layer")
          .tooltip = "0 = clear sky, 1 = solid overcast.";
      add_float(n.attrs, "density", "Density", 1.f, 0.1f, 3.f, "Layer")
          .tooltip = "How opaque the cloud is. Thin lets the sun through and\n"
                     "lights the cloud from within; thick blocks it and casts\n"
                     "shadow on the ground.";
      add_float(n.attrs, "altitude", "Base altitude", 1.4f, 0.2f, 4.f, "Layer")
          .tooltip = "How high the layer sits.";
      add_float(n.attrs, "thickness", "Thickness", 0.8f, 0.05f, 2.f, "Layer")
          .tooltip = "How deep the layer is from base to top. Depth is what lets\n"
                     "a cloud be lit brightly on top and dark underneath.";
      add_float(n.attrs, "detail", "Detail erosion", 0.6f, 0.f, 1.f, "Shape")
          .tooltip = "How much fine structure the cloud has. Low gives soft\n"
                     "blobs; high gives the wispy, torn edges of real cloud.";
      add_float(n.attrs, "anvil", "Anvil spread", 0.3f, 0.f, 1.f, "Shape")
          .tooltip = "How far the cloud spreads out at its top, the way a storm\n"
                     "cell flattens against the top of the troposphere.";
      add_float(n.attrs, "wind_speed", "Wind speed", 0.02f, 0.f, 0.3f, "Motion")
          .tooltip = "How fast the layer drifts. Cloud is the only thing in a\n"
                     "still landscape that moves, so this is what makes a\n"
                     "sequence read as time passing.";
      add_float(n.attrs, "wind_dir", "Wind direction", 45.f, 0.f, 360.f, "Motion")
          .tooltip = "Which way the layer drifts.";
      add_float(n.attrs, "ambient", "Sky light", 0.55f, 0.f, 2.f, "Lighting")
          .tooltip = "How much sky light the cloud picks up where the sun does\n"
                     "not reach it directly, which sets how dark its underside\n"
                     "goes.";
      add_float(n.attrs, "color_r", "Color R", 1.f, 0.f, 1.f, "Lighting")
          .tooltip = "The red component of the cloud, linear rather than sRGB.";
      add_float(n.attrs, "color_g", "Color G", 1.f, 0.f, 1.f, "Lighting")
          .tooltip = "The green component of the cloud, linear rather than sRGB.";
      add_float(n.attrs, "color_b", "Color B", 1.f, 0.f, 1.f, "Lighting")
          .tooltip = "The blue component of the cloud, linear rather than sRGB.";
      add_choice(n.attrs, "quality", "Quality", {"Draft", "Normal", "High"}, 1,
                 "Lighting")
          .tooltip = "How many samples the raymarcher takes through the cloud.\n"
                     "This is the direct trade between how solid the cloud looks\n"
                     "and how fast the frame draws.";
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    WaterLayer, "Atmosphere", "Water body: level, colors, waves and foam",
    [](Node &n) {
      n.add_out("water");
      add_bool(n.attrs, "enabled", "Enabled", true, "Body")
          .tooltip = "Turns the water off without losing its settings.";
      add_float(n.attrs, "level", "Level", 0.08f, 0.f, 1.f, "Body")
          .tooltip = "The height the water surface sits at.";
      add_float(n.attrs, "clarity", "Clarity", 18.f, 1.f, 60.f, "Body")
          .tooltip = "How far light travels into the water before it is\n"
                     "absorbed. Clear water shows the bed in the shallows and\n"
                     "goes deep blue further out; murky water hides the bed at\n"
                     "once.";
      add_float(n.attrs, "opacity", "Opacity", 0.92f, 0.3f, 1.f, "Body")
          .tooltip = "How much the surface hides what is beneath it.";
      add_float(n.attrs, "deep_r", "Deep R", 0.02f, 0.f, 1.f, "Color")
          .tooltip = "The red component of deep water, linear rather than sRGB.";
      add_float(n.attrs, "deep_g", "Deep G", 0.08f, 0.f, 1.f, "Color")
          .tooltip = "The green component of deep water, linear rather than\n"
                     "sRGB.";
      add_float(n.attrs, "deep_b", "Deep B", 0.12f, 0.f, 1.f, "Color")
          .tooltip = "The blue component of deep water, linear rather than sRGB.";
      add_float(n.attrs, "shallow_r", "Shallow R", 0.10f, 0.f, 1.f, "Color")
          .tooltip = "The red component of shallow water, linear rather than\n"
                     "sRGB.";
      add_float(n.attrs, "shallow_g", "Shallow G", 0.26f, 0.f, 1.f, "Color")
          .tooltip = "The green component of shallow water, linear rather than\n"
                     "sRGB.";
      add_float(n.attrs, "shallow_b", "Shallow B", 0.36f, 0.f, 1.f, "Color")
          .tooltip = "The blue component of shallow water, linear rather than\n"
                     "sRGB.";
      add_float(n.attrs, "wave_amp", "Wave amplitude", 1.f, 0.f, 4.f, "Waves")
          .tooltip = "How high the waves stand.";
      add_float(n.attrs, "wave_scale", "Wave scale", 1.f, 0.2f, 6.f, "Waves")
          .tooltip = "How far apart the waves are. Together with the height this\n"
                     "is what sets the apparent size of the body of water: small\n"
                     "close-set waves read as a pond however wide it is.";
      add_float(n.attrs, "wave_speed", "Wave speed", 1.f, 0.f, 5.f, "Waves")
          .tooltip = "How fast the waves travel.";
      add_bool(n.attrs, "foam", "Foam", true, "Foam")
          .tooltip = "Turns foam on at the shoreline and the wave crests.";
      add_float(n.attrs, "foam_amount", "Shoreline foam", 0.6f, 0.f, 2.f, "Foam")
          .tooltip = "How much foam there is.";
      add_float(n.attrs, "foam_crests", "Crest foam", 0.35f, 0.f, 1.f, "Foam")
          .tooltip = "How much foam appears on the wave tops as against at the\n"
                     "shore. Open water foams on its crests; a beach foams where\n"
                     "the water meets the land.";
      add_float(n.attrs, "foam_scale", "Foam scale", 3.f, 0.5f, 10.f, "Foam")
          .tooltip = "How fine the foam's own texture is.";
      // Vue's Water Surface Options, in physical units (gpx/water_waves.hpp)
      add_bool(n.attrs, "displaced", "Displaced water surface", true, "Waves")
          .tooltip = "The waves are real geometry that stands up against the sky;\n"
                     "off, the sea is flat and the waves are in its shading only.";
      add_float(n.attrs, "wind_speed", "Wind intensity (m/s)", 4.f, 0.f, 25.f, "Waves")
          .tooltip = "The wind the sea has been under: it decides how long the\n"
                     "waves are and how much of each size there is. 4 m/s is a\n"
                     "breeze on a lake, 15 a gale, 0 a mirror.";
      add_float(n.attrs, "wind_dir", "Wind direction", 30.f, 0.f, 360.f, "Waves")
          .tooltip = "The way the waves run seen from above, in degrees: 0 toward\n"
                     "+X, 90 toward +Z.";
      add_float(n.attrs, "choppiness", "Choppiness", 0.5f, 0.f, 1.f, "Waves")
          .tooltip = "0 is round swells; toward 1 the crests stand up sharp, which\n"
                     "is where they break and foam.";
      add_float(n.attrs, "foam_depth", "Typical depth (m)", 1.5f, 0.05f, 10.f, "Foam")
          .tooltip = "How shallow the water has to get before foam gathers along\n"
                     "the coast.";
      add_float(n.attrs, "foam_coverage", "Crest coverage", 0.4f, 0.f, 1.f, "Foam")
          .tooltip = "How much of each breaking crest turns white.";
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    RenderCamera, "Render", "Camera and tone mapping for the render",
    [](Node &n) {
      n.add_out("camera");
      add_float(n.attrs, "exposure", "Exposure", 1.1f, 0.3f, 3.f, "Tone")
          .tooltip = "How much light the picture is given, in stops.";
      add_float(n.attrs, "height_scale", "Terrain height scale", 0.22f, 0.02f, 0.8f,
                "Scene")
          .tooltip = "The terrain's vertical scale, so the render matches what\n"
                     "the viewport shows.";
      add_float(n.attrs, "terrain_size_m", "Terrain size (m)", 5000.f, 100.f, 100000.f,
                "Scene")
          .tooltip = "How wide the tile is in metres, so anything sized in real\n"
                     "units renders at the right size.";
    },
    [](Node &n) { (void)n; })

REGISTER_NODE(
    RenderQuality, "Render", "Offline render engine, resolution and sampling",
    [](Node &n) {
      n.add_out("quality");
      add_choice(n.attrs, "engine", "Engine",
                 {"Mitsuba 3", "Blender Cycles", "LuxCoreRender", "OpenGL viewport"},
                 0, "Engine")
          .tooltip = "Which renderer produces the frame.";
      add_int(n.attrs, "width", "Width", 1920, 64, 8192, "Output")
          .tooltip = "The width of the rendered image, in pixels.";
      add_int(n.attrs, "height", "Height", 1080, 64, 8192, "Output")
          .tooltip = "The height of the rendered image, in pixels.";
      add_int(n.attrs, "samples", "Samples", 128, 8, 4096, "Output")
          .tooltip = "How many samples each pixel gets. Noise falls with the\n"
                     "square root, so four times the samples is half the noise.";
      add_filename(n.attrs, "path", "Output file", "terraforge_render.png", "Output")
          .tooltip = "Where the finished image is written.";
    },
    [](Node &n) { (void)n; })

} // namespace gpx
