// Geekatplay TerraForge — icon glyphs: tools, files, objects and the viewport.
//
// Cinema 4D's language: a tool is a thin line drawing with small filled
// arrow heads (Move is the four-way cross, Rotate the circular arrow, Scale
// a box with a diagonal arrow out of its corner); an object type is a simple
// filled silhouette; a state is a small filled mark. Nothing decorative.
#include "icons_pen.hpp"
#include "theme_colors.hpp"

namespace studio {

bool paint_glyphs_a(const Pen &k, Icon ic) {
  const float PI = ICON_PI;
  switch (ic) {
    // ---- edit ----
    case Icon::Undo: { // an arrow curling back to the left
      Pen t = k.thick();
      t.arc(0.05f, 0.15f, 0.6f, PI * 1.0f, PI * 1.95f);
      t.line(0.65f, 0.15f, 0.65f, 0.45f);
      t.head(-0.6f, 0.15f, PI * 0.5f, 0.42f);
      return true;
    }
    case Icon::Redo: {
      Pen t = k.thick();
      t.arc(-0.05f, 0.15f, 0.6f, PI * 1.05f, PI * 2.0f);
      t.line(-0.65f, 0.15f, -0.65f, 0.45f);
      t.head(0.6f, 0.15f, PI * 0.5f, 0.42f);
      return true;
    }
    case Icon::Refresh: { // a near-full circle closed by an arrow head
      Pen t = k.thick();
      t.arc(0.f, 0.f, 0.6f, PI * 0.1f, PI * 1.7f);
      t.head(0.62f, -0.2f, PI * 0.45f, 0.42f);
      return true;
    }
    case Icon::Brush: // a round tip on an angled handle, the tip in the tool colour
      k.thick().line(-0.75f, 0.75f, 0.f, 0.f);
      k.hi().circle(0.35f, -0.35f, 0.4f, true);
      k.lit().circle(0.25f, -0.45f, 0.14f, true);
      return true;
    // ---- files ----
    case Icon::Save: // a floppy: shutter on top, label below
      k.poly({-0.75f, -0.75f, 0.45f, -0.75f, 0.75f, -0.45f, 0.75f, 0.75f,
              -0.75f, 0.75f}, true);
      k.rect(-0.4f, -0.75f, 0.3f, -0.3f, true);
      k.rect(-0.4f, 0.2f, 0.4f, 0.75f);
      return true;
    case Icon::Open: // a folder with its front flap tilted
      k.poly({-0.8f, -0.55f, -0.25f, -0.55f, -0.05f, -0.3f, 0.7f, -0.3f,
              0.7f, 0.65f, -0.8f, 0.65f}, true);
      k.line(-0.6f, 0.1f, 0.9f, 0.1f);
      k.line(0.9f, 0.1f, 0.7f, 0.65f);
      return true;
    case Icon::Folder:
      k.poly({-0.8f, -0.55f, -0.25f, -0.55f, -0.05f, -0.3f, 0.8f, -0.3f,
              0.8f, 0.65f, -0.8f, 0.65f}, true);
      return true;
    case Icon::Trash: // a bin with a lid and handle
      k.hi().line(-0.75f, -0.45f, 0.75f, -0.45f);
      k.hi().line(-0.2f, -0.7f, 0.2f, -0.7f);
      k.poly({-0.55f, -0.45f, -0.45f, 0.75f, 0.45f, 0.75f, 0.55f, -0.45f});
      k.line(-0.15f, -0.15f, -0.15f, 0.5f);
      k.line(0.15f, -0.15f, 0.15f, 0.5f);
      return true;
    case Icon::Gear: // a ring with six square teeth
      k.circle(0.f, 0.f, 0.42f);
      k.circle(0.f, 0.f, 0.14f, true);
      for (int i = 0; i < 6; ++i) {
        float a = PI * i / 3.f;
        k.line(std::cos(a) * 0.42f, std::sin(a) * 0.42f, std::cos(a) * 0.78f,
               std::sin(a) * 0.78f);
      }
      return true;
    case Icon::Search:
      k.circle(-0.15f, -0.15f, 0.48f);
      k.line(0.2f, 0.2f, 0.75f, 0.75f);
      return true;
    case Icon::Plus:
      k.line(-0.65f, 0.f, 0.65f, 0.f);
      k.line(0.f, -0.65f, 0.f, 0.65f);
      return true;
    case Icon::Minus:
      k.line(-0.65f, 0.f, 0.65f, 0.f);
      return true;
    case Icon::Chevron: // the tree-fold triangle, right = closed
      k.tri(-0.25f, -0.5f, -0.25f, 0.5f, 0.4f, 0.f);
      return true;
    case Icon::ChevronDown:
      k.tri(-0.5f, -0.25f, 0.5f, -0.25f, 0.f, 0.4f);
      return true;
    case Icon::Link: // two chain links, joined
      k.arc(-0.3f, 0.f, 0.36f, PI * 0.5f, PI * 1.5f);
      k.arc(0.3f, 0.f, 0.36f, PI * 1.5f, PI * 2.5f);
      k.line(-0.3f, -0.36f, 0.f, -0.36f);
      k.line(0.f, 0.36f, 0.3f, 0.36f);
      k.line(-0.25f, 0.f, 0.25f, 0.f);
      return true;
    case Icon::Unlink:
      k.arc(-0.36f, 0.f, 0.36f, PI * 0.5f, PI * 1.5f);
      k.arc(0.36f, 0.f, 0.36f, PI * 1.5f, PI * 2.5f);
      k.line(-0.36f, -0.36f, -0.15f, -0.36f);
      k.line(0.15f, 0.36f, 0.36f, 0.36f);
      k.line(0.f, -0.7f, 0.f, -0.35f);
      k.line(0.f, 0.35f, 0.f, 0.7f);
      return true;
    case Icon::Lock: // a padlock, shackle closed
      k.hi().rect(-0.6f, -0.05f, 0.6f, 0.75f, true);
      k.arc(0.f, -0.2f, 0.36f, PI, PI * 2.f);
      k.line(-0.36f, -0.2f, -0.36f, -0.05f);
      k.line(0.36f, -0.2f, 0.36f, -0.05f);
      return true;
    case Icon::Unlock: // shackle swung open to the right
      k.hi().rect(-0.6f, -0.05f, 0.6f, 0.75f, true);
      k.arc(0.35f, -0.35f, 0.36f, PI, PI * 2.f);
      k.line(-0.01f, -0.35f, -0.01f, -0.05f);
      k.line(0.71f, -0.35f, 0.71f, -0.2f);
      return true;
    // ---- transform tools ----
    case Icon::Move: // the four-way cross, filled arrow heads in the tool colour
      k.thick().line(-0.5f, 0.f, 0.5f, 0.f);
      k.thick().line(0.f, -0.5f, 0.f, 0.5f);
      k.hi().head(0.9f, 0.f, 0.f, 0.45f);
      k.hi().head(-0.9f, 0.f, PI, 0.45f);
      k.hi().head(0.f, -0.9f, -PI * 0.5f, 0.45f);
      k.hi().head(0.f, 0.9f, PI * 0.5f, 0.45f);
      return true;
    case Icon::Rotate: // a heavy circular arrow, open at the top right
      k.hi().thick().arc(0.f, 0.f, 0.6f, PI * 1.85f, PI * 3.2f);
      k.hi().head(0.62f, -0.22f, -PI * 0.55f, 0.46f);
      k.circle(0.f, 0.f, 0.16f, true);
      return true;
    case Icon::Scale: // a solid box with an arrow out of its top-right corner
      k.hi().rect(-0.8f, -0.05f, 0.05f, 0.8f, true);
      k.lit().rect(-0.8f, -0.05f, 0.05f, 0.1f, true);
      k.thick().line(0.15f, -0.15f, 0.55f, -0.55f);
      k.head(0.85f, -0.85f, -PI * 0.25f, 0.42f);
      return true;
    // ---- object types: filled silhouettes ----
    case Icon::Object: // a solid cube: lit top, front in the colour, side in shadow
      k.lit().fill({-0.7f, -0.25f, -0.25f, -0.7f, 0.7f, -0.7f, 0.25f, -0.25f});
      k.hi().rect(-0.7f, -0.25f, 0.25f, 0.7f, true);
      k.dark().fill({0.25f, -0.25f, 0.7f, -0.7f, 0.7f, 0.25f, 0.25f, 0.7f});
      return true;
    // The rest of the primitive set, each drawn as the solid it makes. They
    // used to share the cube glyph with a letter in the corner, which is a
    // label rather than an icon: you had to read it to tell a sphere from a
    // cone. Same three tones as the cube - lit where the light falls, the
    // functional colour on the body, shadow underneath - so the set reads as
    // one family.
    case Icon::Sphere: // a ball: a terminator arc and a specular highlight
      k.hi().circle(0.f, 0.f, 0.74f, true);
      k.dark().thick(2.2f).arc(0.f, 0.f, 0.58f, -ICON_PI * 0.28f,
                               ICON_PI * 0.60f);
      k.lit().circle(-0.26f, -0.28f, 0.20f, true);
      return true;
    case Icon::Plane: // a flat quad seen at an angle, with a grid across it
      // Opened out from a flatter rhombus, which at menu size read as a
      // sliver rather than as a surface. The near edges carry the shadow, so
      // it still reads as a slab lying down when a menu draws it in one tone.
      k.hi().fill({-0.92f, 0.10f, 0.f, -0.55f, 0.92f, -0.10f, 0.f, 0.55f});
      k.lit().line(-0.46f, 0.33f, 0.46f, -0.33f);
      k.lit().line(-0.46f, -0.22f, 0.46f, 0.22f);
      k.dark().poly({-0.92f, 0.10f, 0.f, 0.55f, 0.92f, -0.10f});
      return true;
    case Icon::Cylinder: // a tube: shaded base, body, lit cap
      k.dark().ellipse(0.f, 0.50f, 0.52f, 0.22f, true);
      k.hi().fill({-0.52f, -0.42f, 0.52f, -0.42f, 0.52f, 0.50f, -0.52f, 0.50f});
      k.lit().ellipse(0.f, -0.42f, 0.52f, 0.22f, true);
      return true;
    case Icon::Cone: // a cone: shaded base, the near face lit down one side
      k.dark().ellipse(0.f, 0.52f, 0.60f, 0.24f, true);
      k.hi().tri(0.f, -0.70f, -0.60f, 0.52f, 0.60f, 0.52f);
      k.lit().tri(0.f, -0.70f, -0.60f, 0.52f, 0.f, 0.52f);
      return true;
    case Icon::Mesh: // the same cube with its wire showing
      k.lit().fill({-0.7f, -0.25f, -0.25f, -0.7f, 0.7f, -0.7f, 0.25f, -0.25f});
      k.hi().rect(-0.7f, -0.25f, 0.25f, 0.7f, true);
      k.dark().fill({0.25f, -0.25f, 0.7f, -0.7f, 0.7f, 0.25f, 0.25f, 0.7f});
      k.tone(IM_COL32(255, 255, 255, 210)).line(-0.7f, 0.7f, 0.25f, -0.25f);
      k.tone(IM_COL32(255, 255, 255, 210)).line(-0.7f, 0.22f, 0.25f, 0.22f);
      return true;
    case Icon::Camera: // a solid body with a lens barrel to the right and a lit lens
      k.hi().rect(-0.85f, -0.3f, 0.3f, 0.55f, true);
      k.lit().rect(-0.85f, -0.3f, 0.3f, -0.15f, true);
      k.hi().tri(0.3f, 0.1f, 0.85f, -0.35f, 0.85f, 0.55f);
      k.dark().rect(-0.6f, -0.6f, -0.15f, -0.3f, true);
      k.tone(IM_COL32(255, 255, 255, 220)).circle(-0.3f, 0.12f, 0.2f, true);
      return true;
    case Icon::Light: // a bulb: a lit disc with rays
      k.hi().dot(0.f, 0.f, 0.3f);
      k.lit().dot(-0.08f, -0.08f, 0.12f);
      for (int i = 0; i < 8; ++i) {
        float a = PI * i / 4.f;
        k.hi().line(std::cos(a) * 0.48f, std::sin(a) * 0.48f, std::cos(a) * 0.8f,
                    std::sin(a) * 0.8f);
      }
      return true;
    case Icon::Terrain: // two solid peaks, the far one in shadow, snow on the near one
      k.dark().tri(0.f, 0.7f, 0.5f, -0.2f, 0.95f, 0.7f);
      k.hi().tri(-0.95f, 0.7f, -0.25f, -0.7f, 0.5f, 0.7f);
      k.tone(IM_COL32(255, 255, 255, 230)).tri(-0.25f, -0.7f, -0.45f, -0.3f, -0.05f, -0.3f);
      return true;
    case Icon::Planet: // a solid globe with a ring, lit from the top left
      k.hi().circle(0.f, 0.f, 0.5f, true);
      k.lit().circle(-0.15f, -0.15f, 0.22f, true);
      k.thick().arc(0.f, 0.f, 0.82f, PI * 1.08f, PI * 1.92f);
      k.thick().arc(0.f, 0.f, 0.82f, PI * 0.08f, PI * 0.92f);
      return true;
    case Icon::World: // a globe with meridian and equator
      k.circle(0.f, 0.f, 0.68f);
      k.line(-0.68f, 0.f, 0.68f, 0.f);
      k.line(0.f, -0.68f, 0.f, 0.68f);
      k.arc(-0.34f, 0.f, 0.34f, PI * 1.5f, PI * 2.5f);
      k.arc(0.34f, 0.f, 0.34f, PI * 0.5f, PI * 1.5f);
      return true;
    case Icon::Cloud: { // a solid cloud, lit along the top
      Pen c = k.hi();
      c.circle(-0.35f, 0.15f, 0.34f, true);
      c.circle(0.1f, -0.1f, 0.45f, true);
      c.circle(0.5f, 0.2f, 0.3f, true);
      c.rect(-0.35f, 0.15f, 0.5f, 0.5f, true);
      k.lit().circle(0.05f, -0.2f, 0.2f, true);
      return true;
    }
    case Icon::Sky: // a low sun over the horizon, the sun solid
      k.hi().dl->PathClear();
      k.hi().arc(0.f, 0.3f, 0.4f, PI, PI * 2.f);
      k.hi().fill({-0.4f, 0.3f, -0.28f, 0.02f, 0.f, -0.1f, 0.28f, 0.02f, 0.4f, 0.3f});
      k.thick().line(-0.85f, 0.3f, 0.85f, 0.3f);
      k.line(-0.6f, 0.65f, 0.6f, 0.65f);
      for (int i = 0; i < 3; ++i) {
        float a = PI * (0.25f + i * 0.25f);
        k.hi().line(std::cos(a) * 0.55f, 0.3f - std::sin(a) * 0.55f,
                    std::cos(a) * 0.8f, 0.3f - std::sin(a) * 0.8f);
      }
      return true;
    case Icon::Water: // three heavy waves
      for (int row = 0; row < 3; ++row) {
        float y = -0.5f + row * 0.5f;
        k.hi().thick().arc(-0.4f, y, 0.4f, PI * 1.05f, PI * 1.95f);
        k.hi().thick().arc(0.4f, y, 0.4f, PI * 0.05f, PI * 0.95f);
      }
      return true;
    case Icon::Material: // a shaded ball: the colour, a highlight, a shadow rim
      k.hi().circle(0.f, 0.f, 0.72f, true);
      k.dark().arc(0.f, 0.f, 0.62f, PI * 0.15f, PI * 0.85f);
      k.tone(IM_COL32(255, 255, 255, 170)).circle(-0.25f, -0.28f, 0.2f, true);
      return true;
    case Icon::Node: // a node card with a port either side
      k.rect(-0.55f, -0.45f, 0.55f, 0.45f);
      k.line(-0.55f, -0.15f, 0.55f, -0.15f);
      k.hi().dot(-0.55f, 0.15f, 0.16f);
      k.hi().dot(0.55f, 0.15f, 0.16f);
      return true;
    case Icon::Render: // a solid frame with a play mark
      k.hi().rect(-0.8f, -0.6f, 0.8f, 0.6f, true);
      k.lit().rect(-0.8f, -0.6f, 0.8f, -0.42f, true);
      k.tone(IM_COL32(255, 255, 255, 235)).tri(-0.18f, -0.3f, -0.18f, 0.3f, 0.35f, 0.f);
      return true;
    case Icon::Scene: // a landscape in a frame
      k.rect(-0.75f, -0.6f, 0.75f, 0.6f);
      k.poly({-0.75f, 0.35f, -0.25f, -0.2f, 0.05f, 0.15f, 0.3f, -0.05f, 0.75f, 0.35f});
      k.hi().dot(0.35f, -0.3f, 0.12f);
      return true;
    // ---- viewport: projections ----
    case Icon::ViewPersp: // a frustum
      k.poly({-0.25f, -0.65f, 0.25f, -0.65f, 0.8f, 0.65f, -0.8f, 0.65f}, true);
      k.hi().line(-0.5f, 0.f, 0.5f, 0.f);
      return true;
    case Icon::ViewTop: // the plane edge-on, an arrow coming down onto it
      k.hi().line(-0.8f, 0.6f, 0.8f, 0.6f);
      k.line(0.f, -0.75f, 0.f, 0.05f);
      k.head(0.f, 0.4f, PI * 0.5f, 0.34f);
      return true;
    case Icon::ViewFront:
      k.hi().line(0.6f, -0.8f, 0.6f, 0.8f);
      k.line(-0.75f, 0.f, 0.05f, 0.f);
      k.head(0.4f, 0.f, 0.f, 0.34f);
      return true;
    case Icon::ViewRight:
      k.hi().line(-0.6f, -0.8f, -0.6f, 0.8f);
      k.line(0.75f, 0.f, -0.05f, 0.f);
      k.head(-0.4f, 0.f, PI, 0.34f);
      return true;
    // ---- shading ----
    case Icon::Shaded: // a lit sphere: the shadow half filled
      k.circle(0.f, 0.f, 0.68f);
      k.dl->PathClear();
      k.dl->PathArcTo(k.p(0.f, 0.f), 0.68f * k.r, PI * 1.75f, PI * 2.75f, 24);
      k.dl->PathFillConvex(k.col);
      return true;
    case Icon::Textured: // the sphere with a checker
      k.circle(0.f, 0.f, 0.68f);
      k.rect(-0.48f, -0.48f, 0.f, 0.f, true);
      k.rect(0.f, 0.f, 0.48f, 0.48f, true);
      return true;
    case Icon::Outline: // a filled square with a highlight ring around it
      k.rect(-0.35f, -0.35f, 0.35f, 0.35f, true);
      k.rect(-0.75f, -0.75f, 0.75f, 0.75f);
      return true;
    case Icon::Wireframe: // a quad split into triangles
      k.rect(-0.7f, -0.7f, 0.7f, 0.7f);
      k.line(-0.7f, 0.7f, 0.7f, -0.7f);
      k.line(-0.7f, 0.f, 0.f, 0.7f);
      k.line(0.f, -0.7f, 0.7f, 0.f);
      return true;
    case Icon::Grid: // a solid ground plane seen at an angle, with its lines
      k.hi().fill({-0.9f, 0.6f, -0.45f, -0.4f, 0.45f, -0.4f, 0.9f, 0.6f});
      k.dark().line(-0.68f, 0.1f, 0.68f, 0.1f);
      k.dark().line(-0.15f, -0.4f, -0.3f, 0.6f);
      k.dark().line(0.15f, -0.4f, 0.3f, 0.6f);
      return true;
    case Icon::Eye:
      k.arc(0.f, 0.4f, 0.8f, PI * 1.2f, PI * 1.8f);
      k.arc(0.f, -0.4f, 0.8f, PI * 0.2f, PI * 0.8f);
      k.dot(0.f, 0.f, 0.22f);
      return true;
    case Icon::EyeOff:
      k.arc(0.f, 0.4f, 0.8f, PI * 1.2f, PI * 1.8f);
      k.arc(0.f, -0.4f, 0.8f, PI * 0.2f, PI * 0.8f);
      k.line(-0.65f, 0.65f, 0.65f, -0.65f);
      return true;
    // ---- windows ----
    case Icon::Detach: // a window leaving through the top-right corner
      k.poly({-0.1f, -0.35f, -0.75f, -0.35f, -0.75f, 0.75f, 0.35f, 0.75f, 0.35f, 0.1f});
      k.line(0.1f, -0.75f, 0.75f, -0.75f);
      k.line(0.75f, -0.75f, 0.75f, -0.1f);
      k.line(0.75f, -0.75f, -0.05f, 0.05f);
      return true;
    case Icon::Dock: // a window settling into the frame
      k.rect(-0.75f, -0.75f, 0.75f, 0.75f);
      k.line(-0.75f, -0.3f, 0.75f, -0.3f);
      k.line(-0.25f, -0.3f, -0.25f, 0.75f);
      return true;
    default:
      return false;
  }
}

} // namespace studio
