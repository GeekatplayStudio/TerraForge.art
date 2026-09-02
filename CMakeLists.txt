cmake_minimum_required(VERSION 3.20)
project(GeekatplayTerraForge VERSION 2.0.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Floating-point policy.
#
# We used to build with -ffast-math (/fp:fast). That implies -ffinite-math-only,
# which tells the compiler NaN and Inf cannot occur — so GCC folds every
# std::isfinite() to true. Measured with our exact flags: isfinite(NaN) returns
# 1. That deleted, silently:
#   - the NaN/Inf guard in gpx::FieldValue::finite() (engine/gpx/field.hpp)
#   - both "output has NaN/Inf" checks in the universal node contract battery,
#     which apply to every port of every node in the registry
#   - nine more finiteness assertions across the engine tests
# The whole Tier-1 finiteness requirement was passing without testing anything.
#
# It also permits reassociation, which makes the committed golden hashes valid
# only for the exact compiler that produced them.
#
# What it bought, measured on the solvers that dominate a real graph
# (medians of 3, same seeds; -O3 vs -O3 -ffast-math):
#   noise 1024          50.1 -> 49.9 ms
#   hydraulic 512      128.7 -> 125.2 ms
#   thermal 512        253.8 -> 251.7 ms
#   stream power 512   796.1 -> 784.8 ms
#   5-node chain 512   404.3 -> 403.5 ms
# About 1%, inside run-to-run noise. Not worth a safety net.
if(MSVC)
    add_compile_options(/O2 /permissive-)
else()
    add_compile_options(-O3)
endif()

# ---------------------------------------------------------------- dependencies
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad PUBLIC external/glad/include)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

set(IMNE_DIR ${CMAKE_SOURCE_DIR}/external/imgui-node-editor)
add_library(imgui_node_editor STATIC
    ${IMNE_DIR}/imgui_node_editor.cpp
    ${IMNE_DIR}/imgui_node_editor_api.cpp
    ${IMNE_DIR}/imgui_canvas.cpp
    ${IMNE_DIR}/crude_json.cpp
)
target_include_directories(imgui_node_editor PUBLIC ${IMNE_DIR})
target_link_libraries(imgui_node_editor PUBLIC imgui)

add_library(third_party_headers INTERFACE)
target_include_directories(third_party_headers INTERFACE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/glm
)

# ------------------------------------------------------------------ engine lib
set(ENGINE_SOURCES
    src/noise.cpp
    src/erosion.cpp
    src/geology.cpp
    src/materials.cpp
    src/ecosystem.cpp
    src/atmosphere.cpp
    src/dag.cpp
    src/persistence.cpp
    src/mcp_bridge.cpp
    engine/heightmap.cpp
    engine/attribute.cpp
    engine/animation.cpp
    engine/node_graph.cpp
    engine/port_catalog.cpp
    engine/hydrology.cpp
    engine/distance.cpp
    engine/serialization.cpp
    engine/parallel.cpp
    engine/field_glsl.cpp
    engine/field_glsl_emitters.cpp
    engine/metanode.cpp
    engine/nodes/nodes_primitives.cpp
    engine/nodes/nodes_filters.cpp
    engine/nodes/nodes_erosion.cpp
    engine/nodes/nodes_erosion_slope.cpp
    engine/nodes/nodes_erosion_wind.cpp
    engine/nodes/nodes_operators.cpp
    engine/nodes/nodes_masks.cpp
    engine/nodes/nodes_warp.cpp
    engine/nodes/nodes_texture.cpp
    engine/nodes/nodes_materials.cpp
    engine/nodes/nodes_pbr_library.cpp
    engine/nodes/nodes_modeling.cpp
    engine/nodes/nodes_surface.cpp
    engine/nodes/nodes_atmosphere.cpp
    engine/nodes/nodes_material_graph.cpp
    engine/nodes/nodes_hydro.cpp
    engine/nodes/nodes_logic.cpp
    external/miniz/miniz.c
    engine/nodes/nodes_field.cpp
    engine/nodes/nodes_field_shapes.cpp
    engine/nodes/nodes_path.cpp
    engine/nodes/nodes_displace.cpp
    engine/nodes/nodes_analysis.cpp
    engine/nodes/nodes_field_material.cpp
    engine/nodes/nodes_terrain_fx.cpp
    engine/nodes/nodes_export.cpp
)

add_library(geekatplay_nodeterrain STATIC ${ENGINE_SOURCES})
target_include_directories(geekatplay_nodeterrain PUBLIC include engine)
target_link_libraries(geekatplay_nodeterrain PUBLIC third_party_headers)
if(WIN32)
    target_link_libraries(geekatplay_nodeterrain PUBLIC urlmon)
endif()
if(NOT MSVC)
    target_compile_options(geekatplay_nodeterrain PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------------------ studio app
set(STUDIO_SOURCES
    studio/main.cpp
    studio/app.cpp
    studio/theme.cpp
    studio/theme_colors.cpp
    studio/icons.cpp
    studio/panel_graph.cpp
    studio/panel_graph_draw.cpp
    studio/panel_properties.cpp
    studio/panel_properties_object.cpp
    studio/panel_properties_node.cpp
    studio/panel_library.cpp
    studio/panel_nodelist.cpp
    studio/panel_viewport.cpp
    studio/toolbar.cpp
    studio/dialog_new.cpp
    studio/autosave.cpp
    studio/autosave_ui.cpp
    studio/gizmo.cpp
    studio/toolbar_bars.cpp
    studio/terrain_styles.cpp
    studio/undo.cpp
    studio/sculpt.cpp
    studio/renderer.cpp
    studio/renderer_camera.cpp
    studio/renderer_scene.cpp
    studio/renderer_programs.cpp
    studio/renderer_init.cpp
    studio/renderer_export.cpp
    studio/shaders_terrain.cpp
    studio/terrain_cull.cpp
    studio/gpu_timer.cpp
    studio/blue_noise.cpp
    studio/shaders_scene.cpp
    studio/planet_renderer.cpp
    studio/field_gpu_check.cpp
    studio/preview.cpp
    studio/project_io.cpp
    studio/panel_environment.cpp
    studio/panel_ai.cpp
    studio/panel_materials.cpp
    studio/panel_scene.cpp
    studio/panel_console.cpp
    studio/console.cpp
    studio/panel_render.cpp
    studio/scene.cpp
    studio/scene_io.cpp
    studio/render_settings.cpp
    studio/cloud_noise.cpp
    studio/scene_nodes.cpp
    studio/material_library.cpp
    studio/node_library.cpp
    studio/panel_camera.cpp
    studio/ai_assist.cpp
    studio/ai_ops_graph.cpp
    studio/ai_ops_view.cpp
    studio/studio_api.cpp
    studio/i18n.cpp
    studio/file_dialogs.cpp
    studio/prefs.cpp
    studio/ollama.cpp
    studio/resources/app.rc
)

add_executable(geekatplay_studio WIN32 ${STUDIO_SOURCES})
add_custom_command(TARGET geekatplay_studio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/studio/resources
            $<TARGET_FILE_DIR:geekatplay_studio>/resources)
target_include_directories(geekatplay_studio PRIVATE studio)
target_link_libraries(geekatplay_studio PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>"
    imgui imgui_node_editor glad glfw third_party_headers)
if(WIN32)
    target_link_libraries(geekatplay_studio PRIVATE opengl32 winhttp)
endif()
if(NOT MSVC)
    # console visible in debug runs: strip WIN32 flag effect via entry
    set_target_properties(geekatplay_studio PROPERTIES LINK_FLAGS "-mconsole")
endif()

# ------------------------------------------------------------------- cli/tests
add_executable(nodeterrain_cli src/main.cpp)
target_link_libraries(nodeterrain_cli PRIVATE geekatplay_nodeterrain)

add_executable(nodeterrain_tests tests/cpp/test_main.cpp)
target_link_libraries(nodeterrain_tests PRIVATE geekatplay_nodeterrain)

add_executable(engine_tests tests/cpp/test_engine.cpp)
target_link_libraries(engine_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# Undo works on studio state, but none of the state it touches needs a GL
# context, so it tests without the renderer.
add_executable(undo_tests
    tests/cpp/test_undo.cpp
    studio/undo.cpp
    studio/project_io.cpp
    studio/node_library.cpp
    studio/scene.cpp
    studio/scene_io.cpp
    studio/render_settings.cpp
    studio/autosave.cpp
    studio/console.cpp)
target_include_directories(undo_tests PRIVATE studio)
target_link_libraries(undo_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# Regression lock: guards every shipped capability against silent removal.
add_executable(regression_tests tests/cpp/test_regression.cpp)
target_link_libraries(regression_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# Universal node contract, applied to every node in the registry.
add_executable(node_tests tests/cpp/test_nodes.cpp)
target_link_libraries(node_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# Renderer maths that needs no GL context: per-patch visibility, and the
# packing helpers that follow it.
add_executable(render_tests tests/cpp/test_render.cpp studio/terrain_cull.cpp
                            studio/blue_noise.cpp)
target_include_directories(render_tests PRIVATE studio)
target_link_libraries(render_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

# The node reference generator: docs/NODES.md straight from the registry.
add_executable(node_docs_gen tools/gen_node_docs.cpp)
target_link_libraries(node_docs_gen PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,geekatplay_nodeterrain>" third_party_headers)

enable_testing()
add_test(NAME RegressionLock COMMAND regression_tests)
add_test(NAME NodeContract COMMAND node_tests)
add_test(NAME AllTests COMMAND nodeterrain_tests)
add_test(NAME EngineTests COMMAND engine_tests)
add_test(NAME UndoTests COMMAND undo_tests)
add_test(NAME RenderTests COMMAND render_tests)












