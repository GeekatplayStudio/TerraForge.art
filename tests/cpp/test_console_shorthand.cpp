// Geekatplay TerraForge — the terminal's command translation.
//
// The console command line turns `add_node type=Noise octaves=9` into the
// action document the API already speaks. The failure mode here is not a
// crash: it is a well-formed document that means something other than what
// was typed, which the application then carries out without complaint. A
// number arriving as the string "9" sets nothing; a parameter left at the top
// level instead of under "attrs" is silently ignored by add_node. Both of
// those look exactly like "the setting had no effect".
//
// Linked into undo_tests, which runs a slice of the studio with no GL context.
#include "console_shorthand.hpp"
#include <json.hpp>
#include <cstdio>
#include <string>

namespace console_shorthand_tests {

using nlohmann::json;
using namespace studio;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                   \
      ++g_fail;                                                                \
    }                                                                          \
  } while (0)

// The first (and usually only) action of a translated line.
static json one(const std::string &line) {
  std::string err;
  const std::string js = console_shorthand_to_json(line, err);
  if (js.empty()) return json();
  json doc = json::parse(js, nullptr, false);
  if (doc.is_discarded() || !doc.is_array() || doc.empty()) return json();
  return doc[0];
}

static void test_the_op_is_the_first_word() {
  std::printf("  console: the first word is the op...\n");
  CHECK(one("undo")["op"] == "undo", "undo");
  CHECK(one("  set_resolution size=1024  ")["op"] == "set_resolution",
        "leading and trailing space ignored");
  std::string err;
  CHECK(console_shorthand_to_json("", err).empty(), "an empty line runs nothing");
  CHECK(console_shorthand_to_json("   ", err).empty(), "spaces run nothing");
}

static void test_values_keep_their_type() {
  std::printf("  console: numbers arrive as numbers...\n");
  json a = one("set_attr node=Noise key=octaves value=9");
  CHECK(a["value"].is_number_integer(), "9 is an integer, not \"9\"");
  CHECK(a["value"] == 9, "and it is 9");
  json b = one("set_attr node=Noise key=gain value=0.42");
  CHECK(b["value"].is_number_float(), "0.42 is a float");
  CHECK(b["value"] > 0.41 && b["value"] < 0.43, "and it is 0.42");
  // A choice attribute takes an index: 3.0 would not be one.
  json c = one("set_attr node=Hydraulic key=method value=1");
  CHECK(c["value"].is_number_integer(), "an index stays an index");
  json d = one("set_render shadows=true");
  CHECK(d["shadows"].is_boolean() && d["shadows"] == true, "true is a bool");
  json e = one("set_render shadows=false");
  CHECK(e["shadows"].is_boolean() && e["shadows"] == false, "false is a bool");
  json f = one("view_node node=Hydraulic");
  CHECK(f["node"].is_string() && f["node"] == "Hydraulic", "a name is a string");
  // A negative number is still a number, and a version-like string is not.
  json g = one("move_node node=1 x=-40");
  CHECK(g["x"].is_number() && g["x"] == -40, "-40 is a number");
  json h = one("set_attr key=name value=1.2.3");
  CHECK(h["value"].is_string(), "1.2.3 is not a number");
}

static void test_add_node_parameters_go_under_attrs() {
  std::printf("  console: add_node parameters land in attrs...\n");
  // This is the one that would silently do nothing: add_node reads its node
  // parameters from "attrs" and ignores anything at the top level, so
  // `octaves=9` written the natural way has to be moved there.
  json a = one("add_node type=Noise octaves=9 gain=0.5");
  CHECK(a["op"] == "add_node", "op");
  CHECK(a["type"] == "Noise", "type stays at the top level");
  CHECK(a.contains("attrs"), "there is an attrs object");
  CHECK(!a.contains("octaves"), "octaves did not stay at the top level");
  CHECK(a["attrs"]["octaves"] == 9, "octaves is in attrs");
  CHECK(a["attrs"]["gain"].is_number_float(), "gain is in attrs, as a float");
  // The op's own keys must NOT be swept into attrs.
  json b = one("add_node type=Noise x=100 y=200 alias=base");
  CHECK(b["x"] == 100 && b["y"] == 200, "x and y stay at the top level");
  CHECK(b["alias"] == "base", "alias stays at the top level");
  CHECK(!b.contains("attrs"), "nothing was moved that should not be");
  // The explicit form still works, and mixes with the loose one.
  json c = one("add_node type=Noise attrs.octaves=7 gain=0.25");
  CHECK(c["attrs"]["octaves"] == 7, "attrs.octaves");
  CHECK(c["attrs"].contains("gain"), "and the loose one joined it");
  // Other ops are left alone: set_attr's key/value are its own.
  json d = one("set_attr node=Noise key=octaves value=9");
  CHECK(!d.contains("attrs"), "set_attr keeps its keys where they were");
}

static void test_quoted_values_and_paths() {
  std::printf("  console: quoted values survive whole...\n");
  json a = one("save_project path=\"C:/My Terrains/a b.gpxt\"");
  CHECK(a["path"] == "C:/My Terrains/a b.gpxt", "a path with spaces");
  // A Windows path is not valid JSON — \U and \x are not escapes — so it can
  // only arrive intact if the quotes are stripped before the value is read.
  // Leave them on and the path keeps its quotes and no file is ever found.
  json w = one("save_project path=\"C:\\Users\\me\\a b.gpxt\"");
  CHECK(w["path"].is_string(), "a Windows path is a string");
  CHECK(w["path"].get<std::string>().front() != '"', "with its quotes removed");
  CHECK(w["path"] == "C:\\Users\\me\\a b.gpxt", "and otherwise unchanged");
  std::vector<std::string> t =
      console_tokenize("open_project path=\"two words\" force=true");
  CHECK(t.size() == 3, "three tokens, not four");
  // A bracketed value is one token even with spaces inside it.
  std::vector<std::string> u = console_tokenize("set_sun dir=[0, 1, 0] up=y");
  CHECK(u.size() == 3, "the vector is one token");
  json b = one("set_sun dir=[0,1,0]");
  CHECK(b["dir"].is_array() && b["dir"].size() == 3, "a vector arrives as an array");
}

static void test_json_passes_through() {
  std::printf("  console: JSON is not re-translated...\n");
  std::string err;
  const std::string in = "{\"op\":\"undo\"}";
  CHECK(console_shorthand_to_json(in, err) == in, "an object passes through");
  const std::string arr = "[{\"op\":\"undo\"},{\"op\":\"redo\"}]";
  CHECK(console_shorthand_to_json(arr, err) == arr, "an array passes through");
  // Including one whose top-level keys would otherwise be swept into attrs.
  const std::string add = "[{\"op\":\"add_node\",\"type\":\"Noise\"}]";
  CHECK(console_shorthand_to_json(add, err) == add, "add_node JSON is untouched");
}

static void test_a_bad_line_is_refused_not_guessed() {
  std::printf("  console: a line that is not a command says so...\n");
  std::string err;
  CHECK(console_shorthand_to_json("add_node Noise", err).empty(),
        "a bare word after the op is not a command");
  CHECK(!err.empty(), "and it says why");
  CHECK(err.find("Noise") != std::string::npos, "naming the offending word");
  err.clear();
  CHECK(console_shorthand_to_json("add_node =9", err).empty(),
        "an empty key is refused");
  CHECK(!err.empty(), "and it says why");
}

static void test_the_completion_table_is_terminated() {
  std::printf("  console: the op table is NUL-terminated...\n");
  // The completion loop walks until a null; an unterminated table walks off
  // the end of it, which is a crash on Tab rather than a wrong answer.
  int n = 0;
  bool found_add_node = false, found_set_attr = false;
  for (const char *const *o = console_common_ops(); *o && n < 4096; ++o, ++n) {
    if (std::string(*o) == "add_node") found_add_node = true;
    if (std::string(*o) == "set_attr") found_set_attr = true;
  }
  CHECK(n > 10 && n < 4096, "the table ends");
  CHECK(found_add_node && found_set_attr, "and holds the ops it claims to");
}

int run() {
  std::printf("console command shorthand\n");
  g_fail = 0;
  g_checks = 0;
  test_the_op_is_the_first_word();
  test_values_keep_their_type();
  test_add_node_parameters_go_under_attrs();
  test_quoted_values_and_paths();
  test_json_passes_through();
  test_a_bad_line_is_refused_not_guessed();
  test_the_completion_table_is_terminated();
  std::printf("  %d checks, %d failures\n", g_checks, g_fail);
  return g_fail;
}

} // namespace console_shorthand_tests

int test_console_shorthand_run() { return console_shorthand_tests::run(); }
