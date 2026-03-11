#!/usr/bin/env python3
"""
GET.py — Sans dépendance filetype, détection par magic bytes
Images en parallèle avec requests
"""
import sys, os, shutil, threading
from urllib.parse import urljoin
from playwright.sync_api import sync_playwright
try:
    from playwright_stealth import stealth_sync as _stealth
except ImportError:
    _stealth = None
import requests

if len(sys.argv) < 2:
    sys.exit(1)

URL = sys.argv[1]
BLOCKED_ADS   = ["doubleclick.net","google-analytics.com","googlesyndication.com",
                  "adnxs.com","amazon-adsystem.com","scorecardresearch.com"]
BLOCKED_TYPES = {"font"}  # scripts autorisés pour lazy loading
IMAGE_DIR = "datas/images"
VIDEO_DIR = "datas/videos"
UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36")

os.makedirs("datas/cache", exist_ok=True)

# ── Détection par magic bytes (sans filetype) ────────────────────────────────
MAGIC = [
    (b'\xff\xd8\xff',           ".jpg"),
    (b'\x89PNG\r\n',            ".png"),
    (b'GIF87a',                 ".gif"),
    (b'GIF89a',                 ".gif"),
    (b'RIFF',                   ".webp"),  # RIFF????WEBP
    (b'\x00\x00\x00\x1cftyp',  ".avif"),  # AVIF/HEIF
    (b'\x00\x00\x00\x18ftyp',  ".avif"),
    (b'\x00\x00\x00\x20ftyp',  ".avif"),
    (b'\xff\x0a',               ".jxl"),   # JPEG-XL
    (b'\x00\x00\x00\x0cJXL ',  ".jxl"),
    (b'<svg',                   ".svg"),
    (b'\x00\x00\x00',           ".mp4"),   # ftyp boxes
    (b'\x1aE\xdf\xa3',         ".webm"),
]
def guess_ext_magic(data: bytes, content_type: str = "") -> str:
    if data:
        for magic, ext in MAGIC:
            if data[:len(magic)] == magic:
                # Cas WEBP : vtyp RIFF + WEBP à offset 8
                if ext == ".webp" and len(data) > 12 and data[8:12] != b'WEBP':
                    return ".bin"
                return ext
    ct = content_type.lower()
    for m, e in [("jpeg",".jpg"),("jpg",".jpg"),("png",".png"),("gif",".gif"),
                 ("webp",".webp"),("svg",".svg"),("mp4",".mp4"),("webm",".webm")]:
        if m in ct: return e
    return ".bin"

def is_image(ext): return ext in (".jpg",".jpeg",".png",".gif",".webp",".svg",".avif",".jxl")
def is_video(ext): return ext in (".mp4",".webm")

def block_ads(route):
    for b in BLOCKED_ADS:
        if b in route.request.url: route.abort(); return
    if route.request.resource_type in BLOCKED_TYPES: route.abort()
    else: route.continue_()

image_urls, video_urls, link_urls = [], [], []

# ── Phase 1 : Playwright ─────────────────────────────────────────────────────
with sync_playwright() as p:
    browser = p.chromium.launch(
        headless=True,
        args=[
            "--disable-blink-features=AutomationControlled",
            "--no-sandbox",
            "--disable-web-security",
        ]
    )
    ctx = browser.new_context(
        user_agent=UA,
        locale="fr-FR",
        viewport={"width": 1280, "height": 800},
        extra_http_headers={"Accept-Language": "fr-FR,fr;q=0.9,en;q=0.8"}
    )
    ctx.add_init_script("Object.defineProperty(navigator,'webdriver',{get:()=>undefined})")
    page = ctx.new_page()
    if _stealth: _stealth(page)
    page.route("**/*", block_ads)
    try:
        page.goto(URL, wait_until="networkidle", timeout=30000)
        # Scroll progressif pour déclencher lazy loading et galeries JS
        page.evaluate("window.scrollTo(0, document.body.scrollHeight/4)")
        page.wait_for_timeout(1000)
        page.evaluate("window.scrollTo(0, document.body.scrollHeight/2)")
        page.wait_for_timeout(1000)
        page.evaluate("window.scrollTo(0, document.body.scrollHeight*3/4)")
        page.wait_for_timeout(1000)
        page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
        page.wait_for_timeout(2000)
        # Remonter en haut pour que les images above-the-fold soient aussi chargées
        page.evaluate("window.scrollTo(0, 0)")
        page.wait_for_timeout(1500)
        # Collecter aussi les images dans figure/lightbox (Fandom galleries)
        page.evaluate("""() => {
            document.querySelectorAll('figure[data-image-key], .gallery-image-wrapper img, .lightbox img, [class*="gallery"] img').forEach(el => {
                if (el.dataset && el.dataset.imageName) {
                    el.src = el.dataset.imageName;
                }
            });
        }""")
        with open("datas/cache/temp.html","w",encoding="utf-8") as f:
            f.write(page.content())
    except Exception as e:
        try:
            with open("datas/cache/temp.html","w",encoding="utf-8") as f:
                f.write(page.content())
        except: pass

    for a in page.query_selector_all("a"):
        href = a.get_attribute("href")
        if href: link_urls.append(urljoin(URL, href))

    # Collecter via attributs HTML + currentSrc JS
    seen = set()
    for img in page.query_selector_all("img"):
        # Collecter TOUS les attributs src possibles (pas juste le premier trouvé)
        for attr in ("src","data-src","data-lazy-src","data-original","data-url",
                     "data-delayed-url","data-actual","data-canonical-src"):
            src = img.get_attribute(attr)
            if src and not src.startswith("data:") and src not in seen:
                seen.add(src)
                image_urls.append(urljoin(URL, src))
        ss = img.get_attribute("srcset") or img.get_attribute("data-srcset") or ""
        if ss:
            # Prendre la plus grande image du srcset
            candidates = []
            for part in ss.split(","):
                p2 = part.strip().split()
                if p2:
                    u2 = p2[0]
                    w  = int(p2[1].rstrip("w")) if len(p2)>1 and p2[1].endswith("w") else 0
                    candidates.append((w, u2))
            if candidates:
                best = sorted(candidates)[-1][1]
                full = urljoin(URL, best)
                if full not in seen:
                    seen.add(full)
                    image_urls.append(full)

    # currentSrc via JS
    js_imgs = page.evaluate("""() => {
        const imgs = new Set();
        // Toutes les images du DOM avec tous les attributs possibles
        document.querySelectorAll('img').forEach(i => {
            for (const attr of ['src','currentSrc','data-src','data-lazy-src',
                                 'data-original','data-url','data-full','data-image-src',
                                 'data-hi-res-src','data-large-src']) {
                const s = i[attr] || i.getAttribute(attr);
                if (s && !s.startsWith('data:') && s.startsWith('http')) {
                    imgs.add(s); break;
                }
            }
            // currentSrc toujours (image réellement affichée)
            if (i.currentSrc && !i.currentSrc.startsWith('data:')) imgs.add(i.currentSrc);
        });
        // Fandom lightbox / gallery
        document.querySelectorAll('a[data-image-name], figure[data-image-key]').forEach(el => {
            const s = el.getAttribute('data-src') || el.getAttribute('data-image-src') ||
                      el.querySelector('img')?.src;
            if (s && !s.startsWith('data:')) imgs.add(s);
        });
        // CSS background-image sur tous les éléments visibles
        document.querySelectorAll('*').forEach(el => {
            const bg = window.getComputedStyle(el).backgroundImage;
            if (bg && bg !== 'none') {
                const m = bg.match(/url[(]["']?([^"')]+)["']?[)]/);
                if (m && m[1] && !m[1].startsWith('data:')) imgs.add(m[1]);
            }
        });
        return [...imgs];
    }""")
    for src in js_imgs:
        if src not in seen:
            seen.add(src)
            image_urls.append(src)

    # Vidéos
    for v in page.query_selector_all("video, source[src]"):
        src = v.get_attribute("src")
        if src and not src.startswith("data:"): video_urls.append(urljoin(URL,src))

    ctx.close()
    browser.close()

print(f"[GET] Page OK — {len(image_urls)} images, {len(video_urls)} vidéos")

# ── Phase 2 : images en parallèle ────────────────────────────────────────────
if os.path.exists(IMAGE_DIR): shutil.rmtree(IMAGE_DIR)
os.makedirs(IMAGE_DIR, exist_ok=True)

HEADERS = {"User-Agent": UA, "Referer": URL,
           "Accept": "image/avif,image/webp,image/apng,image/*,*/*;q=0.8"}
lock  = threading.Lock()
nimg  = [0]
imgmap = {}

def download_image(args):
    idx, url = args
    try:
        r = requests.get(url, headers=HEADERS, timeout=8)
        if r.status_code >= 400 or len(r.content) < 64: return
        ct  = r.headers.get("content-type", "")
        ext = guess_ext_magic(r.content, ct)
        # Si magic bytes inconnus mais Content-Type est clairement une image → forcer .jpg
        if ext == ".bin" and "image/" in ct:
            ext = ".jpg"
        if not is_image(ext): return
        with lock:
            n = nimg[0]; nimg[0] += 1
        # Nommer avec index d'origine pour conserver l'ordre de la page
        fname = f"img{idx:03d}{ext}"
        with open(os.path.join(IMAGE_DIR, fname), "wb") as f:
            f.write(r.content)
        with lock:
            imgmap[url] = fname
    except: pass

threads = [threading.Thread(target=download_image, args=((i,u),))
           for i,u in enumerate(image_urls[:30])]
for t in threads: t.start()
for t in threads: t.join(timeout=12)
print(f"[GET] {nimg[0]} images téléchargées")
import json as _j
with open("datas/cache/imgmap.json","w") as f: _j.dump(imgmap,f)

# ── Phase 3 : vidéos ─────────────────────────────────────────────────────────
if os.path.exists(VIDEO_DIR): shutil.rmtree(VIDEO_DIR)
os.makedirs(VIDEO_DIR, exist_ok=True)
nvid = 0
for url in video_urls[:3]:
    try:
        r = requests.get(url, headers={"User-Agent":UA,"Referer":URL},
                         timeout=20, stream=True)
        if r.status_code >= 400: continue
        first = next(r.iter_content(16), b"")
        ext = guess_ext_magic(first, r.headers.get("content-type",""))
        if not is_video(ext): continue
        path = os.path.join(VIDEO_DIR, f"vid{nvid}{ext}")
        with open(path, "wb") as f:
            f.write(first)
            for chunk in r.iter_content(65536): f.write(chunk)
        nvid += 1
    except: pass

# ── Liens ─────────────────────────────────────────────────────────────────────
with open("datas/cache/templink.csv","w",encoding="utf-8") as f:
    for l in link_urls: f.write(l+"\n")
print("[GET] Terminé.")