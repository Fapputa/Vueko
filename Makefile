# Vueko — Makefile

CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LIBS    = -lncursesw -ljson-c

# Binaires
BROWSER = vueko
CONVERT = convert
PLAY    = play

.PHONY: all clean install deps dirs vueko

all: $(BROWSER) $(CONVERT) $(PLAY)

$(BROWSER): browser.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(CONVERT): convert.c
	$(CC) $(CFLAGS) -o $@ $<

$(PLAY): play.c
	$(CC) $(CFLAGS) -o $@ $<

# Alias explicite
vueko: $(BROWSER)

# Crée les dossiers nécessaires
dirs:
	mkdir -p datas/cache datas/images datas/videos

# Dépendances Python
deps:
	pip install playwright requests beautifulsoup4 --break-system-packages || \
	python3 -m pip install playwright requests beautifulsoup4
	playwright install chromium || python3 -m playwright install chromium

# Installation dans ~/.local/bin (tout le projet)
install: all dirs
	mkdir -p $(HOME)/.local/bin
	cp $(BROWSER) $(CONVERT) $(PLAY) $(HOME)/.local/bin/
	cp GET.py render.py search.py $(HOME)/.local/bin/
	mkdir -p $(HOME)/.local/bin/datas/cache \
	         $(HOME)/.local/bin/datas/images \
	         $(HOME)/.local/bin/datas/videos
	@echo ""
	@echo "✓ vueko installé dans $(HOME)/.local/bin/"
	@echo "  Lance : vueko"

clean:
	rm -f $(BROWSER) $(CONVERT) $(PLAY)