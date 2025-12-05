#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>

#define MAX_RESULTS 1000
#define MAX_LEN 1024

typedef struct {
    char title[MAX_LEN];
    char url[MAX_LEN];
} Result;

Result results[MAX_RESULTS];
int results_count = 0;

// Charger le JSON depuis datas/cache/search_results.json
void load_json(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Erreur ouverture fichier JSON");
        results_count = 0;
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *json_str = malloc(fsize + 1);
    fread(json_str, 1, fsize, fp);
    json_str[fsize] = 0;
    fclose(fp);

    struct json_object *parsed_json;
    parsed_json = json_tokener_parse(json_str);

    int array_len = json_object_array_length(parsed_json);
    results_count = array_len > MAX_RESULTS ? MAX_RESULTS : array_len;

    for (int i = 0; i < results_count; i++) {
        struct json_object *item = json_object_array_get_idx(parsed_json, i);
        struct json_object *title, *url;
        json_object_object_get_ex(item, "title", &title);
        json_object_object_get_ex(item, "url", &url);

        strncpy(results[i].title, json_object_get_string(title), MAX_LEN-1);
        strncpy(results[i].url, json_object_get_string(url), MAX_LEN-1);
    }

    free(json_str);
}

// Fonction pour demander la recherche via Ctrl+R
void get_user_input(WINDOW *win, char *query, int size) {
    echo();
    curs_set(1);
    mvwprintw(win, 0, 0, "Entrez votre recherche: ");
    wclrtoeol(win);
    wrefresh(win);
    wgetnstr(win, query, size-1);
    curs_set(0);
    noecho();
}

int main() {
    char query[256];

    // Initialisation ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_CYAN);

    int highlight = 0;
    int ch;

    while(1) {
        clear();
        mvprintw(0,0,"Appuyez Ctrl+R pour rechercher (ESC pour quitter)");
        for (int i=0; i<results_count; i++) {
            if (i == highlight) {
                attron(COLOR_PAIR(1));
                mvprintw(i+1,0,"%s | %s", strlen(results[i].title)>0 ? results[i].title : "<Sans titre>", results[i].url);
                attroff(COLOR_PAIR(1));
            } else {
                mvprintw(i+1,0,"%s | %s", strlen(results[i].title)>0 ? results[i].title : "<Sans titre>", results[i].url);
            }
        }
        refresh();

        ch = getch();
        if (ch == 18) { // Ctrl+R
            get_user_input(stdscr, query, sizeof(query));

            mvprintw(2,0,"Recherche en cours avec search.py...");
            wclrtoeol(stdscr);
            refresh();

            // Lancement de search.py et attendre la fin d'exécution
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "python3 search.py \"%s\"", query);
            system(cmd);

            // Charger résultats depuis datas/cache/search_results.json
            load_json("datas/cache/search_results.json");
            highlight = 0;
        }
        else if(ch == KEY_UP && highlight > 0) highlight--;
        else if(ch == KEY_DOWN && highlight < results_count-1) highlight++;
        else if(ch == 10 && results_count > 0) { // Entrée
            mvprintw(LINES-2,0,"Exécution de GET.py pour l'URL sélectionnée...");
            wclrtoeol(stdscr);
            refresh();

            char cmd_get[2048];
            snprintf(cmd_get, sizeof(cmd_get), "python3 GET.py \"%s\"", results[highlight].url);
            system(cmd_get);
        }
        else if(ch == 27) break; // ESC
    }

    endwin();
    return 0;
}
