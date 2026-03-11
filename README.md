# Vueko

## Description

Navigateur web dans le terminal — avec rendu HTML, images via `chafa`, lecture vidéo, et UI ncurses complète.

---

## Prérequis

### Paquets système

**Arch / Manjaro**
```sh
pacman -S chafa ffmpeg json-c ncurses
```

**Debian / Ubuntu**
```sh
apt install chafa ffmpeg libjson-c-dev libncurses-dev
```

### Librairies Python
```sh
pip install playwright requests filetype beautifulsoup4
playwright install chromium
```
Ou sur certains systèmes:
```sh
python3 -m pip install playwright requests filetype beautifulsoup4
python3 -m playwright install chromium
```

---

## Compilation & lancement

```sh
make all        # compile browser, convert, play
make dirs       # crée datas/cache, datas/images, datas/videos
./browser       # lance le navigateur
```

---

## Architecture

```
vueko/
│
├── browser.c       — UI ncurses principale (navigation, recherche, affichage page)
├── render.py       — Convertit temp.html → page.json (texte + liens + positions images)
├── GET.py          — Télécharge une page (HTML + images + vidéos)
├── search.py       — Recherche Bing → search_results.json
├── convert.c       — Convertit MP4 → GIF + MP3 (pour play.c)
├── play.c          — Joue GIF + MP3 simultanément dans le terminal
├── help.c          — Aide (--help)
├── main.c          — Point d'entrée CLI (--help, etc.)
├── Makefile
│
└── datas/
    ├── cache/
    │   ├── search_results.json   — Résultats de recherche
    │   ├── temp.html             — HTML de la page courante
    │   ├── page.json             — Page rendue (lignes + liens)
    │   └── templink.csv          — Liens de la page courante
    ├── images/                   — Images téléchargées (img0.jpg, ...)
    └── videos/                   — Vidéos téléchargées (vid0.mp4, ...)
```

---

## Format page.json (render.py → browser.c)

```json
{
  "lines": [
    "##H1 Titre principal",
    "##H2 Sous-titre",
    "##HR",
    "  Texte normal wrappé...",
    "##LK Texte du lien",
    "##VD ▶  Vidéo disponible",
    "  ┌── Image: photo.jpg ──",
    "  <lignes ANSI chafa>",
    "  └────────────────"
  ],
  "links": [
    { "text": "Texte du lien", "url": "https://...", "line": 42 }
  ]
}
```

### Préfixes de contrôle (browser.c)
| Préfixe | Rendu            |
|---------|------------------|
| `##H1 ` | H1 gras + souligné |
| `##H2 ` | H2 gras           |
| `##H3 ` | H3 gras           |
| `##LK ` | Lien cliquable (cyan souligné) |
| `##VD ` | Badge vidéo (magenta) |
| `##HR`  | Ligne horizontale |
| *(rien)*| Texte normal      |

---

## Raccourcis clavier

| Touche      | Action                              |
|-------------|-------------------------------------|
| `Ctrl+R`    | Nouvelle recherche                  |
| `↑` / `↓`  | Navigation résultats / scroll page  |
| `PgUp/Dn`  | Défilement rapide (×10)             |
| `Entrée`    | Ouvrir le résultat / suivre un lien |
| `Tab`       | Ouvrir/fermer le panneau des liens  |
| `B`         | Retour à la liste des résultats     |
| `Q` / `ESC` | Quitter                            |

---

## Lecture vidéo

Quand une page contient une vidéo :
1. `GET.py` télécharge le `.mp4` dans `datas/videos/`
2. `render.py` insère un badge `##VD` et un lien spécial `file://__video__path`
3. `browser.c` détecte ce lien et lance `convert` + `play` automatiquement
4. La vidéo joue en GIF animé (chafa) + audio (mpg123) simultanément
