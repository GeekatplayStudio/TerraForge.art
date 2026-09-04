"""Build the public website into a folder you can upload as-is.

    python scripts/make_site.py
    python scripts/make_site.py --base-url https://terraforge.art
    python scripts/make_site.py --out D:/upload/terraforge

The source of the site is docs/index.html and docs/images. That folder also
holds the markdown documentation, which has no business on a web host, so the
upload set is assembled here rather than pointing a host at docs/ and hoping.

--base-url rewrites the Open Graph tags to absolute URLs. Social networks
fetch those with their own scrapers, from their own machines, and a relative
path means nothing there - so the preview card is blank without it. Everything
else on the page stays relative, so the folder works at a domain root, in a
subdirectory, or opened straight off the disk.
"""
import argparse
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "site"),
                    help="folder to build into (default: dist/site)")
    ap.add_argument("--base-url", default="",
                    help="public URL of the site, e.g. https://terraforge.art "
                         "- makes the Open Graph tags absolute")
    args = ap.parse_args()

    src_html = os.path.join(ROOT, "docs", "index.html")
    src_img = os.path.join(ROOT, "docs", "images")
    if not os.path.isfile(src_html):
        print(f"missing {src_html}", file=sys.stderr)
        return 1

    out = os.path.abspath(args.out)
    # Rebuilt from scratch: a stale image left behind from a previous build is
    # the kind of thing nobody notices until it is on the internet.
    #
    # The contents go, not the folder. On Windows a directory cannot be
    # removed while any process holds it as a working directory - a terminal
    # sitting in dist/site, or a `python -m http.server` previewing the
    # build - and failing the whole build for that is absurd when emptying it
    # does the same job.
    if os.path.isdir(out):
        for name in os.listdir(out):
            path = os.path.join(out, name)
            if os.path.isdir(path) and not os.path.islink(path):
                shutil.rmtree(path)
            else:
                os.remove(path)
    os.makedirs(os.path.join(out, "images"), exist_ok=True)

    html = open(src_html, encoding="utf-8").read()

    base = args.base_url.rstrip("/")
    if base:
        html = re.sub(r'(<meta property="og:image" content=")(images/)',
                      rf'\1{base}/\2', html)
        html = html.replace('<meta property="og:type" content="website">',
                            f'<meta property="og:url" content="{base}/">\n'
                            f'<link rel="canonical" href="{base}/">\n'
                            '<meta property="og:type" content="website">')

    with open(os.path.join(out, "index.html"), "w", encoding="utf-8", newline="\n") as f:
        f.write(html)

    # Only the images the page actually asks for. docs/images may accumulate
    # others, and an upload set should carry nothing it does not use.
    wanted = sorted(set(re.findall(r'(?:src|href)="images/([^"]+)"', html)))
    total = 0
    missing = []
    for name in wanted:
        s = os.path.join(src_img, name)
        if not os.path.isfile(s):
            missing.append(name)
            continue
        shutil.copy2(s, os.path.join(out, "images", name))
        total += os.path.getsize(s)

    # The .ico browsers still request by name whatever the page links.
    ico = os.path.join(ROOT, "studio", "resources", "terraforge.ico")
    if os.path.isfile(ico):
        shutil.copy2(ico, os.path.join(out, "favicon.ico"))
        total += os.path.getsize(ico)

    # GitHub Pages runs everything through Jekyll otherwise, which quietly
    # drops files and folders beginning with an underscore.
    open(os.path.join(out, ".nojekyll"), "w").close()

    readme = f"""TerraForge website - ready to upload
====================================

Upload the *contents* of this folder to the root of your web space, keeping
the images/ subfolder alongside index.html:

    index.html
    favicon.ico
    images/...

That is the whole site. It is static: no build step, no server code, no
database, nothing to configure. Any host will serve it, including GitHub
Pages, Netlify, or plain shared hosting over FTP.

{"Open Graph tags point at " + base if base else
 "Open Graph tags are relative. For link previews on social networks to work,"
 " rebuild with:  python scripts/make_site.py --base-url https://your-domain"}

Rebuild after changing docs/index.html or the images:

    python scripts/make_site.py --base-url https://your-domain

Geekatplay Studio - Vladimir Shopine
"""
    with open(os.path.join(out, "READ ME - how to upload.txt"), "w",
              encoding="utf-8", newline="\r\n") as f:
        f.write(readme)

    print(f"site built: {out}")
    print(f"  index.html + {len(wanted)} images  ({total / 1024 / 1024:.1f} MB)")
    if base:
        print(f"  Open Graph tags absolute against {base}")
    else:
        print("  Open Graph tags left relative (pass --base-url for social cards)")
    if missing:
        print("  MISSING, referenced by the page but not in docs/images:",
              file=sys.stderr)
        for m in missing:
            print(f"    {m}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
