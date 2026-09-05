// Geekatplay TerraForge - the configuration manager and the shortcut table,
// without a window: JSON round trips with secrets protected, provider
// defaults filled in, chords parsed and formatted as exact inverses, the
// table free of duplicate ids and default chords, conflicts detected.
//
// The mutation half: a deliberately corrupted file, a chord that cannot be
// read, a provider nobody knows - each must fail the way the code says it
// fails, not crash or half-load.
#include "config.hpp"
#include "shortcuts.hpp"
#include <cstdio>
#include <imgui.h>
#include <set>
#include <string>

using namespace studio;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static void test_secrets() {
  std::printf("secrets...\n");
  std::string k = "sk-live-0123456789abcdef";
  std::string p = secret_protect(k);
  check(p != k, "a protected secret is not the secret");
  check(p.rfind("dpapi:", 0) == 0 || p.rfind("plain:", 0) == 0, "the protection says what it is");
  check(secret_unprotect(p) == k, "and it round-trips");
  check(secret_protect("").empty() && secret_unprotect("").empty(), "empty stays empty");
  check(secret_unprotect("raw-typed-key") == "raw-typed-key", "a key typed straight into the file still reads");
}

static void test_config_json() {
  std::printf("config json...\n");
  Config c;
  c.services["openai"] = {"sk-test", "", "gpt-4o-mini", true};
  c.services["meshy"] = {"m-key", "https://example.test/meshy", "", false};
  c.comfy.url = "http://10.0.0.5:8188";
  c.comfy.cloud_key = "cloud-secret";
  c.comfy.workflow_dirs = {"D:/wf", "E:/more"};
  c.ai.image_provider = "stability";
  c.apps["blender"] = "C:/Blender/blender.exe";
  c.shortcuts["file.save"] = "Ctrl+Shift+S";
  c.perf.governor = false;
  c.perf.fps_primary = 45;
  std::string js = config_to_json(c, true);
  check(js.find("sk-test") == std::string::npos, "the key is not in the file in clear");
  check(js.find("cloud-secret") == std::string::npos, "nor the cloud key");
  Config back;
  std::string err;
  check(config_from_json(back, js, err), err.c_str());
  check(back.services["openai"].key == "sk-test", "the key comes back");
  check(back.services["meshy"].endpoint == "https://example.test/meshy" && !back.services["meshy"].enabled,
        "endpoint override and enabled flag come back");
  check(back.comfy.url == "http://10.0.0.5:8188" && back.comfy.cloud_key == "cloud-secret", "comfy settings come back");
  check(back.comfy.workflow_dirs.size() == 2 && back.comfy.workflow_dirs[1] == "E:/more", "workflow folders come back");
  check(back.ai.image_provider == "stability", "AI defaults come back");
  check(back.apps["blender"] == "C:/Blender/blender.exe", "application paths come back");
  check(back.shortcuts["file.save"] == "Ctrl+Shift+S", "shortcuts come back");
  check(!back.perf.governor && back.perf.fps_primary == 45 && back.perf.fps_secondary == 20, "governor settings come back, with the default where unset");
  // mutations: garbage, and a file with nothing in it
  Config junk;
  check(!config_from_json(junk, "{not json", err) && !err.empty(), "garbage fails with a reason");
  check(config_from_json(junk, "{}", err), "an empty object loads");
  check(junk.comfy.url == "http://127.0.0.1:8188" && junk.ai.text_provider == "ollama", "with every default");
}

static void test_providers() {
  std::printf("providers...\n");
  std::set<std::string> ids;
  for (const ProviderInfo &p : known_providers()) {
    check(ids.insert(p.id).second, "provider ids are unique");
    check(p.default_endpoint[0] != 0, "every provider has a default endpoint");
  }
  check(provider_info("meshy") && std::string(provider_info("meshy")->purpose) == "3d", "meshy is a 3D provider");
  check(provider_info("nobody") == nullptr, "an unknown provider is null, not a crash");
  config().services.clear();
  check(service_endpoint("openai") == "https://api.openai.com/v1", "the default endpoint fills in");
  check(service_model("ollama") == "llama3.1", "and the default model");
  check(service_ready("ollama") && service_ready("comfyui"), "local services need no key");
  check(!service_ready("openai"), "a cloud service without a key is not ready");
  config().services["openai"].key = "sk";
  check(service_ready("openai") && service_ready("openai_image"), "one OpenAI key serves images too");
  config().services["openai"].enabled = false;
  check(!service_ready("openai"), "disabled is not ready even with a key");
  config().services.clear();
}

static void test_chords() {
  std::printf("chords...\n");
  const char *samples[] = {"Ctrl+S", "Ctrl+Shift+S", "F5", "W", "Shift+Alt+F12", "Escape", "Ctrl+Comma"};
  for (const char *s : samples) {
    KeyChord c = chord_parse(s);
    check(c.valid(), (std::string("parses ") + s).c_str());
    check(chord_format(c) == s, (std::string("formats back to ") + s).c_str());
  }
  check(chord_format(chord_parse("ctrl + shift + s")) == "Ctrl+Shift+S", "loose spelling normalises");
  check(!chord_parse("Ctrl+").valid(), "a modifier alone is not a chord");
  check(!chord_parse("Hyper+Q").valid() || chord_parse("Hyper+Q").key == ImGuiKey_Q, "an unknown modifier is ignored");
  check(!chord_parse("Ctrl+Bananas").valid(), "an unknown key is invalid");
  check(!chord_parse("").valid(), "empty is invalid");
}

static void test_table() {
  std::printf("shortcut table...\n");
  std::set<std::string> ids, chords;
  for (const ShortcutCommand &c : shortcut_commands()) {
    check(ids.insert(c.id).second, (std::string("unique id ") + c.id).c_str());
    KeyChord k = chord_parse(c.chord);
    check(k.valid(), (std::string("default chord parses: ") + c.id).c_str());
    check(chords.insert(chord_format(k)).second, (std::string("no two commands share a default: ") + c.chord).c_str());
  }
  check(shortcut_chord("file.save") == "Ctrl+S", "the default is what the table says");
  check(shortcut_is_default("file.save"), "and reads as default");
  shortcut_set("file.save", "ctrl+alt+s");
  check(shortcut_chord("file.save") == "Ctrl+Alt+S", "a rebinding takes, normalised");
  check(!shortcut_is_default("file.save"), "and is no longer the default");
  std::vector<std::string> cl = shortcut_conflicts(chord_parse("Ctrl+Alt+S"), "edit.undo");
  check(cl.size() == 1 && cl[0] == "file.save", "the conflict finder sees it");
  check(shortcut_conflicts(chord_parse("Ctrl+Alt+S"), "file.save").empty(), "but not against itself");
  shortcut_set("file.save", "");
  check(shortcut_is_default("file.save") && shortcut_chord("file.save") == "Ctrl+S", "empty restores the default");
  shortcut_set("no.such.command", "F1");
  check(config().shortcuts.count("no.such.command") == 0, "an unknown command is not stored");
}

int main() {
  // an ImGui context so key names and IO exist without a window
  ImGui::CreateContext();
  test_secrets();
  test_config_json();
  test_providers();
  test_chords();
  test_table();
  ImGui::DestroyContext();
  if (failures) {
    std::printf("%d config check(s) failed\n", failures);
    return 1;
  }
  std::printf("config tests passed\n");
  return 0;
}
