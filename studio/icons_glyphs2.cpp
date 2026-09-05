// Geekatplay TerraForge — icon glyphs: deformers, the timeline, the object
// manager's marks, navigation, environment and the viewport helpers.
//
// Transport controls are the plain filled marks every player uses; keys are
// the Timeline's diamonds; the two visibility dots are just that — a filled
// dot and a ring — the way Cinema 4D's Object Manager draws its states.
#include "icons_pen.hpp"

namespace studio {

bool paint_glyphs_b(const Pen &k, Icon ic) {
  const float PI = ICON_PI;
  switch (ic) {
    // ---- deformers: a box, and what the deformer does to it ----
    case Icon::Twist: // a column with a spiral band
      k.line(-0.55f, -0.75f, -0.55f, 0.75f);
      k.line(0.55f, -0.75f, 0.55f, 0.75f);
      k.arc(0.f, -0.45f, 0.55f, PI * 0.05f, PI * 0.95f);
      k.arc(0.f, 0.2f, 0.55f, PI * 1.05f, PI * 1.95f);
      k.arc(0.f, 0.15f, 0.55f, PI * 0.05f, PI * 0.95f);
      return true;
    case Icon::Bend: // a bar arched over
      k.arc(0.f, 0.75f, 1.05f, PI * 1.2f, PI * 1.8f);
      k.arc(0.f, 0.75f, 0.6f, PI * 1.2f, PI * 1.8f);
      k.line(-0.85f, 0.15f, -0.5f, 0.4f);
      k.line(0.85f, 0.15f, 0.5f, 0.4f);
      return true;
    case Icon::Skew: // a box pushed into a parallelogram
      k.poly({-0.8f, 0.7f, -0.35f, -0.7f, 0.8f, -0.7f, 0.35f, 0.7f}, true);
      k.line(-0.55f, 0.f, 0.55f, 0.f);
      return true;
    case Icon::Taper: // a box narrowing toward the top
      k.poly({-0.75f, 0.7f, -0.3f, -0.7f, 0.3f, -0.7f, 0.75f, 0.7f}, true);
      k.line(-0.55f, 0.f, 0.55f, 0.f);
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
      k.tri(-0.45f, -0.45f, -0.85f, 0.f, -0.45f, 0.45f);
      k.tri(-0.45f, -0.45f, -0.05f, 0.f, -0.45f, 0.45f);
      return true;
    case Icon::NextKey:
      k.tri(-0.8f, -0.55f, -0.8f, 0.55f, 0.05f, 0.f);
      k.tri(0.45f, -0.45f, 0.85f, 0.f, 0.45f, 0.45f);
      k.tri(0.45f, -0.45f, 0.05f, 0.f, 0.45f, 0.45f);
      return true;
    case Icon::KeyAdd: // a diamond with a plus beside it
      k.tri(-0.3f, -0.55f, -0.85f, 0.f, -0.3f, 0.55f);
      k.tri(-0.3f, -0.55f, 0.25f, 0.f, -0.3f, 0.55f);
      k.line(0.35f, -0.55f, 0.85f, -0.55f);
      k.line(0.6f, -0.8f, 0.6f, -0.3f);
      return true;
    case Icon::KeyRemove:
      k.tri(-0.3f, -0.55f, -0.85f, 0.f, -0.3f, 0.55f);
      k.tri(-0.3f, -0.55f, 0.25f, 0.f, -0.3f, 0.55f);
      k.line(0.35f, -0.55f, 0.85f, -0.55f);
      return true;
    case Icon::Autokey: // a key diamond inside the record ring
      k.circle(0.f, 0.f, 0.72f);
      k.tri(0.f, -0.4f, -0.4f, 0.f, 0.f, 0.4f);
      k.tri(0.f, -0.4f, 0.4f, 0.f, 0.f, 0.4f);
      return true;
    case Icon::Loop: // two arcs chasing each other
      k.arc(0.f, 0.f, 0.62f, PI * 1.1f, PI * 1.9f);
      k.arc(0.f, 0.f, 0.62f, PI * 0.1f, PI * 0.9f);
      k.head(0.6f, -0.15f, PI * 0.5f, 0.32f);
      k.head(-0.6f, 0.15f, -PI * 0.5f, 0.32f);
      return true;
    case Icon::Marker: // a flag on a pole
      k.line(-0.55f, -0.75f, -0.55f, 0.75f);
      k.tri(-0.55f, -0.75f, 0.65f, -0.35f, -0.55f, 0.05f);
      return true;
    case Icon::Curve: // an f-curve with two key points
      k.line(-0.8f, 0.75f, -0.8f, -0.75f);
      k.line(-0.8f, 0.75f, 0.8f, 0.75f);
      k.dl->PathClear();
      k.dl->PathLineTo(k.p(-0.7f, 0.5f));
      k.dl->PathBezierCubicCurveTo(k.p(-0.1f, 0.5f), k.p(-0.05f, -0.55f),
                                   k.p(0.75f, -0.55f), 12);
      k.dl->PathStroke(k.col, 0, k.w);
      k.dot(-0.7f, 0.5f, 0.15f);
      k.dot(0.75f, -0.55f, 0.15f);
      return true;
    case Icon::Timeline: // a ruler with ticks and a playhead
      k.line(-0.85f, 0.2f, 0.85f, 0.2f);
      for (int i = 0; i < 5; ++i) {
        float x = -0.7f + i * 0.35f;
        k.line(x, 0.2f, x, (i & 1) ? 0.45f : 0.65f);
      }
      k.line(-0.35f, -0.7f, -0.35f, 0.2f);
      k.tri(-0.55f, -0.75f, -0.15f, -0.75f, -0.35f, -0.4f);
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
    case Icon::Sun: // a disc with eight short rays
      k.circle(0.f, 0.f, 0.3f, true);
      for (int i = 0; i < 8; ++i) {
        float a = PI * i / 4.f;
        k.line(std::cos(a) * 0.5f, std::sin(a) * 0.5f, std::cos(a) * 0.8f,
               std::sin(a) * 0.8f);
      }
      return true;
    case Icon::Atmosphere: // a planet with a halo
      k.circle(0.f, 0.15f, 0.42f, true);
      k.arc(0.f, 0.15f, 0.75f, PI * 1.02f, PI * 1.98f);
      return true;
    // ---- hierarchy and generators ----
    case Icon::Group: // a parent square with two children beneath
      k.rect(-0.3f, -0.8f, 0.3f, -0.25f);
      k.line(0.f, -0.25f, 0.f, 0.f);
      k.line(-0.5f, 0.f, 0.5f, 0.f);
      k.line(-0.5f, 0.f, -0.5f, 0.25f);
      k.line(0.5f, 0.f, 0.5f, 0.25f);
      k.rect(-0.8f, 0.25f, -0.2f, 0.8f, true);
      k.rect(0.2f, 0.25f, 0.8f, 0.8f, true);
      return true;
    case Icon::Null: // the three-axis origin marker
      k.line(-0.8f, 0.f, 0.8f, 0.f);
      k.line(0.f, -0.8f, 0.f, 0.8f);
      k.circle(0.f, 0.f, 0.35f);
      return true;
    case Icon::Expression: // x=
      k.line(-0.8f, -0.45f, -0.15f, 0.45f);
      k.line(-0.8f, 0.45f, -0.15f, -0.45f);
      k.line(0.2f, -0.2f, 0.8f, -0.2f);
      k.line(0.2f, 0.2f, 0.8f, 0.2f);
      return true;
    case Icon::Modifier: // a box with a filled corner marker
      k.rect(-0.7f, -0.7f, 0.7f, 0.7f);
      k.tri(0.7f, -0.7f, 0.7f, 0.1f, -0.1f, -0.7f);
      return true;
    case Icon::Bake: // a flame over a tray
      k.line(-0.75f, 0.75f, 0.75f, 0.75f);
      k.rect(-0.55f, 0.35f, 0.55f, 0.75f);
      k.dl->PathClear();
      k.dl->PathLineTo(k.p(0.f, 0.05f));
      k.dl->PathBezierQuadraticCurveTo(k.p(-0.6f, -0.2f), k.p(0.f, -0.8f), 10);
      k.dl->PathBezierQuadraticCurveTo(k.p(0.6f, -0.2f), k.p(0.f, 0.05f), 10);
      k.dl->PathFillConvex(k.col);
      return true;
    // ---- viewport helpers ----
    case Icon::Fit: // four corner brackets around a dot
      k.poly({-0.8f, -0.35f, -0.8f, -0.8f, -0.35f, -0.8f});
      k.poly({0.35f, -0.8f, 0.8f, -0.8f, 0.8f, -0.35f});
      k.poly({0.8f, 0.35f, 0.8f, 0.8f, 0.35f, 0.8f});
      k.poly({-0.35f, 0.8f, -0.8f, 0.8f, -0.8f, 0.35f});
      k.dot(0.f, 0.f, 0.16f);
      return true;
    case Icon::Snap: // a dot landing on a grid crossing
      k.line(-0.8f, 0.f, 0.8f, 0.f);
      k.line(0.f, -0.8f, 0.f, 0.8f);
      k.line(-0.8f, -0.55f, 0.8f, -0.55f);
      k.line(-0.55f, -0.8f, -0.55f, 0.8f);
      k.dot(0.f, 0.f, 0.26f);
      return true;
    case Icon::Magnet: // a horseshoe with two poles
      k.arc(0.f, -0.1f, 0.62f, PI, PI * 2.f);
      k.arc(0.f, -0.1f, 0.22f, PI, PI * 2.f);
      k.line(-0.62f, -0.1f, -0.62f, 0.75f);
      k.line(-0.22f, -0.1f, -0.22f, 0.75f);
      k.line(0.22f, -0.1f, 0.22f, 0.75f);
      k.line(0.62f, -0.1f, 0.62f, 0.75f);
      k.rect(-0.62f, 0.45f, -0.22f, 0.75f, true);
      k.rect(0.22f, 0.45f, 0.62f, 0.75f, true);
      return true;
    default:
      return false;
  }
}

} // namespace studio
