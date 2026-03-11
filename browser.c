/*
 * Vueko - Terminal Web Browser
 * browser.c — UI ncurses principale
 *
 * Layout:
 *  [0]   TOP BAR     : titre + raccourcis
 *  [1]   ADDR BAR    : URL courante
 *  [2..H-2] CONTENT  : résultats / page rendue
 *  [H-1] STATUS BAR  : infos / pagination
 *
 * Raccourcis:
 *   Ctrl+R  : nouvelle recherche
 *   Entrée  : ouvrir le résultat sélectionné
 *   Tab     : basculer entre mode résultats / liens de la page
 *   ↑/↓     : navigation
 *   PgUp/Dn : défilement rapide
 *   B       : retour à la liste des résultats
 *   Q/ESC   : quitter
 */

#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <locale.h>

/* ─── Couleurs ───────────────────────────────────────────────────── */
#define CP_TOPBAR       1
#define CP_STATUSBAR    2
#define CP_ADDRBAR      3
#define CP_HIGHLIGHT    4
#define CP_TITLE        5
#define CP_URL          6
#define CP_SPINNER      7
#define CP_LINK_HL      8
#define CP_HEADING      9
#define CP_NORMAL      10
#define CP_VIDEO       11
#define CP_SEPARATOR   12

/* ─── Constantes ─────────────────────────────────────────────────── */
#define MAX_RESULTS     100
#define MAX_LINES       8192
#define MAX_LINKS       1024
#define MAX_LEN         2048
#define PAGE_JUMP       10

/* ─── Modes ──────────────────────────────────────────────────────── */
typedef enum { MODE_SEARCH, MODE_PAGE } AppMode;

/* ─── Structures ─────────────────────────────────────────────────── */
typedef struct {
    char title[512];
    char url[1024];
    char site[256];
} SearchResult;

typedef struct {
    char text[512];
    char url[1024];
    int  line;
} PageLink;

/* ─── État global ────────────────────────────────────────────────── */
static AppMode      mode               = MODE_SEARCH;
static char         base_dir[4096]     = ".";
static char         current_url[1024]  = "";
static char         current_title[512] = "";

static SearchResult results[MAX_RESULTS];
static int          results_count  = 0;
static int          results_hl     = 0;
static int          results_scroll = 0;

static char        *page_lines[MAX_LINES];
static int          page_lines_count = 0;
static PageLink     page_links[MAX_LINKS];
static int          page_links_count = 0;
static int          page_links_hl    = 0;
static int          page_scroll      = 0;
static int          link_positions[MAX_LINKS];

static int          links_panel_open   = 0;
static int          page_scroll_prev   = -1;  /* pour détecter changement scroll */
static pid_t        images_render_pid  = -1;
static int          img_render_width   = 0;   /* largeur utilisée pour rendre les images */

/* ─── Chargement résultats de recherche ──────────────────────────── */
static void load_search_results(const char *path) {
    results_count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    struct json_object *arr = json_tokener_parse(buf);
    free(buf);
    if (!arr || !json_object_is_type(arr, json_type_array)) {
        if (arr) json_object_put(arr);
        return;
    }

    int n = (int)json_object_array_length(arr);
    for (int i = 0; i < n && results_count < MAX_RESULTS; i++) {
        struct json_object *obj = json_object_array_get_idx(arr, i);
        struct json_object *t, *u, *s;
        const char *title = "", *url = "", *site = "";
        if (json_object_object_get_ex(obj, "title", &t)) title = json_object_get_string(t);
        if (json_object_object_get_ex(obj, "url",   &u)) url   = json_object_get_string(u);
        if (json_object_object_get_ex(obj, "site",  &s)) site  = json_object_get_string(s);
        strncpy(results[results_count].title, title, 511);
        strncpy(results[results_count].url,   url,  1023);
        strncpy(results[results_count].site,  site,  255);
        results_count++;
    }
    json_object_put(arr);
}

/* ─── Chargement page rendue ─────────────────────────────────────── */
#define IMG_CACHE_LINES 256
static char  *img_cache[IMG_CACHE_LINES];  /* lignes ANSI allouées */
static int    img_cache_count   = 0;
static int    img_cache_scroll  = -1;      /* scroll pour lequel le cache est valide */
static int    img_old_ys[256];
static int    img_old_ys_count = 0;
static int    img_real_line[256];
static int    img_real_h[256];
static int    img_real_count = 0;

static void img_cache_clear(void) {
    /* Sauvegarder les anciennes positions Y avant de vider */
    img_old_ys_count = 0;
    for (int i = 0; i < img_cache_count; i++) {
        char *e = img_cache[i];
        if (!e) continue;
        char *p = strchr(e, '|');
        if (p) {
            int y = atoi(e);
            int dup = 0;
            for (int j = 0; j < img_old_ys_count; j++) if (img_old_ys[j]==y){dup=1;break;}
            if (!dup && img_old_ys_count < IMG_CACHE_LINES) img_old_ys[img_old_ys_count++] = y;
        }
        free(img_cache[i]); img_cache[i] = NULL;
    }
    img_cache_count  = 0;
    img_cache_scroll = -1;
}


static void free_page_lines(void) {
    for (int i = 0; i < page_lines_count; i++) {
        free(page_lines[i]);
        page_lines[i] = NULL;
    }
    page_lines_count = 0;
    page_links_count = 0;
    page_links_hl    = 0;
    page_scroll      = 0;
    page_scroll_prev = -1;
    img_cache_clear();
}

static void load_rendered_page(const char *path) {
    free_page_lines();

    FILE *f = fopen(path, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    struct json_object *root = json_tokener_parse(buf);
    free(buf);
    if (!root) return;

    struct json_object *lines_arr, *links_arr;

    if (json_object_object_get_ex(root, "lines", &lines_arr)) {
        int n = (int)json_object_array_length(lines_arr);
        for (int i = 0; i < n && page_lines_count < MAX_LINES; i++) {
            struct json_object *l = json_object_array_get_idx(lines_arr, i);
            const char *raw = json_object_get_string(l);
            page_lines[page_lines_count++] = strdup(raw);
        }
    }

    if (json_object_object_get_ex(root, "links", &links_arr)) {
        int n = (int)json_object_array_length(links_arr);
        for (int i = 0; i < n && page_links_count < MAX_LINKS; i++) {
            struct json_object *lobj = json_object_array_get_idx(links_arr, i);
            struct json_object *t, *u, *ln;
            const char *text = "", *url = "";
            int line = 0;
            if (json_object_object_get_ex(lobj, "text", &t))  text = json_object_get_string(t);
            if (json_object_object_get_ex(lobj, "url",  &u))  url  = json_object_get_string(u);
            if (json_object_object_get_ex(lobj, "line", &ln)) line = json_object_get_int(ln);
            strncpy(page_links[page_links_count].text, text,  511);
            strncpy(page_links[page_links_count].url,  url,  1023);
            page_links[page_links_count].line = line;
            link_positions[page_links_count]  = line;
            page_links_count++;
        }
    }

    json_object_put(root);
}

/* ─── Barre top/status ───────────────────────────────────────────── */
static void fill_bar(WINDOW *win, int cp, const char *left,
                     const char *center, const char *right) {
    int h, w;
    getmaxyx(win, h, w);
    (void)h;
    werase(win);
    wbkgd(win, COLOR_PAIR(cp));
    wattron(win, COLOR_PAIR(cp) | A_BOLD);
    if (left)   mvwprintw(win, 0, 1, "%s", left);
    if (right)  mvwprintw(win, 0, w - (int)strlen(right) - 1, "%s", right);
    if (center) {
        int cx = (w - (int)strlen(center)) / 2;
        if (cx < 0) cx = 0;
        mvwprintw(win, 0, cx, "%s", center);
    }
    wattroff(win, A_BOLD);
    wrefresh(win);
}

/* ─── Spinner d'attente ──────────────────────────────────────────── */
static void run_spinner(WINDOW *win, const char *msg, pid_t pid) {
    static const char frames[] = "|/-\\";
    int fi = 0;
    int h, w;
    getmaxyx(win, h, w);

    nodelay(stdscr, TRUE);
    while (1) {
        int st;
        if (waitpid(pid, &st, WNOHANG) != 0) break;
        werase(win);
        wbkgd(win, COLOR_PAIR(CP_NORMAL));
        wattron(win, COLOR_PAIR(CP_SPINNER) | A_BOLD);
        mvwprintw(win, h / 2, (w - (int)strlen(msg) - 4) / 2,
                  " %c  %s ", frames[fi++ % 4], msg);
        wattroff(win, A_BOLD);
        wrefresh(win);
        napms(80);
    }
    nodelay(stdscr, FALSE);
}

/* ─── Popup saisie ───────────────────────────────────────────────── */
static void popup_input(const char *label, char *buf, int bufsz) {
    int h, w;
    getmaxyx(stdscr, h, w);
    int pw = w * 2 / 3;
    if (pw < 50) pw = 50;
    int px = (w - pw) / 2;
    int py = h / 2 - 3;

    WINDOW *pop = newwin(7, pw, py, px);
    wbkgd(pop, COLOR_PAIR(CP_NORMAL));
    box(pop, 0, 0);

    /* Barre titre rouge */
    wattron(pop, COLOR_PAIR(CP_TOPBAR) | A_BOLD);
    for (int c = 1; c < pw - 1; c++) mvwaddch(pop, 0, c, ' ');
    int lx = (pw - (int)strlen(label) - 2) / 2;
    mvwprintw(pop, 0, lx, " %s ", label);
    wattroff(pop, COLOR_PAIR(CP_TOPBAR) | A_BOLD);

    /* Hint */
    wattron(pop, COLOR_PAIR(CP_URL) | A_DIM);
    mvwprintw(pop, 2, 3, "Recherche ou URL");
    wattroff(pop, COLOR_PAIR(CP_URL) | A_DIM);

    /* Champ de saisie */
    wattron(pop, A_BOLD);
    mvwprintw(pop, 4, 2, " ❯ ");
    wattroff(pop, A_BOLD);

    /* Ligne de saisie soulignée */
    wattron(pop, A_UNDERLINE);
    for (int c = 5; c < pw - 2; c++) mvwaddch(pop, 4, c, ' ');
    wattroff(pop, A_UNDERLINE);

    wrefresh(pop);

    echo();
    curs_set(1);
    memset(buf, 0, bufsz);
    mvwgetnstr(pop, 4, 5, buf, bufsz - 1);
    noecho();
    curs_set(0);
    delwin(pop);
    touchwin(stdscr);
    refresh();
}

/* ─── Calcul hauteur image depuis métadonnées ────────────────────── */
/* Format ligne : ##IM path|label|orig_w|orig_h                        */
static void parse_im_line(const char *line,
                           char *imgpath, int pathsz,
                           int *orig_w, int *orig_h) {
    /* Extraire path (champ 0), orig_w (champ 2), orig_h (champ 3) */
    strncpy(imgpath, line + 5, pathsz - 1);
    imgpath[pathsz - 1] = '\0';
    *orig_w = 0; *orig_h = 0;

    char *p = imgpath;
    int field = 0;
    while (*p) {
        if (*p == '|') {
            *p = '\0';
            field++;
            if (field == 1) {
                /* label — on s'en fiche */
            } else if (field == 2) {
                *orig_w = atoi(p + 1);
            } else if (field == 3) {
                *orig_h = atoi(p + 1);
                break;
            }
        }
        p++;
    }
}

static int calc_img_h(int orig_w, int orig_h, int term_w, int term_h) {
    /* Chaque cellule terminal ≈ 2× plus haute que large (ratio 1:2) */
    if (orig_w > 0 && orig_h > 0) {
        int h = (int)((double)orig_h / (double)orig_w * (double)term_w * 0.5);
        if (h < 3)        h = 3;
        if (h > term_h / 2) h = term_h / 2;  /* max : moitié écran */
        return h;
    }
    /* Pas de métadonnées : 1/4 écran par défaut (moins invasif) */
    int h = term_h / 4;
    if (h < 4) h = 4;
    return h;
}

/* ─── Overlay images chafa avec cache ───────────────────────────────── */

/* Cache : stocke les lignes ANSI rendues par chafa pour la position scroll courante */

static void draw_images_overlay(WINDOW *win) {
    int height, width;
    getmaxyx(win, height, width);
    int bx, by;
    getbegyx(win, by, bx);
    (void)bx;

    int img_w = width - 3;  /* -3 : 1 marge gauche + 1 marge droite + 1 colonne scrollbar */

    /* Reconstruire le cache si le scroll a changé */
    if (img_cache_scroll != page_scroll) {
        img_cache_clear();
        img_cache_scroll = page_scroll;

        /* si = screen row, li = logical index — must stay in sync with draw_page */
        int si = 0;
        for (int li = page_scroll; li < page_lines_count && si < height
                                   && img_cache_count < IMG_CACHE_LINES; li++) {
            char *line = page_lines[li];
            if (strncmp(line, "##IM ", 5) != 0) { si++; continue; }

            char imgpath[MAX_LEN];
            int orig_w, orig_h;
            parse_im_line(line, imgpath, MAX_LEN, &orig_w, &orig_h);

            /* Construire le chemin absolu */
            char abs_imgpath[MAX_LEN * 2];
            if (imgpath[0] == '/')
                snprintf(abs_imgpath, sizeof(abs_imgpath), "%s", imgpath);
            else
                snprintf(abs_imgpath, sizeof(abs_imgpath), "%s/%s", base_dir, imgpath);

            int img_h = calc_img_h(orig_w, orig_h, img_w, height);
            for (int _ri = 0; _ri < img_real_count; _ri++)
                if (img_real_line[_ri] == li) { img_h = img_real_h[_ri]; break; }
            if (img_h < 1) img_h = 1;

            if (access(abs_imgpath, F_OK) != 0) { si += img_h; continue; }

            int term_y = by + si + 1;

            char cmd[MAX_LEN * 2];
            snprintf(cmd, sizeof(cmd),
                     "chafa --size=%dx%d --colors=256 --animate=off '%s' 2>/dev/null",
                     img_w, img_h, abs_imgpath);

            FILE *p = popen(cmd, "r");
            if (!p) { si += img_h; continue; }

            char row[8192];
            int r = 0;
            while (r < img_h && fgets(row, sizeof(row), p)
                   && img_cache_count < IMG_CACHE_LINES) {
                int len = strlen(row);
                if (len > 0 && row[len - 1] == '\n') row[len - 1] = '\0';
                char entry[8200];
                snprintf(entry, sizeof(entry), "%d|%s", term_y + r, row);
                img_cache[img_cache_count++] = strdup(entry);
                r++;
            }
            pclose(p);
            int found = 0;
            for (int k = 0; k < img_real_count; k++)
                if (img_real_line[k] == li) { img_real_h[k] = r > 0 ? r : img_h; found = 1; break; }
            if (!found && img_real_count < 256) {
                img_real_line[img_real_count] = li;
                img_real_h[img_real_count]    = r > 0 ? r : img_h;
                img_real_count++;
            }
            si += img_h;
        }
    }

    /* Écrire le cache sur le terminal.
     * [%dX efface N colonnes depuis la position courante sans toucher la suite —
     * la scrollbar en colonne (width) est ainsi préservée. */
    int prev_y = -1;
    for (int i = 0; i < img_cache_count; i++) {
        char *e = img_cache[i];
        char *pip = strchr(e, '|');
        if (!pip) continue;
        int y = atoi(e);
        if (y != prev_y) {
            /* Effacer seulement les colonnes de l'image, pas la scrollbar */
            printf("\033[%d;1H\033[%dX", y, width - 2);
        }
        printf("\033[%d;1H%s\033[0m", y, pip + 1);
        prev_y = y;
    }
    if (img_cache_count > 0) fflush(stdout);
}



/* ─── Dessin : résultats de recherche ───────────────────────────── */
static void draw_results(WINDOW *win) {
    int height, width;
    getmaxyx(win, height, width);
    werase(win);
    wbkgd(win, COLOR_PAIR(CP_NORMAL));

    if (results_count == 0) {
        wattron(win, COLOR_PAIR(CP_URL) | A_DIM);
        mvwprintw(win, height / 2, (width - 32) / 2,
                  "Aucun résultat — Ctrl+R pour rechercher");
        wattroff(win, COLOR_PAIR(CP_URL) | A_DIM);
        wrefresh(win);
        return;
    }

    /* Chaque résultat occupe 3 lignes : titre / url / blanc */
    int row = 1;
    for (int idx = results_scroll; idx < results_count && row < height - 1; idx++) {
        int is_hl = (idx == results_hl);

        if (is_hl) {
            /* Surligner les 2 lignes du résultat */
            wattron(win, COLOR_PAIR(CP_HIGHLIGHT));
            for (int c = 0; c < width - 1; c++) {
                mvwaddch(win, row,     c, ' ');
                mvwaddch(win, row + 1, c, ' ');
            }
            wattroff(win, COLOR_PAIR(CP_HIGHLIGHT));
        }

        /* Ligne 1 : numéro + titre en gras */
        wattron(win, COLOR_PAIR(CP_URL) | A_DIM);
        mvwprintw(win, row, 2, "%2d", idx + 1);
        wattroff(win, COLOR_PAIR(CP_URL) | A_DIM);

        int title_x   = 6;
        int title_max = width - title_x - 2;
        if (title_max < 4) title_max = 4;

        if (is_hl)
            wattron(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
        else
            wattron(win, COLOR_PAIR(CP_TITLE) | A_BOLD);
        mvwprintw(win, row, title_x, "%.*s", title_max, results[idx].title);
        if (is_hl)
            wattroff(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
        else
            wattroff(win, COLOR_PAIR(CP_TITLE) | A_BOLD);

        /* Ligne 2 : URL soulignée en bleu */
        int url_max = width - title_x - 2;
        if (url_max < 4) url_max = 4;

        if (is_hl)
            wattron(win, COLOR_PAIR(CP_HIGHLIGHT) | A_UNDERLINE);
        else
            wattron(win, COLOR_PAIR(CP_URL) | A_UNDERLINE);
        mvwprintw(win, row + 1, title_x, "%.*s", url_max, results[idx].url);
        if (is_hl)
            wattroff(win, COLOR_PAIR(CP_HIGHLIGHT) | A_UNDERLINE);
        else
            wattroff(win, COLOR_PAIR(CP_URL) | A_UNDERLINE);

        row += 3;  /* titre + url + ligne vide */
    }

    /* Scrollbar */
    int visible = (height - 2) / 3;
    if (visible < 1) visible = 1;
    if (results_count > visible) {
        int sb_h = height - 2;
        int th   = (sb_h * visible) / results_count;
        if (th < 1) th = 1;
        int tp = (results_scroll * (sb_h - th)) / (results_count - visible);
        if (tp > sb_h - th) tp = sb_h - th;
        wattron(win, COLOR_PAIR(CP_SEPARATOR));
        for (int i = 0; i < sb_h; i++)
            mvwaddch(win, 1 + i, width - 1, ACS_VLINE);
        for (int i = 0; i < th; i++)
            mvwaddch(win, 1 + tp + i, width - 1, ACS_BLOCK);
        wattroff(win, COLOR_PAIR(CP_SEPARATOR));
    }

    wrefresh(win);
}

/* ─── Dessin : page rendue ───────────────────────────────────────── */
static void draw_page(WINDOW *win) {
    int height, width;
    getmaxyx(win, height, width);
    werase(win);
    wbkgd(win, COLOR_PAIR(CP_NORMAL));

    if (page_lines_count == 0) {
        wattron(win, COLOR_PAIR(CP_URL) | A_DIM);
        mvwprintw(win, height / 2, (width - 24) / 2, "Page vide ou non chargée");
        wattroff(win, A_DIM);
        wrefresh(win);
        return;
    }

    int next_link_line = -1;
    if (page_links_count > 0 && page_links_hl < page_links_count)
        next_link_line = link_positions[page_links_hl];

    /* si = screen row (0..height-1), li = logical index in page_lines[] */
    int si = 0;
    for (int li = page_scroll; li < page_lines_count && si < height; li++) {
        char *line = page_lines[li];
        int attr = COLOR_PAIR(CP_NORMAL);
        int prefix_len = 0;

        if (strncmp(line, "##IM ", 5) == 0) {
            char _ip[MAX_LEN]; int _ow, _oh;
            parse_im_line(line, _ip, MAX_LEN, &_ow, &_oh);
            int img_h = calc_img_h(_ow, _oh, width - 3, height);
            for (int _ri = 0; _ri < img_real_count; _ri++)
                if (img_real_line[_ri] == li) { img_h = img_real_h[_ri]; break; }
            for (int r = 0; r < img_h && si + r < height; r++) {
                wmove(win, si + r, 0);
                wclrtoeol(win);
            }
            si += img_h;
            continue;

        } else if (strncmp(line, "##H1 ", 5) == 0) {
            attr = COLOR_PAIR(CP_HEADING) | A_BOLD | A_UNDERLINE;
            prefix_len = 5;
        } else if (strncmp(line, "##H2 ", 5) == 0) {
            attr = COLOR_PAIR(CP_HEADING) | A_BOLD;
            prefix_len = 5;
        } else if (strncmp(line, "##H3 ", 5) == 0) {
            attr = COLOR_PAIR(CP_HEADING) | A_BOLD;
            prefix_len = 5;
        } else if (strncmp(line, "##LK ", 5) == 0) {
            int is_link_hl = (li == next_link_line);
            attr = is_link_hl ? (COLOR_PAIR(CP_LINK_HL) | A_BOLD)
                              : (COLOR_PAIR(CP_URL)     | A_UNDERLINE);
            prefix_len = 5;
        } else if (strncmp(line, "##VD ", 5) == 0) {
            attr = COLOR_PAIR(CP_VIDEO) | A_BOLD;
            prefix_len = 5;
        } else if (strncmp(line, "##HR", 4) == 0) {
            wattron(win, COLOR_PAIR(CP_SEPARATOR) | A_DIM);
            for (int c = 0; c < width - 1; c++)
                mvwaddch(win, si, c, ACS_HLINE);
            wattroff(win, A_DIM);
            si++;
            continue;
        }

        wattron(win, attr);
        mvwprintw(win, si, 0, "%.*s", width - 1, line + prefix_len);
        wattroff(win, attr);
        si++;
    }

    if (page_lines_count > height) {
        int sb_h = height;
        int th   = (sb_h * height) / page_lines_count;
        if (th < 1) th = 1;
        int tp = (page_scroll * (sb_h - th)) / (page_lines_count - height);
        if (tp > sb_h - th) tp = sb_h - th;
        wattron(win, COLOR_PAIR(CP_SEPARATOR));
        for (int i = 0; i < sb_h; i++)
            mvwaddch(win, i, width - 1, ACS_VLINE);
        for (int i = 0; i < th; i++)
            mvwaddch(win, tp + i, width - 1, ACS_BLOCK);
        wattroff(win, COLOR_PAIR(CP_SEPARATOR));
    }

    wrefresh(win);
}

/* ─── Panneau latéral des liens ──────────────────────────────────── */
static void draw_links_panel(int x, int y, int h, int w) {
    WINDOW *lw = newwin(h, w, y, x);
    wbkgd(lw, COLOR_PAIR(CP_ADDRBAR));
    box(lw, 0, 0);
    wattron(lw, COLOR_PAIR(CP_ADDRBAR) | A_BOLD);
    mvwprintw(lw, 0, 2, " Liens (%d) ", page_links_count);
    wattroff(lw, A_BOLD);

    int visible = h - 2;
    int start   = page_links_hl > visible ? page_links_hl - visible / 2 : 0;

    for (int i = 0; i < visible; i++) {
        int idx = start + i;
        if (idx >= page_links_count) break;
        int is_hl = (idx == page_links_hl);
        if (is_hl) wattron(lw, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
        else       wattron(lw, COLOR_PAIR(CP_URL));
        mvwprintw(lw, i + 1, 1, "%.*s", w - 3, page_links[idx].text);
        if (is_hl) wattroff(lw, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
        else       wattroff(lw, COLOR_PAIR(CP_URL));
    }
    wrefresh(lw);
    delwin(lw);
}

/* ─── Affichage image avec chafa ─────────────────────────────────── */
static void show_image(const char *path) {
    if (access(path, F_OK) != 0) return;

    int height, width;
    getmaxyx(stdscr, height, width);
    int img_w = width - 4;
    int img_h = height - 6;
    if (img_w < 10) img_w = 10;
    if (img_h < 5)  img_h = 5;

    /* Suspendre ncurses proprement */
    def_prog_mode();
    endwin();

    /* Chemin absolu pour chafa */
    char abs_path[MAX_LEN * 2];
    if (path[0] == '/')
        snprintf(abs_path, sizeof(abs_path), "%s", path);
    else
        snprintf(abs_path, sizeof(abs_path), "%s/%s", base_dir, path);

    char cmd[MAX_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "chafa --size=%dx%d --colors=256 --animate=off '%s'\n"
             "printf '\\n-- Appuyez sur Entrée pour revenir --'\n"
             "read _dummy",
             img_w, img_h, abs_path);
    system(cmd);

    /* Reprendre ncurses proprement */
    reset_prog_mode();
    setlocale(LC_ALL, "");
    clearok(curscr, TRUE);
    wrefresh(curscr);
}

/* ─── Lecture vidéo (convert + play) ────────────────────────────── */
static void play_video(const char *mp4_path, WINDOW *content_win) {
    char cmd_conv[MAX_LEN];
    snprintf(cmd_conv, sizeof(cmd_conv), "./convert \"%s\"", mp4_path);

    wclear(content_win);
    wrefresh(content_win);
    char msg[512];
    snprintf(msg, sizeof(msg), "Conversion vidéo...");

    pid_t pid = fork();
    if (pid == 0) { system(cmd_conv); exit(0); }
    if (pid > 0)  run_spinner(content_win, msg, pid);

    char gif[MAX_LEN], mp3[MAX_LEN];
    snprintf(gif, sizeof(gif), "%s", mp4_path);
    snprintf(mp3, sizeof(mp3), "%s", mp4_path);
    char *dot_gif = strrchr(gif, '.');
    char *dot_mp3 = strrchr(mp3, '.');
    if (dot_gif) strcpy(dot_gif, ".gif");
    if (dot_mp3) strcpy(dot_mp3, ".mp3");

    /* Suspendre ncurses proprement */
    def_prog_mode();
    endwin();

    char cmd_play[MAX_LEN * 2];
    snprintf(cmd_play, sizeof(cmd_play), "%s/play \"%s\" \"%s\"", base_dir, gif, mp3);
    system(cmd_play);

    /* Reprendre ncurses proprement */
    reset_prog_mode();
    setlocale(LC_ALL, "");
    clearok(curscr, TRUE);
    wrefresh(curscr);
}

/* ─── Lance cmd en silence ───────────────────────────────────────── */
static pid_t fork_silent(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        exit(1);
    }
    return pid;
}

/* ─── Ouverture d'une page ───────────────────────────────────────── */
static void open_url(const char *url, const char *title, WINDOW *content_win) {
    if (strncmp(url, "file://__img__", 14) == 0) {
        show_image(url + 14);
        return;
    }
    if (strncmp(url, "file://__video__", 16) == 0) {
        play_video(url + 16, content_win);
        return;
    }

    /* Résoudre les URLs relatives vers une URL absolue */
    char resolved[2048];
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        /* Extraire l'origine de current_url (scheme://host) */
        char origin[1024] = "";
        const char *p = current_url;
        /* Chercher "://" */
        const char *sep = strstr(p, "://");
        if (sep) {
            sep += 3; /* après :// */
            const char *slash = strchr(sep, '/');
            if (slash) {
                /* scheme://host */
                int olen = (int)(slash - current_url);
                if (olen < (int)sizeof(origin)) {
                    strncpy(origin, current_url, olen);
                    origin[olen] = '\0';
                }
            } else {
                strncpy(origin, current_url, sizeof(origin) - 1);
            }
        }
        if (url[0] == '/') {
            /* Chemin absolu : origin + path */
            snprintf(resolved, sizeof(resolved), "%s%s", origin, url);
        } else {
            /* Chemin relatif : origin + répertoire courant + path */
            char dir[1024] = "";
            const char *last_slash = strrchr(current_url, '/');
            if (last_slash && last_slash > current_url + 8) {
                int dlen = (int)(last_slash - current_url) + 1;
                if (dlen < (int)sizeof(dir)) {
                    strncpy(dir, current_url, dlen);
                    dir[dlen] = '\0';
                }
                snprintf(resolved, sizeof(resolved), "%s%s", dir, url);
            } else {
                snprintf(resolved, sizeof(resolved), "%s/%s", origin, url);
            }
        }
        url = resolved;
    }

    snprintf(current_url,   sizeof(current_url),   "%s", url);
    snprintf(current_title, sizeof(current_title), "%s", title ? title : url);

    { FILE *d = fopen("/tmp/vueko.log", "a");
      if (d) { fprintf(d, "open_url: %s\n", url); fclose(d); } }

    werase(content_win);
    wrefresh(content_win);

    char cmd[MAX_LEN * 2];
    /* Écrire l'URL dans un fichier temp : évite tout problème d'échappement shell */
    {   FILE *_uf = fopen("/tmp/vueko_url.txt","w");
        if (_uf) { fputs(url, _uf); fclose(_uf); } }
    snprintf(cmd, sizeof(cmd),
             "python3 %s/GET.py \"$(cat /tmp/vueko_url.txt)\" 2>>/tmp/vueko.log",
             base_dir);

    char msg[512];
    snprintf(msg, sizeof(msg), "Chargement: %.60s", title ? title : url);

    pid_t pid = fork_silent(cmd);
    if (pid > 0) run_spinner(content_win, msg, pid);

    werase(content_win);
    wrefresh(content_win);
    {
        char _cmd_r[4096];
        int _w = 0; { int _h; getmaxyx(stdscr, _h, _w); (void)_h; }
        snprintf(_cmd_r, sizeof(_cmd_r),
                 "python3 %s/render.py --width=%d 2>>/tmp/vueko.log",
                 base_dir, _w > 4 ? _w - 2 : 100);
        pid = fork_silent(_cmd_r);
    }
    if (pid > 0) run_spinner(content_win, "Rendu de la page...", pid);

    { FILE *d = fopen("/tmp/vueko.log", "a");
      if (d) {
        fprintf(d, "page.json existe: %d\n", access("datas/cache/page.json", F_OK) == 0);
        fprintf(d, "page_lines_count avant load: %d\n", page_lines_count);
        FILE *pj = fopen("datas/cache/page.json", "r");
        if (pj) { char tmp[200] = {0}; fread(tmp, 1, 199, pj); fclose(pj);
                  fprintf(d, "page.json debut: %.200s\n", tmp); }
        fclose(d);
      }
    }

    { int _iw = 0, _ih = 0; getmaxyx(stdscr, _ih, _iw); img_render_width = _iw > 4 ? _iw - 2 : 80; (void)_ih; }
    load_rendered_page("datas/cache/page.json");
    { FILE *d = fopen("/tmp/vueko.log", "a");
      if (d) { fprintf(d, "page_lines_count apres load: %d\n", page_lines_count); fclose(d); } }
    mode = MODE_PAGE;

    {
        char _cmd_ri[4096];
        int _wi = 0; { int _hi; getmaxyx(stdscr, _hi, _wi); (void)_hi; }
        snprintf(_cmd_ri, sizeof(_cmd_ri),
                 "python3 %s/render.py --images --width=%d 2>>/tmp/vueko.log",
                 base_dir, _wi > 4 ? _wi - 2 : 100);
        images_render_pid = fork_silent(_cmd_ri);
    }
}

/* ─── Main ───────────────────────────────────────────────────────── */
int main(void) {
    {
        char exe[4096] = {0};
        ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (len > 0) {
            exe[len] = '\0';
            char *slash = strrchr(exe, '/');
            if (slash) {
                *slash = '\0';
                strncpy(base_dir, exe, sizeof(base_dir) - 1);
                chdir(base_dir);
            }
        } else {
            getcwd(base_dir, sizeof(base_dir));
        }
        FILE *dbg = fopen("/tmp/vueko.log", "w");
        if (dbg) { fprintf(dbg, "base_dir: %s\n", base_dir); fclose(dbg); }
    }

    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();

    init_pair(CP_TOPBAR,    COLOR_WHITE,  COLOR_RED);
    init_pair(CP_STATUSBAR, COLOR_WHITE,  COLOR_GREEN);
    init_pair(CP_ADDRBAR,   COLOR_WHITE,  238);
    init_pair(CP_HIGHLIGHT, COLOR_BLACK,  COLOR_CYAN);
    init_pair(CP_TITLE,     COLOR_WHITE,  -1);
    init_pair(CP_URL,       COLOR_CYAN,   -1);
    init_pair(CP_SPINNER,   COLOR_BLACK,  COLOR_YELLOW);
    init_pair(CP_LINK_HL,   COLOR_BLACK,  COLOR_CYAN);
    init_pair(CP_HEADING,   COLOR_YELLOW, -1);
    init_pair(CP_NORMAL,    -1,           -1);
    init_pair(CP_VIDEO,     COLOR_BLACK,  COLOR_MAGENTA);
    init_pair(CP_SEPARATOR, COLOR_WHITE,  -1);

    if (!can_change_color())
        init_pair(CP_ADDRBAR, COLOR_WHITE, COLOR_BLACK);

    int height, width;
    getmaxyx(stdscr, height, width);

    WINDOW *top_win     = newwin(1, width, 0, 0);
    WINDOW *addr_win    = newwin(1, width, 1, 0);
    WINDOW *content_win = newwin(height - 3, width, 2, 0);
    WINDOW *status_win  = newwin(1, width, height - 1, 0);

    load_search_results("datas/cache/search_results.json");

    int ch;
    char query[512] = "";

    refresh();

    while (1) {
        getmaxyx(stdscr, height, width);
        wresize(top_win,     1,          width);
        wresize(addr_win,    1,          width);
        wresize(content_win, height - 3, width);
        wresize(status_win,  1,          width);
        mvwin(status_win, height - 1, 0);

        int content_h = height - 3;

        fill_bar(top_win, CP_TOPBAR,
                 " \xf0\x9f\x8c\x90 Vueko",
                 mode == MODE_SEARCH ? "[ Résultats ]" : current_title,
                 "^R:Recherche  B:Retour  Tab:Liens  Q:Quitter ");

        {
            werase(addr_win);
            wbkgd(addr_win, COLOR_PAIR(CP_ADDRBAR));
            wattron(addr_win, COLOR_PAIR(CP_ADDRBAR));
            mvwprintw(addr_win, 0, 1, " %.*s",
                      width - 4,
                      current_url[0] ? current_url : "about:blank");
            wattroff(addr_win, COLOR_PAIR(CP_ADDRBAR));
            wrefresh(addr_win);
        }

        if (mode == MODE_SEARCH) {
            /* Chaque résultat = 3 lignes, visible = content_h / 3 */
            int vis = content_h / 3;
            if (vis < 1) vis = 1;
            if (results_hl < results_scroll)
                results_scroll = results_hl;
            if (results_hl >= results_scroll + vis)
                results_scroll = results_hl - vis + 1;
            draw_results(content_win);
        } else {
            if (images_render_pid > 0) {
                int st;
                if (waitpid(images_render_pid, &st, WNOHANG) > 0) {
                    images_render_pid = -1;
                    img_cache_clear();   /* forcer redraw des images */
                    load_rendered_page("datas/cache/page.json");
                }
            }
            if (page_scroll < 0) page_scroll = 0;
            int max_scroll = page_lines_count - content_h;
            if (max_scroll < 0) max_scroll = 0;
            if (page_scroll > max_scroll) page_scroll = max_scroll;
            draw_page(content_win);

            if (links_panel_open && page_links_count > 0) {
                int pw = width / 3;
                if (pw < 20) pw = 20;
                draw_links_panel(width - pw, 2, content_h, pw);
            }
        }

        {
            char left[256], right[256];
            if (mode == MODE_SEARCH) {
                snprintf(left,  sizeof(left),  " %d résultat(s)", results_count);
                snprintf(right, sizeof(right), " [%d/%d] ",
                         results_count ? results_hl + 1 : 0, results_count);
            } else {
                snprintf(left,  sizeof(left),  " %d ligne(s)  %d lien(s)",
                         page_lines_count, page_links_count);
                int pct = page_lines_count > 0
                          ? (page_scroll * 100) / page_lines_count : 0;
                snprintf(right, sizeof(right), " %d%% ", pct);
            }
            fill_bar(status_win, CP_STATUSBAR, left, NULL, right);
        }

        /* Marquer tout le contenu comme corrompu → ncurses réécrit tout par-dessus les résidus chafa */
        if (mode == MODE_PAGE) redrawwin(content_win);
        doupdate();
        if (mode == MODE_PAGE) {
            /* Effacer les anciennes positions image */
            if (img_old_ys_count > 0) {
                int _tw = getmaxx(stdscr);
                for (int _i = 0; _i < img_old_ys_count; _i++) {
                    printf("\033[%d;1H\033[2K", img_old_ys[_i]);
                    for (int _c = 0; _c < _tw; _c++) putchar(' ');
                }
                img_old_ys_count = 0;
                fflush(stdout);
            }
            draw_images_overlay(content_win);
        }
        halfdelay(1);
        ch = getch();
        cbreak();

        if (ch == 'q' || ch == 'Q' || ch == 27) break;

        if (ch == 18) {  /* Ctrl+R */
            popup_input("Recherche", query, sizeof(query));
            if (query[0] == '\0') continue;

            char cmd[1024];
            snprintf(cmd, sizeof(cmd),
                     "python3 %s/search.py '%s' 2>>/tmp/vueko.log", base_dir, query);
            werase(content_win);
            wrefresh(content_win);
            char msg[512];
            snprintf(msg, sizeof(msg), "Recherche: %.80s", query);
            pid_t pid = fork_silent(cmd);
            if (pid > 0) run_spinner(content_win, msg, pid);

            load_search_results("datas/cache/search_results.json");
            snprintf(current_url, sizeof(current_url), "bing://search?q=%s", query);
            results_hl       = 0;
            results_scroll   = 0;
            mode             = MODE_SEARCH;
            links_panel_open = 0;
        }

        else if ((ch == 'b' || ch == 'B') && mode == MODE_PAGE) {
            mode             = MODE_SEARCH;
            links_panel_open = 0;
        }

        else if (ch == '\t' && mode == MODE_PAGE) {
            if (page_links_count > 0) {
                page_links_hl = (page_links_hl + 1) % page_links_count;
                page_scroll = link_positions[page_links_hl];
            }
        }

        else if (ch == KEY_UP) {
            if (mode == MODE_SEARCH) {
                if (results_hl > 0) results_hl--;
            } else {
                if (links_panel_open) {
                    if (page_links_hl > 0) {
                        page_links_hl--;
                        page_scroll = link_positions[page_links_hl];
                    }
                } else {
                    if (page_scroll > 0) page_scroll--;
                }
            }
        }

        else if (ch == KEY_DOWN) {
            if (mode == MODE_SEARCH) {
                if (results_hl < results_count - 1) results_hl++;
            } else {
                if (links_panel_open) {
                    if (page_links_hl < page_links_count - 1) {
                        page_links_hl++;
                        page_scroll = link_positions[page_links_hl];
                    }
                } else {
                    if (page_scroll < page_lines_count - content_h)
                        page_scroll++;
                }
            }
        }

        else if (ch == KEY_PPAGE) {
            if (mode == MODE_SEARCH) {
                results_hl -= PAGE_JUMP;
                if (results_hl < 0) results_hl = 0;
            } else {
                page_scroll -= PAGE_JUMP;
                if (page_scroll < 0) page_scroll = 0;
            }
        }

        else if (ch == KEY_NPAGE) {
            if (mode == MODE_SEARCH) {
                results_hl += PAGE_JUMP;
                if (results_hl >= results_count) results_hl = results_count - 1;
            } else {
                page_scroll += PAGE_JUMP;
            }
        }

        else if (ch == '\n' || ch == KEY_ENTER) {
            if (mode == MODE_SEARCH && results_count > 0) {
                open_url(results[results_hl].url,
                         results[results_hl].title,
                         content_win);
            } else if (mode == MODE_PAGE) {
                /* URL du lien courant (panneau ouvert ou non) */
                const char *target_url  = (page_links_count > 0) ? page_links[page_links_hl].url  : NULL;
                const char *target_text = (page_links_count > 0) ? page_links[page_links_hl].text : NULL;

                if (target_url && strncmp(target_url, "file://__img__", 14) == 0) {
                    /* Lien image : afficher directement */
                    show_image(target_url + 14);
                } else if (target_url && strncmp(target_url, "file://__video__", 16) == 0) {
                    play_video(target_url + 16, content_win);
                } else if (links_panel_open && target_url) {
                    open_url(target_url, target_text, content_win);
                } else {
                    if (target_url)
                        open_url(target_url, target_text, content_win);
                }
            }
        }

        else if (ch == KEY_RESIZE) {
            getmaxyx(stdscr, height, width);
            clear();
        }
    }

    free_page_lines();
    delwin(top_win);
    delwin(addr_win);
    delwin(content_win);
    delwin(status_win);
    endwin();
    { FILE *dbg = fopen("/tmp/vueko.log", "a");
      if (dbg) { fprintf(dbg, "exit normal\n"); fclose(dbg); } }
    return 0;
}