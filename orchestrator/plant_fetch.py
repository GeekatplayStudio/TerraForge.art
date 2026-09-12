"""Geekatplay TerraForge - fetch CC0 plant models into the plant library.

    python -m orchestrator.plant_fetch --dest <library>/plants [--res 1k]
        [--ids fern_02,shrub_03] [--progress file] [--list]

Poly Haven (https://polyhaven.com) publishes photoscanned plants under CC0 1.0:
no attribution is required and they may be used and redistributed for any
purpose. They are fetched on request, as the ambientCG material sets are, and
never shipped inside the application (docs/LICENSING.md). Each plant lands in
<dest>/polyhaven/<id>/ as the glTF Poly Haven serves - the .gltf, its .bin and
its textures - with a thumbnail, a LICENSE.txt naming the source and the
authors, and plant.json, the manifest the Plants tab reads.

A file already present with the md5 the API lists is not fetched again, so a
second run only completes what the first one could not.

Two things about the files as served are put right on the way in. A cut-out
leaf's colour picture is a JPEG with no alpha, its cut-out a separate map, so
the two are merged into one PNG (merge_alpha). And one file is often a set:
fern_02 is three ferns a metre apart, grass_bermuda_01 a dozen tufts in a row.
Each of those becomes a glTF of its own that shares the set's buffer and
pictures, stood at the origin (split_variants), and the manifest lists them.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
import os
import re
import sys
import urllib.request

API = "https://api.polyhaven.com"
AGENT = "GeekatplayTerraForge-plant-library/1.0"

# The set a new library starts with: trees, shrubs, ground cover, grass and
# flowers for the landscapes the studio builds - temperate forest, desert,
# jungle, meadow - each light enough to scatter (tens of thousands of
# triangles at most, a few hundred thousand for the trees).
CURATED = [
    # trees and trunks
    "jacaranda_tree", "quiver_tree_01", "quiver_tree_02", "pachira_aquatica_01",
    "fir_sapling", "pine_sapling_small", "dead_tree_trunk_02", "tree_stump_01",
    # shrubs
    "shrub_02", "shrub_03", "shrub_04", "shrub_sorrel_01", "wild_rooibos_bush",
    # ground cover, grass, flowers
    "fern_02", "grass_bermuda_01", "dandelion_01", "nettle_plant", "periwinkle_plant",
    "weed_plant_02", "calathea_orbifolia_01", "anthurium_botany_01", "flower_gazania",
    "flower_ursinia", "flower_empodium", "crystalline_iceplant", "dry_branches_medium_01",
]


def _get_json(url: str, opener=None) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with (opener or urllib.request.urlopen)(req, timeout=60) as r:
        return json.load(r)


def _md5(path: str) -> str:
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _download(url: str, dest: str, md5: str | None = None, opener=None) -> bool:
    """Fetch url to dest unless a file with that md5 is already there.
    True when a download happened."""
    if md5 and os.path.isfile(dest) and _md5(dest) == md5:
        return False
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": AGENT})
    tmp = dest + ".part"
    with (opener or urllib.request.urlopen)(req, timeout=120) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 16)
            if not chunk:
                break
            f.write(chunk)
    if md5 and _md5(tmp) != md5:
        os.remove(tmp)
        raise IOError(f"checksum mismatch for {url}")
    os.replace(tmp, dest)
    return True


def gltf_files(files: dict, res: str) -> dict | None:
    """The glTF entry of an asset's file listing at `res`, or else the
    smallest resolution it has; None when it has no glTF at all."""
    g = files.get("gltf") or {}
    for r in [res, "1k", "2k", "4k", "8k"]:
        if r in g and g[r].get("gltf"):
            return g[r]["gltf"]
    return None


def plant_group(asset_id: str, categories: list) -> str:
    """The shelf the Plants tab puts a plant on. Poly Haven files shrubs
    under ground cover and a stump under trees; a person looking for a bush
    or for dead wood looks for those words."""
    if "stump" in asset_id or "dead" in asset_id or "branches" in asset_id:
        return "deadwood"
    if "trees" in categories or "tree" in asset_id or "sapling" in asset_id:
        return "trees"
    if asset_id.startswith("shrub") or "bush" in asset_id:
        return "shrubs"
    if "grass" in categories or "grass" in asset_id:
        return "grass"
    if "flowers" in categories or asset_id.startswith("flower"):
        return "flowers"
    return "ground cover"


def manifest(asset_id: str, info: dict, model_name: str, res: str,
             variants: list | None = None, height_m: float | None = None) -> dict:
    """What the Plants tab needs to show and place a plant."""
    if height_m is None:
        dims = info.get("dimensions") or []
        # Poly Haven gives dimensions in millimetres, Blender's z-up: z is
        # height - but of the whole file, every variant in it laid side by side
        height_m = round(float(dims[2]) / 1000.0, 3) if len(dims) >= 3 else None
    categories = [c for c in info.get("categories", []) if not c.startswith("collection:")]
    return {
        "id": asset_id,
        "name": info.get("name", asset_id),
        "source": "polyhaven",
        "url": f"https://polyhaven.com/a/{asset_id}",
        "license": "CC0 1.0",
        "authors": sorted((info.get("authors") or {}).keys()),
        "categories": categories,
        "group": plant_group(asset_id, categories),
        "tags": info.get("tags", []),
        "polycount": info.get("polycount"),
        "height_m": height_m,
        "unit_m": 1.0,  # glTF is in metres
        "model": model_name,
        "variants": variants or [],
        "thumbnail": "thumb.png",
        "resolution": res,
        "fetched": datetime.date.today().isoformat(),
    }


def license_text(m: dict) -> str:
    return (f"{m['name']} ({m['id']})\n"
            f"Source: {m['url']}\n"
            f"Authors: {', '.join(m['authors']) or 'Poly Haven'}\n"
            "Licence: CC0 1.0 Universal (public domain dedication)\n"
            "https://creativecommons.org/publicdomain/zero/1.0/\n"
            "No attribution is required; credit to Poly Haven and the authors is appreciated.\n")


def _all_urls(node, out: dict) -> dict:
    """Every downloadable file of a listing by its file name: {name: (url, md5)}."""
    if isinstance(node, dict):
        if isinstance(node.get("url"), str):
            out.setdefault(node["url"].rsplit("/", 1)[-1], (node["url"], node.get("md5")))
        for v in node.values():
            _all_urls(v, out)
    return out


def alpha_file_for(image: dict, res: str) -> list[str]:
    """The file names an alpha map for a colour picture can have. Poly Haven
    names a cut-out's colour image "<x>_diff-<x>_alpha" in the glTF, but the
    JPEG it serves has no alpha channel - the cut-out lives in a map of its
    own."""
    names = []
    label = image.get("name", "")
    if "-" in label:
        names.append(label.split("-", 1)[1])
    uri = image.get("uri", "").rsplit("/", 1)[-1]
    if "_diff" in uri:
        names.append(uri.split("_diff")[0] + "_alpha")
    out = []
    for n in names:
        out += [f"{n}_{res}.png", f"{n}_{res}.jpg"]
    return out


def merge_alpha(folder: str, gltf_name: str, files: dict, res: str, opener=None) -> int:
    """Give every cut-out material's colour picture its alpha. The viewport
    and the offline engines cut a leaf where the picture's alpha is under a
    half; with the JPEG alone every leaf card was a solid rectangle. Writes a
    PNG beside the JPEG and points the glTF at it. Returns how many pictures
    were merged (0 without Pillow, which reports why)."""
    path = os.path.join(folder, gltf_name)
    with open(path, "r", encoding="utf-8") as f:
        g = json.load(f)
    textures, images = g.get("textures", []), g.get("images", [])
    wanted = set()
    for m in g.get("materials", []):
        if m.get("alphaMode", "OPAQUE") in ("MASK", "BLEND"):
            t = (m.get("pbrMetallicRoughness") or {}).get("baseColorTexture")
            if t and 0 <= t.get("index", -1) < len(textures):
                src = textures[t["index"]].get("source", -1)
                if 0 <= src < len(images):
                    wanted.add(src)
            m["alphaMode"] = "MASK"  # we cut, we do not blend
            m.setdefault("alphaCutoff", 0.5)
    if not wanted:
        return 0
    try:
        from PIL import Image
    except ImportError:
        print("Pillow is not installed: leaf cut-outs are left solid (pip install pillow)")
        return 0
    urls = _all_urls(files, {})
    merged = 0
    for i in sorted(wanted):
        im = images[i]
        colour = os.path.join(folder, *im["uri"].split("/"))
        alpha_name = next((n for n in alpha_file_for(im, res) if n in urls), None)
        if not alpha_name or not os.path.isfile(colour) or im["uri"].endswith(".png"):
            continue
        url, md5 = urls[alpha_name]
        alpha_path = os.path.join(os.path.dirname(colour), alpha_name)
        _download(url, alpha_path, md5, opener)
        rgb = Image.open(colour).convert("RGB")
        a = Image.open(alpha_path).convert("L").resize(rgb.size)
        rgba = rgb.copy()
        rgba.putalpha(a)
        out_rel = im["uri"].rsplit(".", 1)[0] + "_cutout.png"
        rgba.save(os.path.join(folder, *out_rel.split("/")))
        im["uri"], im["mimeType"] = out_rel, "image/png"
        merged += 1
    with open(path, "w", encoding="utf-8") as f:
        json.dump(g, f, indent=1)
    return merged


IDENTITY = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def _node_matrix(n: dict) -> list:
    """A glTF node's local transform, column-major."""
    if isinstance(n.get("matrix"), list) and len(n["matrix"]) == 16:
        return [float(v) for v in n["matrix"]]
    t = n.get("translation") or [0.0, 0.0, 0.0]
    s = n.get("scale") or [1.0, 1.0, 1.0]
    x, y, z, w = n.get("rotation") or [0.0, 0.0, 0.0, 1.0]
    cols = [(1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)),
            (2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)),
            (2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y))]
    m = []
    for c in range(3):
        m += [cols[c][0] * s[c], cols[c][1] * s[c], cols[c][2] * s[c], 0.0]
    return m + [float(t[0]), float(t[1]), float(t[2]), 1.0]


def _mat_mul(a: list, b: list) -> list:
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def subtree_bounds(g: dict, node: int, parent: list, lo: list, hi: list, depth: int = 0) -> int:
    """Grow lo/hi by the box of everything under `node`, placed by `parent`;
    returns its triangle count. From the accessors' min and max, which glTF
    requires on positions, so no vertex is read."""
    nodes = g.get("nodes", [])
    if depth > 64 or not 0 <= node < len(nodes):
        return 0
    n = nodes[node]
    m = _mat_mul(parent, _node_matrix(n))
    tris = 0
    meshes, accessors = g.get("meshes", []), g.get("accessors", [])
    if 0 <= n.get("mesh", -1) < len(meshes):
        for prim in meshes[n["mesh"]].get("primitives", []):
            pos = accessors[prim["attributes"]["POSITION"]]
            if pos.get("min") and pos.get("max"):
                for c in range(8):
                    p = [pos["max"][k] if (c >> k) & 1 else pos["min"][k] for k in range(3)]
                    for r in range(3):
                        v = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r]
                        lo[r], hi[r] = min(lo[r], v), max(hi[r], v)
            count = accessors[prim["indices"]]["count"] if "indices" in prim else pos["count"]
            tris += count // 3
    for child in n.get("children", []):
        tris += subtree_bounds(g, child, m, lo, hi, depth + 1)
    return tris


LOD_SUFFIX = re.compile(r"_LOD(\d+)$", re.IGNORECASE)


def _tail_tokens(asset_id: str, node_name: str) -> list[str]:
    """A root's name without the asset's id or a level of detail, as words:
    "pachira_aquatica_01_leaves_a" -> ["leaves", "a"]."""
    tail = LOD_SUFFIX.sub("", node_name)
    tail = tail[len(asset_id):] if tail.startswith(asset_id) else tail
    return [t for t in tail.split("_") if t]


def _overlap(a: tuple, b: tuple) -> bool:
    """Whether two roots are one plant: their footprints share at least half
    the smaller one. The parts of one plant stand in one place - a pachira's
    bark inside its leaves - while a file's variants are laid out apart."""
    (alo, ahi), (blo, bhi) = a, b
    w = min(ahi[0], bhi[0]) - max(alo[0], blo[0])
    d = min(ahi[2], bhi[2]) - max(alo[2], blo[2])
    if w <= 0.0 or d <= 0.0:
        return False
    area = lambda lo, hi: max((hi[0] - lo[0]) * (hi[2] - lo[2]), 1e-12)
    return w * d >= 0.5 * min(area(alo, ahi), area(blo, bhi))


def split_variants(folder: str, model_name: str, asset_id: str) -> tuple[list, float | None]:
    """One glTF per plant in a file that holds several, and the tallest one's
    height. Every root node of the scene is a plant or a part of one: a name
    ending in _LOD<n> is a plant at a level of detail, and only its finest is
    kept; roots standing in one place are the parts of one plant (a trunk and
    its leaves) and stay together. Each variant file shares the set's buffer
    and pictures - only its scene differs - with the plant moved to stand at
    the origin. A file holding one plant is left alone and has no variants."""
    path = os.path.join(folder, model_name)
    with open(path, "r", encoding="utf-8") as f:
        g = json.load(f)
    nodes, scenes = g.get("nodes", []), g.get("scenes", [])
    si = g.get("scene", 0)
    roots = [r for r in (scenes[si].get("nodes", []) if 0 <= si < len(scenes) else [])
             if 0 <= r < len(nodes)]
    finest: dict[str, tuple[int, int]] = {}
    for r in roots:
        name = nodes[r].get("name", "") or f"#{r}"
        m = LOD_SUFFIX.search(name)
        base, lod = (LOD_SUFFIX.sub("", name), int(m.group(1))) if m else (name, 0)
        if base not in finest or lod < finest[base][1]:
            finest[base] = (r, lod)
    boxes = {}
    for r in sorted(r for r, _ in finest.values()):
        lo, hi = [math.inf] * 3, [-math.inf] * 3
        tris = subtree_bounds(g, r, IDENTITY, lo, hi)
        if lo[0] != math.inf:
            boxes[r] = (lo, hi, tris)
    if not boxes:
        return [], None

    # the parts of one plant, by where they stand
    groups: list[list[int]] = []
    for r in boxes:
        home = [grp for grp in groups if any(_overlap(boxes[r][:2], boxes[o][:2]) for o in grp)]
        merged = [r] + [o for grp in home for o in grp]
        groups = [grp for grp in groups if grp not in home] + [sorted(merged)]
    groups.sort(key=lambda grp: grp[0])

    def box_of(grp):
        lo, hi = [math.inf] * 3, [-math.inf] * 3
        for r in grp:
            for k in range(3):
                lo[k], hi[k] = min(lo[k], boxes[r][0][k]), max(hi[k], boxes[r][1][k])
        return lo, hi

    def height(lo, hi):
        # above the ground the scan stands on (y 0): roots under it do not count
        return round(hi[1] - max(lo[1], 0.0) if hi[1] > 0.0 else hi[1] - lo[1], 3)

    tallest = max(height(*box_of(grp)) for grp in groups)
    stem = model_name.rsplit(".", 1)[0]
    for old in os.listdir(folder):  # an earlier run's split, which may have differed
        if old.startswith(stem + "_") and old.endswith(".gltf"):
            os.remove(os.path.join(folder, old))
    if len(groups) == 1 and len(groups[0]) == len(roots):
        return [], tallest
    variants, used = [], set()
    for i, grp in enumerate(groups):
        lo, hi = box_of(grp)
        cx, cz = (lo[0] + hi[0]) * 0.5, (lo[2] + hi[2]) * 0.5
        v = json.loads(json.dumps(g))
        for r in grp:
            node = v["nodes"][r]
            if isinstance(node.get("matrix"), list) and len(node["matrix"]) == 16:
                node["matrix"][12] -= cx
                node["matrix"][14] -= cz
            else:
                t = node.get("translation") or [0.0, 0.0, 0.0]
                node["translation"] = [t[0] - cx, t[1], t[2] - cz]
        v["scenes"], v["scene"] = [{"nodes": grp}], 0
        # the words the parts' names share from the end: bark_a + leaves_a is "a"
        words = [_tail_tokens(asset_id, nodes[r].get("name", "")) for r in grp]
        common = []
        while all(len(w) > len(common) for w in words) and \
                len({tuple(w[len(w) - len(common) - 1:]) for w in words}) == 1:
            common = words[0][len(words[0]) - len(common) - 1:]
        label = " ".join(common or words[0]) or str(i + 1)
        slug = re.sub(r"[^a-z0-9]+", "_", label.lower()).strip("_") or str(i + 1)
        while slug in used:
            slug += "_"
        used.add(slug)
        name = f"{stem}_{slug}.gltf"
        with open(os.path.join(folder, name), "w", encoding="utf-8") as f:
            json.dump(v, f, indent=1)
        variants.append({"id": slug, "name": label, "model": name, "height_m": height(lo, hi),
                         "width_m": round(max(hi[0] - lo[0], hi[2] - lo[2]), 3),
                         "polycount": sum(boxes[r][2] for r in grp)})
    return variants, tallest


def normalise_thumb(path: str, size: int = 256) -> bool:
    """The picture as a square PNG. The CDN answers some requests with WebP
    under the .png name, which the studio's image reader cannot open, and its
    pictures are cut to the model's own outline - a flower is 256 by 39 - so
    they are centred on a clear square. False without Pillow."""
    try:
        from PIL import Image
    except ImportError:
        return False
    try:
        im = Image.open(path).convert("RGBA")
    except OSError:
        return False
    im.thumbnail((size, size))
    square = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    square.paste(im, ((size - im.width) // 2, (size - im.height) // 2))
    square.save(path, "PNG")
    return True


def fetch_one(asset_id: str, dest_root: str, res: str = "1k", opener=None) -> dict:
    info = _get_json(f"{API}/info/{asset_id}", opener)
    files = _get_json(f"{API}/files/{asset_id}", opener)
    entry = gltf_files(files, res)
    if not entry:
        raise ValueError(f"{asset_id} has no glTF download")
    folder = os.path.join(dest_root, "polyhaven", asset_id)
    model_name = entry["url"].rsplit("/", 1)[-1]
    # the served glTF is kept as .source.gltf; the one the studio opens is
    # rewritten from it (merge_alpha), so a second run re-merges rather than
    # fetching a changed file again
    source = os.path.join(folder, model_name.rsplit(".", 1)[0] + ".source.gltf")
    _download(entry["url"], source, entry.get("md5"), opener)
    for rel, sub in (entry.get("include") or {}).items():
        _download(sub["url"], os.path.join(folder, *rel.split("/")), sub.get("md5"), opener)
    with open(source, "rb") as f_in, open(os.path.join(folder, model_name), "wb") as f_out:
        f_out.write(f_in.read())
    merge_alpha(folder, model_name, files, res, opener)
    variants, height_m = split_variants(folder, model_name, asset_id)
    if info.get("thumbnail_url"):
        thumb = os.path.join(folder, "thumb.png")
        try:
            _download(info["thumbnail_url"], thumb, None, opener)
            normalise_thumb(thumb)
        except OSError:
            pass  # a plant without a picture is still a plant
    m = manifest(asset_id, info, model_name, res, variants, height_m)
    with open(os.path.join(folder, "plant.json"), "w", encoding="utf-8") as f:
        json.dump(m, f, indent=1)
    with open(os.path.join(folder, "LICENSE.txt"), "w", encoding="utf-8") as f:
        f.write(license_text(m))
    return m


def _progress(path: str | None, text: str) -> None:
    if not path:
        return
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    except OSError:
        pass


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Fetch CC0 plants from Poly Haven into the plant library.")
    ap.add_argument("--dest", required=False, help="the plants folder of the library")
    ap.add_argument("--res", default="1k", help="texture resolution: 1k, 2k, 4k")
    ap.add_argument("--ids", default="", help="comma-separated asset ids (default: the curated set)")
    ap.add_argument("--progress", default=None, help="a file to write one-line progress to")
    ap.add_argument("--list", action="store_true", help="print the curated ids and exit")
    args = ap.parse_args(argv)
    if args.list:
        print("\n".join(CURATED))
        return 0
    if not args.dest:
        ap.error("--dest is required")
    ids = [i.strip() for i in args.ids.split(",") if i.strip()] or CURATED
    failed = []
    for n, asset_id in enumerate(ids):
        _progress(args.progress, f"plants {n}/{len(ids)}: {asset_id}")
        try:
            m = fetch_one(asset_id, args.dest, args.res)
            print(f"ok {asset_id}: {m['name']}", flush=True)
        except Exception as e:  # keep going; report at the end
            failed.append(asset_id)
            print(f"failed {asset_id}: {e}", flush=True)
    _progress(args.progress, f"done {len(ids) - len(failed)}/{len(ids)}"
                             + (f", failed: {', '.join(failed)}" if failed else ""))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
