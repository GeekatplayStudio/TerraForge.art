// Geekatplay TerraForge - the AI services without a network: every request
// builder and response parser on the exact JSON the providers document
// (docs_private/IMAGEEXPRESS_PORT.md), the ComfyUI injection and binding
// inference on a real workflow, the prompt templates, the poll backoff.
//
// Mutations, deliberately: error payloads, empty replies, a missing node id,
// a truncated JSON - each must fail with a message, never crash or pass.
#include "ai_describe.hpp"
#include "ai_services.hpp"
#include "config.hpp"
#include "http_client.hpp"
#include <cstdio>
#include <string>

using namespace studio;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
static bool has(const std::string &s, const char *sub) { return s.find(sub) != std::string::npos; }

static void test_http_helpers() {
  std::printf("http helpers...\n");
  UrlParts u;
  check(url_split("https://api.openai.com/v1/images?x=1", u) && u.tls && u.host == "api.openai.com" &&
            u.port == 443 && u.path == "/v1/images?x=1",
        "https url splits");
  check(url_split("http://127.0.0.1:8188/prompt", u) && !u.tls && u.port == 8188 && u.path == "/prompt",
        "http url with port splits");
  check(!url_split("nonsense", u), "a non-url is refused");
  const std::string bin("Terra\0Forge", 11);
  check(base64_decode(base64_encode(bin)) == bin, "base64 round-trips binary");
  check(base64_decode("VGVy\nUmE=") == "TerRa", "base64 tolerates wrapped lines");
  std::string ct;
  std::string body = http_multipart({{"prompt", "a rock", "", ""}, {"image", "PNGBYTES", "in.png", "image/png"}}, ct);
  check(has(ct, "multipart/form-data; boundary=") && has(body, "name=\"prompt\"") &&
            has(body, "filename=\"in.png\"") && has(body, "Content-Type: image/png") && has(body, "PNGBYTES"),
        "multipart carries fields and files");
}

static void test_text_formats() {
  std::printf("text formats...\n");
  std::string j = openai_chat_json("gpt-4o-mini", "SYS", "hello", "");
  check(has(j, "\"model\":\"gpt-4o-mini\"") && has(j, "\"role\":\"system\"") && has(j, "SYS") && has(j, "hello"),
        "OpenAI chat body");
  check(!has(j, "image_url"), "no image part without an image");
  check(has(openai_chat_json("m", "s", "p", "AAAA"), "data:image/png;base64,AAAA"), "an image becomes an image_url part");
  std::string text, err;
  check(parse_openai_chat(R"({"choices":[{"message":{"role":"assistant","content":"[{\"op\":\"x\"}]"}}]})", text, err) &&
            text == "[{\"op\":\"x\"}]",
        "OpenAI reply parses");
  check(!parse_openai_chat(R"({"error":{"message":"invalid api key"}})", text, err) && err == "invalid api key",
        "OpenAI error surfaces its message");
  check(!parse_openai_chat("{\"choices\":[]}", text, err), "no choices fails");
  check(!parse_openai_chat("{\"choices\":[{", text, err) && err == "not JSON", "truncated JSON fails");

  j = anthropic_messages_json("claude", "SYS", "hi", "");
  check(has(j, "\"system\":\"SYS\"") && has(j, "max_tokens"), "Anthropic body carries system and max_tokens");
  check(parse_anthropic_message(R"({"content":[{"type":"text","text":"A"},{"type":"text","text":"B"}]})", text, err) && text == "AB",
        "Anthropic text parts concatenate");
  check(!parse_anthropic_message(R"({"error":{"type":"x","message":"over limit"}})", text, err) && err == "over limit",
        "Anthropic error surfaces");

  j = gemini_generate_json("p", "", false, "");
  check(has(j, "\"contents\"") && has(j, "temperature") && !has(j, "IMAGE"), "Gemini text body");
  j = gemini_generate_json("p", "", true, "16:9");
  check(has(j, "\"responseModalities\":[\"IMAGE\"]") && has(j, "\"aspectRatio\":\"16:9\""), "Gemini image body");
  check(parse_gemini_text(R"({"candidates":[{"content":{"parts":[{"text":"ok"}]}}]})", text, err) && text == "ok",
        "Gemini text parses");
  check(!parse_gemini_text(R"({"candidates":[]})", text, err), "no candidates fails");
}

static void test_image_formats() {
  std::printf("image formats...\n");
  check(openai_size_for(1792, 1024, "dall-e-3") == "1792x1024" && openai_size_for(1024, 1792, "dall-e-3") == "1024x1792" &&
            openai_size_for(1000, 1000, "dall-e-3") == "1024x1024",
        "DALL-E sizes by aspect");
  check(openai_size_for(2048, 1024, "gpt-image-1") == "1536x1024", "gpt-image sizes by aspect");
  std::string j = openai_image_json("dall-e-3", "p", 1024, 1024);
  check(has(j, "b64_json"), "dall-e asks for base64");
  check(!has(openai_image_json("gpt-image-1", "p", 1024, 1024), "response_format"), "gpt-image is not asked (it always returns base64)");
  std::string bytes, err;
  check(parse_openai_image(R"({"data":[{"b64_json":"UE5H"}]})", bytes, err) && bytes == "PNG", "OpenAI base64 image decodes");
  check(parse_openai_image(R"({"data":[{"url":"https://x/y.png"}]})", bytes, err) && bytes == "url:https://x/y.png", "an url reply is passed through");
  check(!parse_openai_image(R"({"error":{"message":"safety"}})", bytes, err) && err == "safety", "OpenAI image error");
  check(!parse_openai_image(R"({"data":[]})", bytes, err), "no data fails");

  int w = 1100, h = 800;
  stability_snap_size(w, h);
  check(w == 1152 && h == 896, "Stability snaps to the nearest legal SDXL size");
  w = 500; h = 1400;
  stability_snap_size(w, h);
  check(w == 640 && h == 1536, "and the other way");
  check(stability_aspect(2048, 1024) == "2:1" || stability_aspect(2048, 1024) == "21:9" || stability_aspect(2048, 1024) == "16:9",
        "Stability aspect for a wide sky is a wide ratio");
  check(stability_aspect(1024, 1024) == "1:1", "square is 1:1");
  unsigned seed = 0;
  check(parse_stability_image(R"({"image":"UE5H","seed":42,"finish_reason":"SUCCESS"})", bytes, seed, err) && bytes == "PNG" && seed == 42,
        "Stability image and seed parse");
  check(!parse_stability_image(R"({"errors":["invalid_prompts"],"name":"bad_request"})", bytes, seed, err) && err == "invalid_prompts",
        "Stability error list surfaces");
  check(!parse_stability_image(R"({"image":"UE5H","finish_reason":"CONTENT_FILTERED"})", bytes, seed, err), "a filtered result is a failure");

  check(gemini_aspect(1024, 1024) == "1:1" && gemini_aspect(1920, 1080) == "16:9" && gemini_aspect(2400, 1000) == "21:9",
        "Gemini aspect picks the nearest");
  std::string mime;
  check(parse_gemini_image(R"({"candidates":[{"content":{"parts":[{"inlineData":{"mimeType":"image/png","data":"UE5H"}}]}}]})", bytes, mime, err) &&
            bytes == "PNG" && mime == "image/png",
        "Gemini inline image (camelCase)");
  check(parse_gemini_image(R"({"candidates":[{"content":{"parts":[{"inline_data":{"mime_type":"image/jpeg","data":"UE5H"}}]}}]})", bytes, mime, err) &&
            mime == "image/jpeg",
        "Gemini inline image (snake_case)");
  check(!parse_gemini_image(R"({"candidates":[{"content":{"parts":[{"text":"I cannot draw that"}]}}]})", bytes, mime, err) &&
            has(err, "I cannot draw that"),
        "a text-only reply reports what the model said");
}

static void test_prompts() {
  std::printf("prompts...\n");
  std::string t = ai_prompt_for(IMG_TEXTURE, "wet cobblestones");
  check(has(t, "tileable") && has(t, "wet cobblestones") && has(t, "no shadows"), "a texture prompt asks for tiling and flat light");
  std::string s = ai_prompt_for(IMG_SKYDOME, "sunset over the sea");
  check(has(s, "equirectangular") && has(s, "2:1") && has(s, "sunset over the sea"), "a skydome prompt asks for an equirectangular 2:1");
  check(ai_prompt_for(IMG_PLAIN, "x") == "x", "a plain image prompt is untouched");
  check(has(ai_negative_for(IMG_TEXTURE, "moss"), "seams") && has(ai_negative_for(IMG_TEXTURE, "moss"), "moss"), "negatives merge");
  int w = 1024, h = 1024;
  ai_size_for(IMG_SKYDOME, w, h);
  check(w == 2048 && h == 1024, "a skydome is 2:1 and at least 2048 wide");
  w = 768; h = 100;
  ai_size_for(IMG_TEXTURE, w, h);
  check(w == 768 && h == 768, "a texture is square");
  check(poll_next_delay_ms(0, false) == 2000 && poll_next_delay_ms(2000, false) == 3000 &&
            poll_next_delay_ms(14000, false) == 15000 && poll_next_delay_ms(15000, true) == 2000,
        "poll backoff: 2 s, x1.5, capped at 15 s, reset on progress");
}

// the planner's reply, in the shapes a model actually sends
static void test_describe_reply() {
  std::printf("describe reply...\n");
  check(ai_describe_extract_actions(R"({"actions":[{"op":"set_sun","altitude_deg":10}]})") ==
            R"({"actions":[{"op":"set_sun","altitude_deg":10}]})",
        "an object reply passes through");
  std::string fenced = "Here you go:\n```json\n[{\"op\":\"set_sun\"},{\"op\":\"set_fog\"}]\n```";
  check(ai_describe_extract_actions(fenced) == R"({"actions":[{"op":"set_sun"},{"op":"set_fog"}]})",
        "a bare array in a fence is wrapped and the fence dropped");
  check(ai_describe_extract_actions("Sure! {\"actions\": [{\"op\":\"x\"}]} Done.") == R"({"actions": [{"op":"x"}]})",
        "chatter around the object is dropped");
  check(ai_describe_extract_actions("I cannot do that.").empty(), "no JSON at all is empty");
}

static void test_comfy() {
  std::printf("comfy...\n");
  std::string wf = comfy_bundled_workflow("text_to_image");
  check(!wf.empty(), "a bundled workflow exists");
  std::vector<ComfyBinding> b = comfy_infer_bindings(wf);
  auto bound = [&](const char *src, const char *node, const char *input) {
    for (const ComfyBinding &x : b)
      if (x.source == src && x.node_id == node && x.input == input) return true;
    return false;
  };
  check(bound("prompt", "6", "text") && bound("negative", "7", "text"), "prompt and negative found by title");
  check(bound("seed", "3", "seed") && bound("steps", "3", "steps") && bound("cfg", "3", "cfg"), "sampler inputs found");
  check(bound("width", "5", "width") && bound("height", "5", "height"), "size found on the latent");
  std::vector<std::string> outs = comfy_output_nodes(wf);
  check(outs.size() == 1 && outs[0] == "9", "the SaveImage is the output");
  std::string err;
  std::map<std::string, std::string> params = {{"prompt", "mossy rock"}, {"seed", "77"}, {"width", "512"}, {"height", "512"}, {"steps", ""}};
  check(comfy_inject(wf, b, params, err), err.c_str());
  check(has(wf, "\"text\":\"mossy rock\"") && has(wf, "\"seed\":77") && has(wf, "\"width\":512"), "values land in the right inputs");
  check(has(wf, "\"steps\":28"), "an empty value leaves the workflow's default");
  check(!has(wf, "\"seed\":\"77\""), "a number stays a number");
  std::vector<ComfyBinding> bad = {{"prompt", "99", "text"}};
  check(!comfy_inject(wf, bad, params, err) && has(err, "99"), "a missing node id is an error that names it");
  std::string junk = "{";
  check(!comfy_inject(junk, b, params, err), "a broken workflow is refused");
  std::string fn, sub, type;
  check(parse_comfy_history(R"({"abc":{"outputs":{"9":{"images":[{"filename":"TerraForge_00001_.png","subfolder":"","type":"output"}]}}}})", "abc", fn, sub, type) &&
            fn == "TerraForge_00001_.png" && type == "output",
        "history yields the image");
  check(!parse_comfy_history(R"({})", "abc", fn, sub, type), "an unfinished prompt has no image yet");
  check(comfy_find_workflow("no-such-workflow", err).empty() && has(err, "no-such-workflow"), "an unknown workflow says so");
}

static void test_models() {
  std::printf("3d providers...\n");
  TaskStatus st;
  check(parse_meshy_status(R"({"id":"t1","status":"IN_PROGRESS","progress":45})", st) && st.state == TaskStatus::RUNNING && st.progress > 0.44f && st.progress < 0.46f,
        "Meshy in progress at 45%");
  check(parse_meshy_status(R"({"id":"t1","status":"SUCCEEDED","progress":100,"model_urls":{"glb":"https://m/x.glb"},"thumbnail_url":"https://m/t.png"})", st) &&
            st.state == TaskStatus::SUCCEEDED && st.result_url == "https://m/x.glb" && st.thumb_url == "https://m/t.png",
        "Meshy success gives the glb");
  check(parse_meshy_status(R"({"id":"t1","status":"FAILED","task_error":{"message":"nsfw"}})", st) && st.state == TaskStatus::FAILED && st.message == "nsfw",
        "Meshy failure carries the reason");
  check(parse_tripo_status(R"({"code":0,"data":{"task_id":"a","status":"running","progress":0.3}})", st) && st.state == TaskStatus::RUNNING,
        "Tripo running (fractional progress)");
  check(parse_tripo_status(R"({"code":0,"data":{"task_id":"a","status":"success","output":{"model":"https://t/m.glb","rendered_image":"https://t/r.png"}}})", st) &&
            st.state == TaskStatus::SUCCEEDED && st.result_url == "https://t/m.glb",
        "Tripo success gives the model");
  check(parse_tripo_status(R"({"code":2001,"message":"invalid key"})", st) && st.state == TaskStatus::FAILED, "Tripo error code without data is a failure");
  check(parse_hitems_status(R"({"code":0,"data":{"task_id":"h","task_status":1,"process_pct":30}})", st) && st.state != TaskStatus::SUCCEEDED && st.progress > 0.29f,
        "Hitem3D queued with percent");
  check(parse_hitems_status(R"({"data":{"task_id":"h","task_status":4,"task_result":{"model_url":"https://h/m.obj"}}})", st) &&
            st.state == TaskStatus::SUCCEEDED && st.result_url == "https://h/m.obj",
        "Hitem3D success by status 4");
  check(parse_hitems_status(R"({"data":{"task_id":"h","task_status":-1,"msg":"bad image"}})", st) && st.state == TaskStatus::FAILED, "Hitem3D failure by -1");
  check(!parse_meshy_status("<html>", st) && !parse_tripo_status("", st), "non-JSON is refused");
  check(hitems_auth_header("ak123:sk456") == "Basic " + base64_encode("ak123:sk456"), "ak:sk becomes Basic");
  check(hitems_auth_header("tok") == "Bearer tok" && hitems_auth_header("Bearer x") == "Bearer x", "a token becomes Bearer, a header passes through");
  check(has(meshy_text_json("a tree", ""), "\"mode\":\"preview\"") && has(meshy_text_json("a tree", ""), "meshy-4"), "Meshy text body");
  check(has(tripo_text_json("a tree"), "text_to_model"), "Tripo text body");
}

static void test_not_configured() {
  std::printf("unconfigured providers fail cleanly...\n");
  config().services.clear();
  std::string out, err;
  check(!ai_text("openai", "s", "p", "", out, err) && has(err, "not configured"), "text without a key says so");
  GenImageRequest rq;
  rq.provider = "stability";
  GenResult r = ai_generate_image(rq, nullptr, nullptr);
  check(!r.ok && has(r.message, "not configured"), "image without a key says so");
  GenModelRequest mq;
  mq.provider = "meshy";
  r = ai_generate_model(mq, nullptr, nullptr);
  check(!r.ok && has(r.message, "not configured"), "model without a key says so");
  check(!ai_text("nobody", "s", "p", "", out, err), "an unknown provider fails");
}

int main() {
  test_http_helpers();
  test_text_formats();
  test_image_formats();
  test_prompts();
  test_describe_reply();
  test_comfy();
  test_models();
  test_not_configured();
  if (failures) {
    std::printf("%d AI service check(s) failed\n", failures);
    return 1;
  }
  std::printf("AI service tests passed\n");
  return 0;
}
