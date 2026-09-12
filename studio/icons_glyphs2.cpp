// Geekatplay TerraForge — icon glyphs: deformers, the timeline, the object
// manager's marks, navigation, environment and the viewport helpers.
//
// Transport controls are the plain filled marks every player uses; keys are
// the Timeline's diamonds; the two visibility dots are just that — a filled
// dot and a ring — the way Cinema 4D's Object Manager draws its states.
#include "icons_pen.hpp"
#include "theme_colors.hpp"

namespace studio {

bool paint_glyphs_b(const Pen &k, Icon ic) {
  const float PI = ICON_PI;
  switch (ic) {
    // ---- deformers: a box, and what the deformer does to it ----
    case Icon::Twist: // a solid column with a spiral band
      k.hi().rect(-0.55f, -0.75f, 0.55f, 0.75f, true);
      k.lit().rect(-0.55f, -0.75f, -0.35f, 0.75f, true);
      k.dark().thick().arc(0.f, -0.45f, 0.55f, PI * 0.05f, PI * 0.95f);
      k.dark().thick().arc(0.f, 0.2f, 0.55f, PI * 1.05f, PI * 1.95f);
      k.dark().thick().arc(0.f, 0.15f, 0.55f, PI * 0.05f, PI * 0.95f);
      return true;
    case Icon::Bend: // a solid bar arched over
      k.hi().thick(2.2f).arc(0.f, 0.75f, 0.82f, PI * 1.2f, PI * 1.8f);
      k.lit().arc(0.f, 0.75f, 1.02f, PI * 1.22f, PI * 1.78f);
      k.dark().arc(0.f, 0.75f, 0.62f, PI * 1.22f, PI * 1.78f);
      return true;
    case Icon::Skew: // a solid box pushed into a parallelogram
      k.hi().fill({-0.85f, 0.7f, -0.35f, -0.7f, 0.85f, -0.7f, 0.35f, 0.7f});
      k.lit().fill({-0.35f, -0.7f, 0.85f, -0.7f, 0.79f, -0.52f, -0.41f, -0.52f});
      k.dark().line(-0.6f, 0.f, 0.6f, 0.f);
      return true;
    case Icon::Taper: // a solid box narrowing toward the top
      k.hi().fill({-0.85f, 0.7f, -0.3f, -0.7f, 0.3f, -0.7f, 0.85f, 0.7f});
      k.lit().fill({-0.3f, -0.7f, 0.3f, -0.7f, 0.37f, -0.52f, -0.37f, -0.52f});
      k.dark().line(-0.6f, 0.f, 0.6f, 0.f);
      return true;
    // ---- transport ----
    case Icon::Play:
      k.tri(-0.5f, -0.7f, -0.5f, 0.7f, 0.7f, 0.f);
      return true;
    case Icon::Pause:
      k.rect(-0.6f, -0.65f, -0.15f, 0.65f, true);
      k.rect(0.15f, -0.65f, 0.6f, 0.65f, true);
      return true;
    case Icon::Stop:
      k.rect(-0.6f, -0.6f, 0.6f, 0.6f, true);
      return true;
    case Icon::ToStart: // a bar and a triangle pointing at it
      k.rect(-0.75f, -0.65f, -0.5f, 0.65f, true);
      k.tri(0.7f, -0.65f, 0.7f, 0.65f, -0.35f, 0.f);
      return true;
    case Icon::ToEnd:
      k.rect(0.5f, -0.65f, 0.75f, 0.65f, true);
      k.tri(-0.7f, -0.65f, -0.7f, 0.65f, 0.35f, 0.f);
      return true;
    case Icon::PrevKey: // a triangle pointing at a key diamond
      k.tri(0.8f, -0.55f, 0.8f, 0.55f, -0.05f, 0.f);
      k.hi().tri(-0.45f, -0.45f, -0.85f, 0.f, -0.45f, 0.45f);
      k.hi().tri(-0.45f, -0.45f, -0.05f, 0.f, -0.45f, 0.45f);
      return true;
    case Icon::NextKey:
      k.tri(-0.8f, -0.55f, -0.8f, 0.55f, 0.05f, 0.f);
      k.hi().tri(0.45f, -0.45f, 0.85f, 0.f, 0.45f, 0.45f);
      k.hi().tri(0.45f, -0.45f, 0.05f, 0.f, 0.45f, 0.45f);
      return true;
    case Icon::KeyAdd: // a diamond with a plus beside it
      k.hi().tri(-0.3f, -0.55f, -0.85f, 0.f, -0.3f, 0.55f);
      k.hi().tri(-0.3f, -0.55f, 0.25f, 0.f, -0.3f, 0.55f);
      k.line(0.35f, -0.55f, 0.85f, -0.55f);
      k.line(0.6f, -0.8f, 0.6f, -0.3f);
      return true;
    case Icon::KeyRemove:
      k.hi().tri(-0.3f, -0.55f, -0.85f, 0.f, -0.3f, 0.55f);
      k.hi().tri(-0.3f, -0.55f, 0.25f, 0.f, -0.3f, 0.55f);
      k.line(0.35f, -0.55f, 0.85f, -0.55f);
      return true;
    case Icon::Autokey: // a key diamond inside the record ring
      k.circle(0.f, 0.f, 0.72f);
      k.hi().tri(0.f, -0.4f, -0.4f, 0.f, 0.f, 0.4f);
      k.hi().tri(0.f, -0.4f, 0.4f, 0.f, 0.f, 0.4f);
      return true;
    case Icon::Loop: // two arcs chasing each other
      k.arc(0.f, 0.f, 0.62f, PI * 1.1f, PI * 1.9f);
      k.arc(0.f, 0.f, 0.62f, PI * 0.1f, PI * 0.9f);
      k.head(0.6f, -0.15f, PI * 0.5f, 0.32f);
      k.head(-0.6f, 0.15f, -PI * 0.5f, 0.32f);
      return true;
    case Icon::Marker: // a flag on a pole
      k.line(-0.55f, -0.75f, -0.55f, 0.75f);
      k.hi().tri(-0.55f, -0.75f, 0.65f, -0.35f, -0.55f, 0.05f);
      return true;
    case Icon::Curve: // an f-curve with two key points
      k.line(-0.8f, 0.75f, -0.8f, -0.75f);
      k.line(-0.8f, 0.75f, 0.8f, 0.75f);
      k.dl->PathClear();
      k.dl->PathLineTo(k.p(-0.7f, 0.5f));
      k.dl->PathBezierCubicCurveTo(k.p(-0.1f, 0.5f), k.p(-0.05f, -0.55f),
                                   k.p(0.75f, -0.55f), 12);
      k.dl->PathStroke(k.col, 0, k.w);
      k.hi().dot(-0.7f, 0.5f, 0.15f);
      k.hi().dot(0.75f, -0.55f, 0.15f);
      return true;
    case Icon::Timeline: // a ruler with ticks and a playhead
      k.line(-0.85f, 0.2f, 0.85f, 0.2f);
      for (int i = 0; i < 5; ++i) {
        float x = -0.7f + i * 0.35f;
        k.line(x, 0.2f, x, (i & 1) ? 0.45f : 0.65f);
      }
      k.hi().line(-0.35f, -0.7f, -0.35f, 0.2f);
      k.hi().tri(-0.55f, -0.75f, -0.15f, -0.75f, -0.35f, -0.4f);
      return true;
    // ---- object manager marks ----
    case Icon::Dot:
      k.circle(0.f, 0.f, 0.5f, true);
      return true;
    case Icon::DotRing:
      k.circle(0.f, 0.f, 0.5f);
      return true;
    case Icon::Check:
      k.poly({-0.7f, 0.05f, -0.2f, 0.55f, 0.75f, -0.55f});
      return true;
    case Icon::Cross:
      k.line(-0.6f, -0.6f, 0.6f, 0.6f);
      k.line(-0.6f, 0.6f, 0.6f, -0.6f);
      return true;
    case Icon::Layer: // three stacked sheets seen at an angle
      k.poly({-0.8f, -0.2f, 0.f, -0.65f, 0.8f, -0.2f, 0.f, 0.25f}, true);
      k.poly({-0.8f, 0.2f, 0.f, 0.65f, 0.8f, 0.2f});
      return true;
    case Icon::Tag: // a label with a hole, point to the left
      k.poly({-0.8f, 0.f, -0.25f, -0.6f, 0.75f, -0.6f, 0.75f, 0.6f, -0.25f, 0.6f},
             true);
      k.dot(-0.2f, 0.f, 0.14f);
      return true;
    case Icon::Filter: // a funnel
      k.poly({-0.8f, -0.7f, 0.8f, -0.7f, 0.15f, 0.1f, 0.15f, 0.75f, -0.15f, 0.6f,
              -0.15f, 0.1f}, true);
      return true;
    // ---- navigation ----
    case Icon::Home: // a house
      k.poly({-0.85f, 0.f, 0.f, -0.75f, 0.85f, 0.f});
      k.poly({-0.6f, -0.2f, -0.6f, 0.75f, 0.6f, 0.75f, 0.6f, -0.2f});
      k.rect(-0.15f, 0.25f, 0.15f, 0.75f, true);
      return true;
    case Icon::Up: // an arrow going up
      k.line(0.f, 0.8f, 0.f, -0.3f);
      k.head(0.f, -0.8f, -PI * 0.5f, 0.5f);
      return true;
    // ---- environment ----
    case Icon::Sun: // a solid disc with heavy rays
      k.hi().circle(0.f, 0.f, 0.32f, true);
      k.lit().circle(-0.1f, -0.1f, 0.12f, true);
      for (int i = 0; i < 8; ++i) {
        float a = PI * i / 4.f;
        k.hi().thick().line(std::cos(a) * 0.5f, std::sin(a) * 0.5f, std::cos(a) * 0.82f,
                            std::sin(a) * 0.82f);
      }
      return true;
    case Icon::Atmosphere: // a solid planet with a halo
      k.hi().circle(0.f, 0.15f, 0.45f, true);
      k.lit().circle(-0.15f, 0.f, 0.16f, true);
      k.hi().thick().arc(0.f, 0.15f, 0.78f, PI * 1.02f, PI * 1.98f);
      return true;
    // ---- hierarchy and generators ----
    case Icon::Group: // a solid parent block over two children
      k.hi().rect(-0.3f, -0.8f, 0.3f, -0.25f, true);
      k.line(0.f, -0.25f, 0.f, 0.f);
      k.line(-0.5f, 0.f, 0.5f, 0.f);
      k.line(-0.5f, 0.f, -0.5f, 0.25f);
      k.line(0.5f, 0.f, 0.5f, 0.25f);
      k.hi().rect(-0.8f, 0.25f, -0.2f, 0.8f, true);
      k.hi().rect(0.2f, 0.25f, 0.8f, 0.8f, true);
      k.lit().rect(-0.3f, -0.8f, 0.3f, -0.66f, true);
      return true;
    case Icon::Null: // the three-axis marker with a solid centre
      k.hi().thick().line(-0.8f, 0.f, 0.8f, 0.f);
      k.hi().thick().line(0.f, -0.8f, 0.f, 0.8f);
      k.hi().circle(0.f, 0.f, 0.3f, true);
      k.lit().circle(-0.08f, -0.08f, 0.1f, true);
      return true;
    case Icon::Expression: // x=
      k.line(-0.8f, -0.45f, -0.15f, 0.45f);
      k.line(-0.8f, 0.45f, -0.15f, -0.45f);
      k.line(0.2f, -0.2f, 0.8f, -0.2f);
      k.line(0.2f, 0.2f, 0.8f, 0.2f);
      return true;
    case Icon::Modifier: // a solid box with a folded corner
      k.hi().rect(-0.7f, -0.7f, 0.7f, 0.7f, true);
      k.lit().rect(-0.7f, -0.7f, 0.7f, -0.52f, true);
      k.dark().tri(0.7f, -0.7f, 0.7f, 0.15f, -0.15f, -0.7f);
      return true;
    case Icon::Bake: // a flame over a tray
      k.thick().line(-0.75f, 0.75f, 0.75f, 0.75f);
      k.rect(-0.55f, 0.35f, 0.55f, 0.75f, true);
      k.dl->PathClear();
      k.dl->PathLineTo(k.p(0.f, 0.15f));
      k.dl->PathBezierQuadraticCurveTo(k.p(-0.65f, -0.2f), k.p(0.f, -0.85f), 10);
      k.dl->PathBezierQuadraticCurveTo(k.p(0.65f, -0.2f), k.p(0.f, 0.15f), 10);
      k.dl->PathFillConvex(k.hue);
      k.lit().circle(0.f, -0.15f, 0.16f, true);
      return true;
    // ---- viewport helpers ----
    case Icon::Fit: { // four corner brackets around a solid dot
      Pen t = k.thick();
      t.poly({-0.8f, -0.35f, -0.8f, -0.8f, -0.35f, -0.8f});
      t.poly({0.35f, -0.8f, 0.8f, -0.8f, 0.8f, -0.35f});
      t.poly({0.8f, 0.35f, 0.8f, 0.8f, 0.35f, 0.8f});
      t.poly({-0.35f, 0.8f, -0.8f, 0.8f, -0.8f, 0.35f});
      k.hi().dot(0.f, 0.f, 0.22f);
      return true;
    }
    case Icon::Snap: // a dot landing on a grid crossing
      k.line(-0.8f, 0.f, 0.8f, 0.f);
      k.line(0.f, -0.8f, 0.f, 0.8f);
      k.line(-0.8f, -0.55f, 0.8f, -0.55f);
      k.line(-0.55f, -0.8f, -0.55f, 0.8f);
      k.hi().dot(0.f, 0.f, 0.26f);
      return true;
    case Icon::Magnet: // a horseshoe with two poles
      k.arc(0.f, -0.1f, 0.62f, PI, PI * 2.f);
      k.arc(0.f, -0.1f, 0.22f, PI, PI * 2.f);
      k.line(-0.62f, -0.1f, -0.62f, 0.75f);
      k.line(-0.22f, -0.1f, -0.22f, 0.75f);
      k.line(0.22f, -0.1f, 0.22f, 0.75f);
      k.line(0.62f, -0.1f, 0.62f, 0.75f);
      k.hi().rect(-0.62f, 0.45f, -0.22f, 0.75f, true);
      k.hi().rect(0.22f, 0.45f, 0.62f, 0.75f, true);
      return true;
    // ---- the animate toggle: a key diamond ----
    case Icon::Key: // a solid key diamond
      k.hi().fill({0.f, -0.75f, 0.75f, 0.f, 0.f, 0.75f, -0.75f, 0.f});
      k.lit().fill({0.f, -0.75f, 0.75f, 0.f, 0.f, -0.3f});
      return true;
    // ---- sculpt brushes: the ground line, and what the brush does to it ----
    case Icon::Raise: // a mound pushed up out of the ground
      k.line(-0.85f, 0.55f, -0.45f, 0.55f);
      k.line(0.45f, 0.55f, 0.85f, 0.55f);
      k.hi().arc(0.f, 0.55f, 0.45f, PI, PI * 2.f);
      k.line(0.f, -0.75f, 0.f, -0.2f);
      k.head(0.f, -0.8f, -PI * 0.5f, 0.3f);
      return true;
    case Icon::Flatten: // a bump pressed under a flat plate
      k.hi().line(-0.8f, -0.1f, 0.8f, -0.1f);
      k.poly({-0.85f, 0.6f, -0.4f, 0.6f, -0.1f, 0.15f, 0.25f, 0.15f, 0.5f, 0.6f, 0.85f, 0.6f});
      return true;
    case Icon::Smooth: // a gentle wave
      k.hi().arc(-0.4f, 0.1f, 0.4f, PI, PI * 2.f);
      k.hi().arc(0.4f, 0.1f, 0.4f, 0.f, PI);
      k.line(-0.85f, 0.7f, 0.85f, 0.7f);
      return true;
    case Icon::Terrace: // steps
      k.hi().poly({-0.85f, 0.7f, -0.85f, 0.25f, -0.3f, 0.25f, -0.3f, -0.2f, 0.25f, -0.2f,
                   0.25f, -0.65f, 0.85f, -0.65f});
      k.line(-0.85f, 0.7f, 0.85f, 0.7f);
      return true;
    case Icon::Noise: // a jagged ridge line
      k.hi().poly({-0.85f, 0.4f, -0.6f, -0.1f, -0.4f, 0.2f, -0.15f, -0.6f, 0.1f, 0.f,
                   0.35f, -0.35f, 0.6f, 0.15f, 0.85f, -0.2f});
      k.line(-0.85f, 0.7f, 0.85f, 0.7f);
      return true;
    case Icon::Erase: // a tilted eraser block over the ground
      k.line(-0.85f, 0.7f, 0.85f, 0.7f);
      k.hi().poly({-0.55f, 0.35f, 0.25f, -0.45f, 0.7f, 0.f, -0.1f, 0.8f}, true);
      k.line(-0.15f, -0.05f, 0.3f, 0.4f);
      return true;
    // ---- the console: a prompt in a frame ----
    case Icon::Console:
      k.rect(-0.8f, -0.65f, 0.8f, 0.65f);
      k.hi().poly({-0.55f, -0.3f, -0.2f, 0.f, -0.55f, 0.3f});
      k.line(0.f, 0.3f, 0.5f, 0.3f);
      return true;
    // ---- views: four panes ----
    case Icon::Views:
      k.rect(-0.8f, -0.8f, 0.8f, 0.8f);
      k.line(-0.8f, 0.f, 0.8f, 0.f);
      k.line(0.f, -0.8f, 0.f, 0.8f);
      k.hi().rect(-0.65f, -0.65f, -0.15f, -0.15f, true);
      return true;
    case Icon::Transform: // a heavy ring with the four-way arrows through it
      k.hi().thick().circle(0.f, 0.f, 0.52f);
      k.thick().line(-0.55f, 0.f, 0.55f, 0.f);
      k.thick().line(0.f, -0.55f, 0.f, 0.55f);
      k.head(0.92f, 0.f, 0.f, 0.36f);
      k.head(-0.92f, 0.f, PI, 0.36f);
      k.head(0.f, -0.92f, -PI * 0.5f, 0.36f);
      k.head(0.f, 0.92f, PI * 0.5f, 0.36f);
      return true;
    case Icon::Plant: // a conifer: a trunk under three tiers, lit down one side
      k.dark().rect(-0.1f, 0.5f, 0.1f, 0.85f, true);
      k.hi().tri(0.f, 0.05f, -0.72f, 0.62f, 0.72f, 0.62f);
      k.hi().tri(0.f, -0.35f, -0.56f, 0.2f, 0.56f, 0.2f);
      k.hi().tri(0.f, -0.8f, -0.4f, -0.2f, 0.4f, -0.2f);
      k.lit().tri(0.f, -0.8f, -0.4f, -0.2f, 0.f, -0.2f);
      k.lit().tri(0.f, -0.35f, -0.56f, 0.2f, 0.f, 0.2f);
      k.lit().tri(0.f, 0.05f, -0.72f, 0.62f, 0.f, 0.62f);
      return true;
    default:
      return false;
  }
}

} // namespace studio
