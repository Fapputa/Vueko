#!/usr/bin/env bash
# ─── install.sh — Installation complète de Vueko ─────────────────────────────
set -e

VUEKO_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$HOME/.local/bin"

echo "╔══════════════════════════════════════════╗"
echo "║         Installation de Vueko            ║"
echo "╚══════════════════════════════════════════╝"
echo ""
echo "  Projet : $VUEKO_DIR"
echo "  Cible  : $BIN_DIR"
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# 1. DÉTECTION DISTRO
# ─────────────────────────────────────────────────────────────────────────────
detect_pm() {
    if   command -v pacman  &>/dev/null; then echo "pacman"
    elif command -v apt-get &>/dev/null; then echo "apt"
    elif command -v dnf     &>/dev/null; then echo "dnf"
    elif command -v zypper  &>/dev/null; then echo "zypper"
    else echo "unknown"
    fi
}

PM=$(detect_pm)
echo "==> Gestionnaire de paquets détecté : $PM"

# ─────────────────────────────────────────────────────────────────────────────
# 2. DÉPENDANCES SYSTÈME
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo "==> Installation des dépendances système..."

case "$PM" in
    pacman)
        sudo pacman -S --needed --noconfirm \
            gcc make \
            ncurses \
            json-c \
            chafa \
            ffmpeg \
            mpg123 \
            python \
            python-pip
        ;;
    apt)
        sudo apt-get update -qq
        sudo apt-get install -y \
            gcc make \
            libncurses-dev libncurses6 \
            libjson-c-dev libjson-c5 \
            chafa \
            ffmpeg \
            mpg123 \
            python3 \
            python3-pip
        ;;
    dnf)
        sudo dnf install -y \
            gcc make \
            ncurses-devel \
            json-c-devel \
            chafa \
            ffmpeg \
            mpg123 \
            python3 \
            python3-pip
        ;;
    zypper)
        sudo zypper install -y \
            gcc make \
            ncurses-devel \
            libjson-c-devel \
            chafa \
            ffmpeg \
            mpg123 \
            python3 \
            python3-pip
        ;;
    *)
        echo "  ⚠ Gestionnaire inconnu — installe manuellement :"
        echo "    gcc, make, libncurses-dev, libjson-c-dev,"
        echo "    chafa, ffmpeg, mpg123, python3, python3-pip"
        ;;
esac

# ─────────────────────────────────────────────────────────────────────────────
# 3. DÉPENDANCES PYTHON
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo "==> Installation des dépendances Python..."

PIP="pip"
command -v pip  &>/dev/null || PIP="pip3"
command -v pip3 &>/dev/null || PIP="python3 -m pip"

$PIP install --break-system-packages \
    playwright \
    requests \
    beautifulsoup4 \
    playwright-stealth

# ─────────────────────────────────────────────────────────────────────────────
# 4. CHROMIUM POUR PLAYWRIGHT
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo "==> Installation de Chromium via Playwright..."
python3 -m playwright install chromium

# ─────────────────────────────────────────────────────────────────────────────
# 5. COMPILATION
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo "==> Compilation de Vueko..."
cd "$VUEKO_DIR"
make all

# S'assurer que le binaire s'appelle vueko
[ -f "$VUEKO_DIR/browser" ] && mv "$VUEKO_DIR/browser" "$VUEKO_DIR/vueko"

# ─────────────────────────────────────────────────────────────────────────────
# 6. DOSSIERS DATAS
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo "==> Création des dossiers datas/..."
mkdir -p "$VUEKO_DIR/datas/cache" \
         "$VUEKO_DIR/datas/images" \
         "$VUEKO_DIR/datas/videos"

# ─────────────────────────────────────────────────────────────────────────────
# 7. INSTALLATION DANS ~/.local/bin
#    base_dir dans browser.c = dossier du binaire via readlink
#    → tous les fichiers nécessaires doivent être au même endroit
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo "==> Copie dans $BIN_DIR..."
mkdir -p "$BIN_DIR"

# Binaires compilés
cp "$VUEKO_DIR/vueko"   "$BIN_DIR/vueko"
cp "$VUEKO_DIR/convert" "$BIN_DIR/convert"
cp "$VUEKO_DIR/play"    "$BIN_DIR/play"
chmod +x "$BIN_DIR/vueko" "$BIN_DIR/convert" "$BIN_DIR/play"

# Scripts Python
cp "$VUEKO_DIR/GET.py"    "$BIN_DIR/GET.py"
cp "$VUEKO_DIR/render.py" "$BIN_DIR/render.py"
cp "$VUEKO_DIR/search.py" "$BIN_DIR/search.py"

# Dossier datas : symlink pour ne pas dupliquer les fichiers téléchargés
if [ ! -e "$BIN_DIR/datas" ]; then
    ln -s "$VUEKO_DIR/datas" "$BIN_DIR/datas"
    echo "  → Lien symbolique : $BIN_DIR/datas → $VUEKO_DIR/datas"
fi

# ─────────────────────────────────────────────────────────────────────────────
# 8. AJOUTER ~/.local/bin AU PATH
# ─────────────────────────────────────────────────────────────────────────────
add_to_rc() {
    local RC="$1"
    local LINE='export PATH="$HOME/.local/bin:$PATH"'
    if [ -f "$RC" ] && ! grep -q '\.local/bin' "$RC" 2>/dev/null; then
        printf '\n# Vueko\n%s\n' "$LINE" >> "$RC"
        echo "  → PATH ajouté à $RC"
    fi
}

case "$SHELL" in
    */zsh)  add_to_rc "$HOME/.zshrc"  ;;
    */fish)
        FISH_RC="$HOME/.config/fish/config.fish"
        mkdir -p "$(dirname "$FISH_RC")"
        if ! grep -q '\.local/bin' "$FISH_RC" 2>/dev/null; then
            echo 'fish_add_path $HOME/.local/bin' >> "$FISH_RC"
            echo "  → PATH ajouté à $FISH_RC"
        fi
        ;;
    *)      add_to_rc "$HOME/.bashrc" ;;
esac

# ─────────────────────────────────────────────────────────────────────────────
# 9. RÉSUMÉ
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════╗"
echo "║         Installation terminée ✓          ║"
echo "╚══════════════════════════════════════════╝"
echo ""
echo "  Binaire  : $BIN_DIR/vueko"
echo "  Données  : $VUEKO_DIR/datas/"
echo ""
echo "  Ouvre un nouveau terminal ou tape :"
echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
echo "  puis :"
echo "    vueko"
echo ""