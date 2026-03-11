#!/usr/bin/env python3
import os, sys, json, shutil, textwrap, re
from bs4 import BeautifulSoup, NavigableString, Tag

WITH_IMAGES = "--images" in sys.argv
term_width = 100
for arg in sys.argv:
    if arg.startswith("--width="):
        try: term_width = int(arg.split("=")[1])
        except: pass
        break
else:
    try: term_width = min(max(os.get_terminal_size().columns - 2, 40), 200)
    except: pass

HTML_FILE   = "datas/cache/temp.html"
OUTPUT_FILE = "datas/cache/page.json"
IMAGE_DIR   = "datas/images"

IGNORE_TAGS = {"script","style","noscript","meta","link","head","button",
               "input","select","option","textarea","iframe","svg","canvas",
               "template","[document]","nav","aside","footer"}
BLOCK_TAGS  = {"p","div","section","article","main","blockquote","pre",
               "figure","figcaption","li","dt","dd",
               "h1","h2","h3","h4","h5","h6","hr","br",
               "ul","ol","dl","table","tr","header"}
INLINE_TAGS = {"span","em","strong","b","i","u","small","sup","sub",
               "cite","abbr","time","mark","s","del","ins","code","kbd",
               "label","font","tt","var","q","samp"}

def heading_prefix(name):
    if name == "h1": return "##H1 "
    if name in ("h2","h3"): return "##H2 "
    if name in ("h4","h5","h6"): return "##H3 "
    return None

def collect_inline_text(node):
    if isinstance(node, NavigableString):
        t = str(node)
        # Ignorer le HTML brut non parsé
        if t.strip().startswith('<'): return ""
        return t
    if not isinstance(node, Tag): return ""
    if node.name in IGNORE_TAGS: return ""
    if node.name == "img": return ""  # ne jamais inclure le src/alt d'une img comme texte
    return "".join(collect_inline_text(c) for c in node.children)

class Renderer:
    def __init__(self, w):
        self.w             = w
        self.lines         = []
        self.links         = []
        IMG_EXTS = (".jpg",".jpeg",".png",".gif",".webp",".svg",".avif",".jxl")
        self.imgs          = sorted(
            f for f in os.listdir(IMAGE_DIR)
            if any(f.endswith(e) for e in IMG_EXTS)
        ) if os.path.exists(IMAGE_DIR) else []
        self.img_i         = 0
        self._inline_buf   = []
        self._pending_links = []
        # Mapping src_url → fichier local (généré par GET.py)
        self.imgmap = {}
        imgmap_path = "datas/cache/imgmap.json"
        if os.path.exists(imgmap_path):
            try:
                with open(imgmap_path, "r", encoding="utf-8") as f:
                    self.imgmap = json.load(f)
            except: pass

    def _find_image(self, src):
        """Trouve le fichier image téléchargé correspondant au src HTML."""
        if not self.imgs:
            return None
        # 1. Chercher dans imgmap par URL exacte
        if src and src in self.imgmap:
            fname = self.imgmap[src]
            path  = os.path.join(IMAGE_DIR, fname)
            if os.path.exists(path):
                return path
        # 2. Chercher dans imgmap par URL avec/sans scheme
        if src:
            for key, fname in self.imgmap.items():
                if key.endswith(src) or src.endswith(key.split("//", 1)[-1]):
                    path = os.path.join(IMAGE_DIR, fname)
                    if os.path.exists(path):
                        return path
        # 3. Fallback : ordre d'apparition (ancien comportement)
        if self.img_i < len(self.imgs):
            path = os.path.join(IMAGE_DIR, self.imgs[self.img_i])
            self.img_i += 1
            return path
        return None

    def _flush(self, prefix="  "):
        text = " ".join("".join(self._inline_buf).split())
        self._inline_buf = []
        if text:
            first = len(self.lines)
            self.wrap(prefix, text)
            for lnk in self._pending_links:
                self.links.append({"text": lnk["text"], "url": lnk["url"], "line": first})
        self._pending_links = []

    def blank(self):
        self._flush()
        if self.lines and self.lines[-1] != "":
            self.lines.append("")

    def wrap(self, prefix, text):
        text = " ".join(text.split())
        if not text: return
        for i, l in enumerate(textwrap.wrap(text, max(self.w - len(prefix), 10))):
            self.lines.append((prefix if i == 0 else " " * len(prefix)) + l)

    def process(self, node, in_block=False):
        if isinstance(node, NavigableString):
            t = str(node)
            ts = t.strip()
            # Ignorer les NavigableString qui contiennent du HTML brut non parsé
            if ts and not ts.startswith('<') and not ts.startswith('&lt;'):
                self._inline_buf.append(t)
            return
        if not isinstance(node, Tag): return
        n = node.name
        if not n or n in IGNORE_TAGS: return

        pfx = heading_prefix(n)
        if pfx:
            self.blank()
            t = " ".join(collect_inline_text(node).split())
            if t: self.wrap(pfx, t)
            self.blank()
            return

        if n == "hr":
            self.blank(); self.lines += ["##HR", ""]; return
        if n == "br":
            self._flush(); self.lines.append(""); return

        if n == "a":
            href = (node.get("href") or "").strip()
            text = " ".join(collect_inline_text(node).split())
            # Si le <a> contient des images, les traiter d'abord
            has_img = node.find("img") is not None
            if has_img:
                for c in node.children:
                    self.process(c, in_block=in_block)
                return
            if not text: return
            if href and href not in ("#", "javascript:void(0)", "javascript:;", ""):
                self._inline_buf.append(text)
                self._pending_links.append({"text": text, "url": href})
            else:
                self._inline_buf.append(text)
            return

        if n == "img":
            self._flush()
            alt  = node.get("alt", "").strip()
            src  = node.get("src", "") or node.get("data-src", "") or ""
            if not src or src.startswith("data:"): return
            # Ignorer icônes/boutons (width ou height < 50px)
            try:
                if int(node.get("width",999)) < 50 or int(node.get("height",999)) < 50: return
            except: pass
            path = self._find_image(src)
            if path:
                label = alt or os.path.basename(path)
                # Récupérer les dimensions originales si disponibles
                # Dimensions : attributs HTML d'abord, sinon lire le fichier image
                try:    ow = int(node.get("width",  0))
                except: ow = 0
                try:    oh = int(node.get("height", 0))
                except: oh = 0
                if (ow == 0 or oh == 0) and path:
                    try:
                        with open(path, "rb") as _f: _hdr = _f.read(32)
                        # JPEG : chercher SOF marker
                        if _hdr[:2] == b'\xff\xd8':
                            import struct as _s
                            with open(path,"rb") as _f2:
                                _f2.read(2)
                                while True:
                                    _m = _f2.read(4)
                                    if len(_m) < 4: break
                                    _mk, _ln = _m[0:2], _s.unpack(">H",_m[2:4])[0]
                                    if _mk[0]==0xff and _mk[1] in (0xC0,0xC2):
                                        _d = _f2.read(5)
                                        oh,ow = _s.unpack(">HH",_d[1:5])
                                        break
                                    _f2.read(_ln-2)
                        # PNG : dims à offset 16
                        elif _hdr[:8] == b'\x89PNG\r\n\x1a\n':
                            import struct as _s
                            ow,oh = _s.unpack(">II", _hdr[16:24])
                        # WEBP : dans le chunk VP8
                        elif _hdr[:4] == b'RIFF' and _hdr[8:12] == b'WEBP':
                            import struct as _s
                            if _hdr[12:16] == b'VP8 ':
                                _d = _hdr[26:30]
                                ow = _s.unpack("<H",_d[0:2])[0] & 0x3fff
                                oh = _s.unpack("<H",_d[2:4])[0] & 0x3fff
                    except: pass
                self.blank()
                li = len(self.lines)
                # Format : ##IM path|label|orig_w|orig_h
                self.lines.append(f"##IM {path}|{label[:60]}|{ow}|{oh}")
                self.links.append({
                    "text": f"[IMG] {label[:50]}",
                    "url":  f"file://__img__{path}",
                    "line": li
                })
                self.blank()
            # Image absente : ne rien afficher
            return

        if n in ("video", "source"):
            self._flush()
            self.blank()
            vfiles = sorted(
                f for f in os.listdir("datas/videos")
                if f.endswith((".mp4", ".webm"))
            ) if os.path.exists("datas/videos") else []
            if vfiles:
                vp = os.path.join("datas/videos", vfiles[0])
                li = len(self.lines)
                self.lines.append("##VD ▶  Vidéo — Entrée pour lire")
                self.links.append({"text": "▶ Lire la vidéo",
                                   "url": f"file://__video__{vp}", "line": li})
            else:
                src = node.get("src", "")
                self.lines.append(f"##VD ▶  {src[:80]}")
            self.blank()
            if n == "video": return

        if n in INLINE_TAGS:
            for c in node.children:
                self.process(c, in_block=in_block)
            return

        if n in ("ul", "ol"):
            self.blank()
            ctr = 1
            for c in node.children:
                if isinstance(c, Tag) and c.name == "li":
                    t = " ".join(collect_inline_text(c).split())
                    if t:
                        self.wrap(f"  {ctr}. " if n == "ol" else "  • ", t)
                        ctr += 1
            self.blank()
            return

        if n == "table":
            self._flush()
            self.blank()
            # Traiter chaque cellule avec process() pour capturer images et texte
            for row in node.find_all("tr"):
                for cell in row.find_all(["td", "th"]):
                    for c in cell.children:
                        self.process(c, in_block=True)
                self._flush()
            self.blank()
            return

        if n == "p":
            self._flush()
            self.blank()
            for c in node.children:
                self.process(c, in_block=True)
            self._flush()
            self.blank()
            return

        if n in BLOCK_TAGS:
            self._flush()
            for c in node.children:
                self.process(c, in_block=True)
            self._flush()
            if n in ("div", "section", "article", "blockquote", "figure"):
                self.blank()
            return

        for c in node.children:
            self.process(c, in_block=in_block)


def main():
    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    if not os.path.exists(HTML_FILE):
        data = {"lines": ["  [Erreur: temp.html introuvable]"], "links": []}
        with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
            json.dump(data, f)
        return

    with open(HTML_FILE, "r", encoding="utf-8", errors="replace") as f:
        html = f.read()

    soup = BeautifulSoup(html, "html.parser")
    # Supprimer scripts, styles, commentaires HTML (NewPP, GTM, etc.)
    from bs4 import Comment
    for tag in soup(["script", "style", "noscript"]):
        tag.decompose()
    for comment in soup.find_all(string=lambda t: isinstance(t, Comment)):
        comment.extract()

    r = Renderer(term_width)
    title_tag = soup.find("title")
    if title_tag:
        r.lines.append(f"##H1 {title_tag.get_text().strip()}")
        r.lines += ["##HR", ""]

    body = soup.find("main") or soup.find("article") or soup.find("body") or soup
    # Si le conteneur choisi ne contient aucune img, utiliser body
    if body.name != "body" and not body.find("img"):
        body = soup.find("body") or soup
    r.process(body)
    r._flush()

    out, prev_blank = [], False
    for l in r.lines:
        b = not l.strip()
        if b and prev_blank: continue
        out.append(l)
        prev_blank = b

    data = {"lines": out, "links": r.links}
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

    print(f"[render] {len(out)} lignes, {len(r.links)} liens")


if __name__ == "__main__":
    main()