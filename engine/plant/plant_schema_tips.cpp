// Geekatplay TerraForge - what every plant parameter means, in words
// (plant_schema.hpp). One entry per (kind, key); the shared blocks
// (Transform, Level of detail, Attachment, Seasons, Ambient motion) are
// registered once under Kind::COUNT and apply to every kind. A key missing
// here is a gap the settings coverage test counts, so keep it complete.
// The tables themselves are in plant_schema_tips_*.cpp.
#include "plant/plant_schema.hpp"
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <cstring>
#include <vector>

namespace gpx {
namespace plant {

namespace {
std::vector<const Tip *> &registry() {
  static std::vector<const Tip *> r;
  return r;
}
} // namespace

// Called by the per-family tip tables at static initialisation.
void tips_register(const Tip *table, size_t count) {
  for (size_t i = 0; i < count; ++i) registry().push_back(table + i);
}

const char *tooltip(Kind k, const char *key) {
  for (const Tip *t : registry())
    if (t->kind == k && std::strcmp(t->key, key) == 0) return t->text;
  for (const Tip *t : registry())
    if (t->kind == Kind::COUNT && std::strcmp(t->key, key) == 0) return t->text;
  return "";
}

} // namespace plant
} // namespace gpx
