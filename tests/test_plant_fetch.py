"""The free-plant fetcher, without a network.

orchestrator/plant_fetch.py turns Poly Haven's glTF downloads into the plant
library's folders. What it must get right is pinned here against files these
tests build: which download it picks, the manifest the Plants tab reads, a
file already fetched not fetched again, a cut-out leaf's alpha merged into
its colour picture, a set of plants split into one glTF each (a plant's parts
kept together, a level of detail kept once), and the picture made readable.
"""
import hashlib
import io
import json
import math
import os

import pytest

from orchestrator import plant_fetch as pf


def _gltf(nodes, meshes, materials=None, images=None, textures=None):
    return {"asset": {"version": "2.0"}, "scene": 0,
            "scenes": [{"nodes": list(range(len(nodes)))}],
            "nodes": nodes, "meshes": meshes,
            "accessors": [{"count": 3, "min": [-0.5, 0.0, -0.5], "max": [0.5, 1.0, 0.5]},
                          {"count": 6}],
            "materials": materials or [], "images": images or [], "textures": textures or []}


def _mesh():
    return {"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}


def test_gltf_files_prefers_the_asked_resolution_then_the_smallest():
    files = {"gltf": {"2k": {"gltf": {"url": "u2"}}, "4k": {"gltf": {"url": "u4"}}}}
    assert pf.gltf_files(files, "4k")["url"] == "u4"
    assert pf.gltf_files(files, "1k")["url"] == "u2"
    assert pf.gltf_files({}, "1k") is None


def test_plant_group_names_the_shelves_a_person_looks_on():
    assert pf.plant_group("tree_stump_01", ["trees"]) == "deadwood"
    assert pf.plant_group("fir_sapling", ["plants"]) == "trees"
    assert pf.plant_group("shrub_03", ["ground cover"]) == "shrubs"
    assert pf.plant_group("grass_bermuda_01", ["grass"]) == "grass"
    assert pf.plant_group("flower_ursinia", ["flowers"]) == "flowers"
    assert pf.plant_group("fern_02", ["plants"]) == "ground cover"


def test_manifest_carries_what_the_plants_tab_reads():
    info = {"name": "Fern 02", "authors": {"Rico": "All"}, "polycount": 6232,
            "categories": ["plants", "collection: x"], "tags": ["leafy"],
            "dimensions": [1972.5, 1724.0, 427.7]}
    variants = [{"id": "a", "name": "a", "model": "f_a.gltf", "height_m": 0.26}]
    m = pf.manifest("fern_02", info, "fern_02_1k.gltf", "1k", variants, 0.397)
    assert m["id"] == "fern_02" and m["source"] == "polyhaven" and m["license"] == "CC0 1.0"
    assert m["categories"] == ["plants"]  # collections are Poly Haven's filing, not a plant's
    assert m["group"] == "ground cover" and m["unit_m"] == 1.0
    assert m["height_m"] == 0.397 and m["variants"] == variants
    # without a measured height, the file's own dimensions (millimetres, z up)
    assert pf.manifest("fern_02", info, "f.gltf", "1k")["height_m"] == pytest.approx(0.428)
    assert "CC0" in pf.license_text(m) and "Rico" in pf.license_text(m)


class _Opener:
    """urlopen's stand-in: serves bytes by URL and counts the calls."""

    def __init__(self, blobs):
        self.blobs, self.calls = blobs, []

    def __call__(self, req, timeout=0):
        self.calls.append(req.full_url)
        return io.BytesIO(self.blobs[req.full_url])


def test_download_skips_a_file_already_there_and_refuses_a_bad_checksum(tmp_path):
    data = b"leafy" * 100
    md5 = hashlib.md5(data).hexdigest()
    opener = _Opener({"http://x/a": data, "http://x/bad": b"other"})
    dest = tmp_path / "sub" / "a.bin"
    assert pf._download("http://x/a", str(dest), md5, opener) is True
    assert dest.read_bytes() == data
    assert pf._download("http://x/a", str(dest), md5, opener) is False
    assert len(opener.calls) == 1
    with pytest.raises(IOError):
        pf._download("http://x/bad", str(tmp_path / "b.bin"), md5, opener)
    assert not (tmp_path / "b.bin").exists() and not (tmp_path / "b.bin.part").exists()


def test_alpha_file_for_reads_both_of_poly_havens_namings():
    names = pf.alpha_file_for({"name": "fern_02_diff-fern_02_alpha",
                               "uri": "textures/fern_02_diff_1k.jpg"}, "1k")
    assert names[0] == "fern_02_alpha_1k.png" and "fern_02_alpha_1k.jpg" in names


def test_merge_alpha_gives_the_leaf_its_cut_out(tmp_path):
    Image = pytest.importorskip("PIL.Image")
    (tmp_path / "textures").mkdir()
    Image.new("RGB", (8, 8), (40, 120, 30)).save(tmp_path / "textures" / "leaf_diff_1k.jpg")
    alpha = Image.new("L", (8, 8), 0)
    alpha.paste(255, (0, 0, 4, 4))  # the leaf: the top-left quarter
    buf = io.BytesIO()
    alpha.save(buf, "PNG")
    g = _gltf([{"mesh": 0}], [_mesh()],
              materials=[{"alphaMode": "BLEND",
                          "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
              images=[{"name": "leaf_diff-leaf_alpha", "uri": "textures/leaf_diff_1k.jpg",
                       "mimeType": "image/jpeg"}],
              textures=[{"source": 0}])
    (tmp_path / "leaf.gltf").write_text(json.dumps(g))
    files = {"Alpha": {"1k": {"png": {"url": "http://x/leaf_alpha_1k.png",
                                      "md5": hashlib.md5(buf.getvalue()).hexdigest()}}}}
    opener = _Opener({"http://x/leaf_alpha_1k.png": buf.getvalue()})
    assert pf.merge_alpha(str(tmp_path), "leaf.gltf", files, "1k", opener) == 1
    out = json.loads((tmp_path / "leaf.gltf").read_text())
    assert out["images"][0]["uri"] == "textures/leaf_diff_1k_cutout.png"
    assert out["images"][0]["mimeType"] == "image/png"
    assert out["materials"][0]["alphaMode"] == "MASK"  # cut, never blended
    rgba = Image.open(tmp_path / "textures" / "leaf_diff_1k_cutout.png")
    assert rgba.mode == "RGBA" and rgba.size == (8, 8)
    assert rgba.getpixel((1, 1))[3] == 255 and rgba.getpixel((6, 6))[3] == 0
    assert rgba.getpixel((1, 1))[:3] == pytest.approx((40, 120, 30), abs=4)  # the colour kept


def test_split_variants_one_glb_per_plant_parts_together_lod_once(tmp_path):
    nodes = [
        # a plant of two parts standing in one place: bark inside leaves
        {"name": "palm_bark_a", "mesh": 0, "translation": [-2.0, 0.0, 0.0], "scale": [0.3, 1.0, 0.3]},
        {"name": "palm_leaves_a", "mesh": 0, "translation": [-2.0, 0.5, 0.0]},
        # a second plant a few metres off, with a coarser copy of itself
        {"name": "palm_b_LOD0", "mesh": 0, "translation": [3.0, 0.0, 1.0]},
        {"name": "palm_b_LOD1", "mesh": 0, "translation": [3.0, 0.0, 1.0]},
    ]
    (tmp_path / "palm_1k.gltf").write_text(json.dumps(_gltf(nodes, [_mesh()])))
    (tmp_path / "palm_1k_stale.gltf").write_text("{}")  # an earlier run's split
    variants, tallest = pf.split_variants(str(tmp_path), "palm_1k.gltf", "palm")
    assert [v["name"] for v in variants] == ["a", "b"]
    assert not (tmp_path / "palm_1k_stale.gltf").exists()
    a = json.loads((tmp_path / variants[0]["model"]).read_text())
    assert a["scenes"] == [{"nodes": [0, 1]}] and a["scene"] == 0
    b = json.loads((tmp_path / variants[1]["model"]).read_text())
    assert b["scenes"] == [{"nodes": [2]}]  # the finest level only
    # each stands at the origin: its footprint centred on x and z
    lo, hi = [math.inf] * 3, [-math.inf] * 3
    for r in b["scenes"][0]["nodes"]:
        pf.subtree_bounds(b, r, pf.IDENTITY, lo, hi)
    assert (lo[0] + hi[0]) / 2 == pytest.approx(0.0) and (lo[2] + hi[2]) / 2 == pytest.approx(0.0)
    assert variants[0]["height_m"] == pytest.approx(1.5) and tallest == pytest.approx(1.5)
    assert variants[1]["polycount"] == 2


def test_a_file_holding_one_plant_is_left_alone(tmp_path):
    (tmp_path / "tree_1k.gltf").write_text(json.dumps(_gltf([{"name": "tree", "mesh": 0}], [_mesh()])))
    variants, tallest = pf.split_variants(str(tmp_path), "tree_1k.gltf", "tree")
    assert variants == [] and tallest == pytest.approx(1.0)
    assert sorted(os.listdir(tmp_path)) == ["tree_1k.gltf"]


def test_node_matrix_matches_translation_rotation_scale():
    # a quarter turn about +y takes +x to -z, then the offset
    n = {"translation": [1.0, 2.0, 3.0], "rotation": [0.0, math.sin(math.pi / 4), 0.0, math.cos(math.pi / 4)],
         "scale": [2.0, 2.0, 2.0]}
    m = pf._node_matrix(n)
    p = [m[r] * 1.0 + m[12 + r] for r in range(3)]
    assert p == pytest.approx([1.0, 2.0, 1.0], abs=1e-6)


def test_normalise_thumb_makes_a_square_png_of_any_picture(tmp_path):
    Image = pytest.importorskip("PIL.Image")
    path = tmp_path / "thumb.png"
    Image.new("RGBA", (256, 39), (200, 100, 50, 255)).save(path, "WEBP")  # the CDN's answer
    assert pf.normalise_thumb(str(path)) is True
    im = Image.open(path)
    assert im.format == "PNG" and im.size == (256, 256)
    assert im.getpixel((128, 128))[3] == 255 and im.getpixel((128, 2))[3] == 0


def test_the_curated_set_and_the_cli(capsys):
    assert len(pf.CURATED) == len(set(pf.CURATED)) >= 20
    assert pf.main(["--list"]) == 0
    assert "fern_02" in capsys.readouterr().out
