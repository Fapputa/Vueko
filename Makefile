# Vueko — Makefile

CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LIBS    = -lncurses -ljson-c

# Binaires
BROWSER = browser
CONVERT = convert
PLAY    = play

.PHONY: all clean install deps

all: $(BROWSER) $(CONVERT) $(PLAY)

$(BROWSER): browser.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(CONVERT): convert.c
	$(CC) $(CFLAGS) -o $@ $<

$(PLAY): play.c
	$(CC) $(CFLAGS) -o $@ $<

# Crée les dossiers nécessaires
dirs:
	mkdir -p datas/cache datas/images datas/videos

# Dépendances Python
deps:
	pip install playwright requests filetype beautifulsoup4 --break-system-packages || \
	python3 -m pip install playwright requests filetype beautifulsoup4
	playwright install chromium || python3 -m playwright install chromium

clean:
	rm -f $(BROWSER) $(CONVERT) $(PLAY)
	rm -rf datas/cache datas/images datas/videos

install: all dirs
	@echo "Vueko installé. Lance ./browser pour démarrer."
