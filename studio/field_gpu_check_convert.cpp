// Geekatplay TerraForge — CPU/GPU agreement cases for the converter, colour
// and animation field nodes. Split from field_gpu_check.cpp for the 500-line
// module rule; field_gpu_verify_all() calls this after its own cases.
//
// Every case wires a converter between real sources and reads a specific
// output port, because these nodes have several outputs and a secondary
// output with a wrong or missing emitter is exactly the divergence the check
// exists to catch.
#include "app.hpp"
#include "gpx/node_graph.hpp"
#include <string>

namespace studio {

struct FieldGpuResult {
  bool ok = false;
  float max_abs_error = 0.f;
  float mean_abs_error = 0.f;
  int samples = 0;
  std::string message;
};
FieldGpuResult field_gpu_verify(const gpx::Node &node, const std::string &port);

void field_gpu_verify_converters(std::string &out) {
  auto run = [&](const char *name, gpx::Node *tip, const char *port) {
    FieldGpuResult r = field_gpu_verify(*tip, port);
    out += std::string(name) + ": " + r.message + "\n";
  };
  {   // colour apart and back together, with the alpha lane in play
    gpx::Graph g;
    gpx::Node *c = g.add_node("FieldColorConstant");
    c->attrs.find("color")->col[3] = 0.35f;
    gpx::Node *sp = g.add_node("FieldColorSplit");
    gpx::Node *cb = g.add_node("FieldColorCombine");
    gpx::Node *nz = g.add_node("FieldNoise");
    g.add_link(c->id, "out", sp->id, "color");
    g.add_link(sp->id, "b", cb->id, "r");
    g.add_link(sp->id, "a", cb->id, "g");
    g.add_link(nz->id, "out", cb->id, "b");
    g.add_link(sp->id, "luminance", cb->id, "a");
    run("colour split>combine", cb, "out");
  }
  {   // vector lanes of the position through the vector op's vec output
    gpx::Graph g;
    gpx::Node *p = g.add_node("FieldPosition");
    gpx::Node *op = g.add_node("FieldVectorOp");
    op->attrs.find("op")->i = 8; // reflect off the default (0,1,0)
    gpx::Node *sp = g.add_node("FieldVectorSplit");
    gpx::Node *cb = g.add_node("FieldVectorCombine");
    g.add_link(p->id, "out", op->id, "a");
    g.add_link(op->id, "vec", sp->id, "vector");
    g.add_link(sp->id, "z", cb->id, "x");
    g.add_link(sp->id, "length", cb->id, "y");
    g.add_link(sp->id, "y", cb->id, "z");
    run("vector reflect>split>combine", cb, "out");
    run("vector split.length", sp, "length");
  }
  {   // texture coordinates split and re-projected
    gpx::Graph g;
    gpx::Node *tc = g.add_node("FieldTexCoord");
    tc->attrs.find("angle")->f = 33.f;
    gpx::Node *sp = g.add_node("FieldTexCoordSplit");
    gpx::Node *tv = g.add_node("FieldToVector");
    tv->attrs.find("plane")->i = 2;
    gpx::Node *tt = g.add_node("FieldToTexCoord");
    tt->attrs.find("plane")->i = 1;
    g.add_link(tc->id, "out", sp->id, "uv");
    g.add_link(tc->id, "out", tv->id, "in");
    g.add_link(tv->id, "out", tt->id, "in");
    run("texcoord split.v", sp, "v");
    run("texcoord>vector(ZY)>texcoord(XY)", tt, "out");
  }
  {   // the lane-picking adapter on every type it can see
    gpx::Graph g;
    gpx::Node *c = g.add_node("FieldColorConstant");
    gpx::Node *p = g.add_node("FieldPosition");
    gpx::Node *tc = g.add_node("FieldTexCoord");
    gpx::Node *n1 = g.add_node("FieldToNumber");
    gpx::Node *n2 = g.add_node("FieldToNumber");
    gpx::Node *n3 = g.add_node("FieldToNumber");
    n1->attrs.find("mode")->i = 5; // largest lane of a colour
    n2->attrs.find("mode")->i = 6; // average of a position
    n3->attrs.find("mode")->i = 2; // v of texcoords
    gpx::Node *m = g.add_node("FieldMath");
    gpx::Node *m2 = g.add_node("FieldMath");
    g.add_link(c->id, "out", n1->id, "in");
    g.add_link(p->id, "out", n2->id, "in");
    g.add_link(tc->id, "out", n3->id, "in");
    g.add_link(n1->id, "out", m->id, "a");
    g.add_link(n2->id, "out", m->id, "b");
    g.add_link(m->id, "out", m2->id, "a");
    g.add_link(n3->id, "out", m2->id, "b");
    run("to-number lanes (max, avg, v)", m2, "out");
  }
  {   // a normal as a colour, then graded through HSV and back
    gpx::Graph g;
    gpx::Node *nm = g.add_node("FieldNormal");
    gpx::Node *tc = g.add_node("FieldToColor");
    gpx::Node *hsv = g.add_node("FieldColorHSV");
    gpx::Node *fh = g.add_node("FieldColorFromHSV");
    gpx::Node *adj = g.add_node("FieldColorAdjust");
    adj->attrs.find("hue")->f = 40.f;
    adj->attrs.find("saturation")->f = 1.4f;
    adj->attrs.find("contrast")->f = 1.2f;
    adj->attrs.find("gamma")->f = 1.8f;
    g.add_link(nm->id, "out", tc->id, "in");
    g.add_link(tc->id, "out", hsv->id, "color");
    g.add_link(hsv->id, "h", fh->id, "h");
    g.add_link(hsv->id, "s", fh->id, "s");
    g.add_link(hsv->id, "v", fh->id, "v");
    g.add_link(fh->id, "out", adj->id, "color");
    run("normal>colour>hsv>rgb>adjust", adj, "out");
  }
  {   // time shaping
    gpx::Graph g;
    gpx::Node *tr = g.add_node("TimeRemap");
    tr->attrs.find("loop")->f = 0.7f;
    tr->attrs.find("pingpong")->b = true;
    gpx::Node *os = g.add_node("Oscillator");
    os->attrs.find("shape")->i = 1;
    g.add_link(tr->id, "out", os->id, "time");
    run("time remap>oscillator(triangle)", os, "out");
  }
}

} // namespace studio
