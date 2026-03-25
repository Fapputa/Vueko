#!/usr/bin/env bash
# ─── install.sh — Installation complète de Vueko ─────────────────────────────
set -e

VUEKO_DIR="$(cd "$(dirname "$0")" && pwd)"

# Toujours utiliser le HOME du vrai utilisateur, même si lancé avec sudo
if [ -n "$SUDO_USER" ]; then
    REAL_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
else
    REAL_HOME="$HOME"
fi

BIN_DIR="$REAL_HOME/bin"

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
        # ffmpeg : ne pas installer si une variante est déjà présente
        # (ffmpeg4.4 entre en conflit avec libvpx du ffmpeg standard)
        FFMPEG_PKGS=""
        if ! command -v ffmpeg &>/dev/null; then
            FFMPEG_PKGS="ffmpeg"
        else
            echo "  → ffmpeg déjà disponible ($(command -v ffmpeg)), installation ignorée"
        fi

        pacman -S --needed --noconfirm \
            gcc make \
            ncurses \
            json-c \
            chafa \
            $FFMPEG_PKGS \
            mpg123 \
            python \
            python-pip
        ;;
    apt)
        apt-get update -qq
        apt-get install -y \
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
        dnf install -y \
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
        zypper install -y \
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

# Toujours installer en tant que l'utilisateur réel, pas root,
# pour que les packages et le cache Playwright soient dans son HOME
RUN_AS_USER() {
    if [ -n "$SUDO_USER" ]; then
        sudo -u "$SUDO_USER" env HOME="$REAL_HOME" "$@"
    else
        "$@"
    fi
}

RUN_AS_USER python3 -m pip install --break-system-packages \
    playwright \
    requests \
    beautifulsoup4 \
    playwright-stealth

# ─────────────────────────────────────────────────────────────────────────────
# 4. CHROMIUM POUR PLAYWRIGHT
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo "==> Installation de Chromium via Playwright..."
# Idem : le cache ms-playwright doit atterrir dans ~/.cache de l'utilisateur réel
RUN_AS_USER python3 -m playwright install chromium

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
# 7. INSTALLATION DANS ~/bin
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

# Corriger la propriété du dossier ~/bin si on est en sudo
if [ -n "$SUDO_USER" ]; then
    chown -R "$SUDO_USER:$SUDO_USER" "$BIN_DIR"
fi

# ─────────────────────────────────────────────────────────────────────────────
# 8. AJOUTER ~/bin AU PATH
# ─────────────────────────────────────────────────────────────────────────────
add_to_rc() {
    local RC="$1"
    local LINE='export PATH="$HOME/bin:$PATH"'
    if [ -f "$RC" ] && ! grep -qE '(^|:)\$HOME/bin(:|"|$)' "$RC" 2>/dev/null; then
        printf '\n# Vueko\n%s\n' "$LINE" >> "$RC"
        echo "  → PATH ajouté à $RC"
    fi
}

USER_SHELL="$(getent passwd "$SUDO_USER" | cut -d: -f7)"
case "$USER_SHELL" in
    */zsh)  add_to_rc "$REAL_HOME/.zshrc"  ;;
    */fish)
        FISH_RC="$REAL_HOME/.config/fish/config.fish"
        mkdir -p "$(dirname "$FISH_RC")"
        if ! grep -qE '\$HOME/bin' "$FISH_RC" 2>/dev/null; then
            echo 'fish_add_path $HOME/bin' >> "$FISH_RC"
            echo "  → PATH ajouté à $FISH_RC"
        fi
        ;;
    *)      add_to_rc "$REAL_HOME/.bashrc" ;;
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
echo "    export PATH=\"\$HOME/bin:\$PATH\""
echo "  puis :"
echo "    vueko"
echo ""