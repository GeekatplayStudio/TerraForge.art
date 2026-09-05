// Geekatplay TerraForge — icon glyphs: tools, files, objects and the viewport.
//
// Cinema 4D's language: a tool is a thin line drawing with small filled
// arrow heads (Move is the four-way cross, Rotate the circular arrow, Scale
// a box with a diagonal arrow out of its corner); an object type is a simple
// filled silhouette; a state is a small filled mark. Nothing decorative.
#include "icons_pen.hpp"

namespace studio {

bool paint_glyphs_a(const Pen &k, Icon ic) {
  const float PI = ICON_PI;
  switch (ic) {
    // ---- edit ----
    case Icon::Undo: // an arrow curling back to the left
      k.arc(0.05f, 0.15f, 0.6f, PI * 1.0f, PI * 1.95f);
      k.line(0.65f, 0.15f, 0.65f, 0.45f);
      k.head(-0.55f, 0.15f, PI * 0.5f, 0.34f);
      return true;
    case Icon::Redo:
      k.arc(-0.05f, 0.15f, 0.6f, PI * 1.05f, PI * 2.0f);
      k.line(-0.65f, 0.15f, -0.65f, 0.45f);
      k.head(0.55f, 0.15f, PI * 0.5f, 0.34f);
      return true;
    case Icon::Refresh: // a near-full circle closed by an arrow head
      k.arc(0.f, 0.f, 0.62f, PI * 0.1f, PI * 1.7f);
      k.head(0.62f, -0.2f, PI * 0.45f, 0.34f);
      return true;
    case Icon::Brush: // a round brush on an angled handle
      k.line(-0.7f, 0.7f, 0.05f, -0.05f);
      k.circle(0.35f, -0.35f, 0.34f, true);
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
      k.line(-0.75f, -0.45f, 0.75f, -0.45f);
      k.line(-0.2f, -0.7f, 0.2f, -0.7f);
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
      k.rect(-0.6f, -0.05f, 0.6f, 0.75f, true);
      k.arc(0.f, -0.2f, 0.36f, PI, PI * 2.f);
      k.line(-0.36f, -0.2f, -0.36f, -0.05f);
      k.line(0.36f, -0.2f, 0.36f, -0.05f);
      return true;
    case Icon::Unlock: // shackle swung open to the right
      k.rect(-0.6f, -0.05f, 0.6f, 0.75f, true);
      k.arc(0.35f, -0.35f, 0.36f, PI, PI * 2.f);
      k.line(-0.01f, -0.35f, -0.01f, -0.05f);
      k.line(0.71f, -0.35f, 0.71f, -0.2f);
      return true;
    // ---- transform tools ----
    case Icon::Move: // the four-way arrow cross
      k.line(-0.55f, 0.f, 0.55f, 0.f);
      k.line(0.f, -0.55f, 0.f, 0.55f);
      k.head(0.85f, 0.f, 0.f);
      k.head(-0.85f, 0.f, PI);
      k.head(0.f, -0.85f, -PI * 0.5f);
      k.head(0.f, 0.85f, PI * 0.5f);
      return true;
    case Icon::Rotate: // a circular arrow, open at the top right
      k.arc(0.f, 0.f, 0.6f, PI * 1.85f, PI * 3.2f);
      k.head(0.6f, -0.2f, -PI * 0.55f, 0.36f);
      k.dot(0.f, 0.f, 0.12f);
      return true;
    case Icon::Scale: // a box with an arrow out of its top-right corner
      k.rect(-0.75f, -0.1f, 0.1f, 0.75f);
      k.line(0.1f, -0.1f, 0.5f, -0.5f);
      k.head(0.75f, -0.75f, -PI * 0.25f, 0.34f);
      k.line(0.75f, -0.75f, 0.2f, -0.75f);
      k.line(0.75f, -0.75f, 0.75f, -0.2f);
      return true;
    // ---- object types: filled silhouettes ----
    case Icon::Object: // a cube, front face filled, top and side outlined
      k.rect(-0.7f, -0.25f, 0.25f, 0.7f, true);
      k.poly({-0.7f, -0.25f, -0.25f, -0.7f, 0.7f, -0.7f, 0.25f, -0.25f});
      k.poly({0.7f, -0.7f, 0.7f, 0.25f, 0.25f, 0.7f});
      return true;
    case Icon::Mesh: // the same cube in wire, with a diagonal on the front
      k.rect(-0.7f, -0.25f, 0.25f, 0.7f);
      k.poly({-0.7f, -0.25f, -0.25f, -0.7f, 0.7f, -0.7f, 0.25f, -0.25f});
      k.poly({0.7f, -0.7f, 0.7f, 0.25f, 0.25f, 0.7f});
      k.line(-0.7f, 0.7f, 0.25f, -0.25f);
      return true;
    case Icon::Camera: // a body with a lens barrel to the right
      k.rect(-0.8f, -0.35f, 0.3f, 0.5f, true);
      k.tri(0.3f, 0.05f, 0.8f, -0.35f, 0.8f, 0.5f);
      k.rect(-0.55f, -0.6f, -0.15f, -0.35f, true);
      return true;
    case Icon::Light: // a point light: a filled dot with rays
      k.dot(0.f, 0.f, 0.26f);
      for (int i = 0; i < 8; ++i) {
        float a = PI * i / 4.f;
        k.line(std::cos(a) * 0.45f, std::sin(a) * 0.45f, std::cos(a) * 0.75f,
               std::sin(a) * 0.75f);
      }
      return true;
    case Icon::Terrain: // two peaks, filled
      k.tri(-0.85f, 0.65f, -0.2f, -0.6f, 0.45f, 0.65f);
      k.tri(0.05f, 0.65f, 0.5f, -0.15f, 0.9f, 0.65f);
      return true;
    case Icon::Planet: // a globe with a ring
      k.circle(0.f, 0.f, 0.48f, true);
      k.arc(0.f, 0.f, 0.8f, PI * 1.08f, PI * 1.92f);
      k.arc(0.f, 0.f, 0.8f, PI * 0.08f, PI * 0.92f);
      return true;
    case Icon::World: // a globe with meridian and equator
      k.circle(0.f, 0.f, 0.68f);
      k.line(-0.68f, 0.f, 0.68f, 0.f);
      k.line(0.f, -0.68f, 0.f, 0.68f);
      k.arc(-0.34f, 0.f, 0.34f, PI * 1.5f, PI * 2.5f);
      k.arc(0.34f, 0.f, 0.34f, PI * 0.5f, PI * 1.5f);
      return true;
    case Icon::Cloud:
      k.circle(-0.35f, 0.15f, 0.34f, true);
      k.circle(0.1f, -0.1f, 0.45f, true);
      k.circle(0.5f, 0.2f, 0.3f, true);
      k.rect(-0.35f, 0.15f, 0.5f, 0.5f, true);
      return true;
    case Icon::Sky: // a low sun over the horizon
      k.arc(0.f, 0.3f, 0.4f, PI, PI * 2.f);
      k.line(-0.85f, 0.3f, 0.85f, 0.3f);
      k.line(-0.6f, 0.65f, 0.6f, 0.65f);
      for (int i = 0; i < 3; ++i) {
        float a = PI * (0.25f + i * 0.25f);
        k.line(std::cos(a) * 0.55f, 0.3f - std::sin(a) * 0.55f,
               std::cos(a) * 0.8f, 0.3f - std::sin(a) * 0.8f);
      }
      return true;
    case Icon::Water: // three waves
      for (int row = 0; row < 3; ++row) {
        float y = -0.5f + row * 0.5f;
        k.arc(-0.4f, y, 0.4f, PI * 1.05f, PI * 1.95f);
        k.arc(0.4f, y, 0.4f, PI * 0.05f, PI * 0.95f);
      }
      return true;
    case Icon::Material: // a shaded sphere, highlight top-left
      k.circle(0.f, 0.f, 0.68f, true);
      return true;
    case Icon::Node: // a node card with a port either side
      k.rect(-0.55f, -0.45f, 0.55f, 0.45f);
      k.line(-0.55f, -0.15f, 0.55f, -0.15f);
      k.dot(-0.55f, 0.15f, 0.16f);
      k.dot(0.55f, 0.15f, 0.16f);
      return true;
    case Icon::Render: // a clapper-style frame with a play mark
      k.rect(-0.75f, -0.55f, 0.75f, 0.55f);
      k.tri(-0.15f, -0.28f, -0.15f, 0.28f, 0.32f, 0.f);
      return true;
    case Icon::Scene: // a landscape in a frame
      k.rect(-0.75f, -0.6f, 0.75f, 0.6f);
      k.poly({-0.75f, 0.35f, -0.25f, -0.2f, 0.05f, 0.15f, 0.3f, -0.05f, 0.75f, 0.35f});
      k.dot(0.35f, -0.3f, 0.12f);
      return true;
    // ---- viewport: projections ----
    case Icon::ViewPersp: // a frustum
      k.poly({-0.25f, -0.65f, 0.25f, -0.65f, 0.8f, 0.65f, -0.8f, 0.65f}, true);
      k.line(-0.5f, 0.f, 0.5f, 0.f);
      return true;
    case Icon::ViewTop: // the plane edge-on, an arrow coming down onto it
      k.line(-0.8f, 0.6f, 0.8f, 0.6f);
      k.line(0.f, -0.75f, 0.f, 0.05f);
      k.head(0.f, 0.4f, PI * 0.5f, 0.34f);
      return true;
    case Icon::ViewFront:
      k.line(0.6f, -0.8f, 0.6f, 0.8f);
      k.line(-0.75f, 0.f, 0.05f, 0.f);
      k.head(0.4f, 0.f, 0.f, 0.34f);
      return true;
    case Icon::ViewRight:
      k.line(-0.6f, -0.8f, -0.6f, 0.8f);
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
    case Icon::Grid:
      k.rect(-0.75f, -0.75f, 0.75f, 0.75f);
      k.line(-0.75f, 0.f, 0.75f, 0.f);
      k.line(0.f, -0.75f, 0.f, 0.75f);
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
