#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include <unistd.h>
#include <sys/wait.h> 

#define MAX_RESULTS 1000
#define MAX_LEN 1024

typedef struct {
    char title[MAX_LEN];
    char url[MAX_LEN];
} Result;

Result results[MAX_RESULTS];
int results_count = 0;
int scroll_offset = 0;

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

    if (!parsed_json || json_object_get_type(parsed_json) != json_type_array) {
        results_count = 0;
        free(json_str);
        return;
    }

    int array_len = json_object_array_length(parsed_json);
    results_count = array_len > MAX_RESULTS ? MAX_RESULTS : array_len;

    for (int i = 0; i < results_count; i++) {
        struct json_object *item = json_object_array_get_idx(parsed_json, i);
        struct json_object *title, *url;
        json_object_object_get_ex(item, "title", &title);
        json_object_object_get_ex(item, "url", &url);

        if (title && json_object_get_type(title) == json_type_string) {
            strncpy(results[i].title, json_object_get_string(title), MAX_LEN-1);
            results[i].title[MAX_LEN-1] = '\0';
        } else {
            results[i].title[0] = '\0';
        }

        if (url && json_object_get_type(url) == json_type_string) {
            strncpy(results[i].url, json_object_get_string(url), MAX_LEN-1);
            results[i].url[MAX_LEN-1] = '\0';
        } else {
            results[i].url[0] = '\0';
        }
    }

    json_object_put(parsed_json);
    free(json_str);
}

void get_user_input(WINDOW *win, char *query, int size) {
    int height, width;
    getmaxyx(stdscr, height, width);
    
    WINDOW *input_win = newwin(3, width / 2, height / 2 - 1, width / 4);
    box(input_win, 0, 0);
    wrefresh(input_win);

    echo();
    curs_set(1);
    mvwprintw(input_win, 1, 1, "Recherche: ");
    wclrtoeol(input_win);
    wmove(input_win, 1, 12);
    wgetnstr(input_win, query, size-1);
    curs_set(0);
    noecho();
    
    delwin(input_win);
}

void draw_content(WINDOW *win, int highlight) {
    int height, width;
    getmaxyx(win, height, width);
    werase(win);
    
    int max_visible = height; 

    for (int i = 0; i < max_visible; i++) {
        int result_index = scroll_offset + i;
        if (result_index >= results_count) break;

        int row = i;
        int max_text_width = width - 4; 

        char *display_line;
        char *url_line = results[result_index].url;
        int is_url_display = 0;

        if (strlen(results[result_index].title) > 0) {
            display_line = results[result_index].title;
        } else {
            display_line = url_line;
            is_url_display = 1;
        }

        if (result_index == highlight) {
            wattron(win, COLOR_PAIR(1));
        }

        mvwprintw(win, row, is_url_display ? 3 : 1, "%.*s", max_text_width, display_line); 

        if (result_index == highlight) {
            wattroff(win, COLOR_PAIR(1));
        }
    }
    
    if (results_count > max_visible) {
        int scrollbar_height = height;
        int thumb_height = (scrollbar_height * max_visible) / results_count;
        if (thumb_height < 1) thumb_height = 1;

        int thumb_pos = (scroll_offset * (scrollbar_height - thumb_height)) / (results_count - max_visible);
        if (thumb_pos > scrollbar_height - thumb_height) thumb_pos = scrollbar_height - thumb_height;


        for (int i = 0; i < scrollbar_height; i++) {
            mvwaddch(win, i, width - 1, ACS_VLINE); 
        }

        for (int i = 0; i < thumb_height; i++) {
            mvwaddch(win, thumb_pos + i, width - 1, ACS_BLOCK);
        }
    }
    
    wrefresh(win);
}

// Fonction générale d'animation
void show_animation(const char *message, int step) {
    int height, width;
    getmaxyx(stdscr, height, width);
    
    int anim_height = 3;
    int msg_len = strlen(message) + 5; 
    int anim_width = (msg_len > 40) ? msg_len : 40; 
    
    WINDOW *anim_win = newwin(anim_height, anim_width, height / 2 - 1, (width - anim_width) / 2);
    box(anim_win, 0, 0);
    
    char spinner[] = {'|', '/', '-', '\\'};
    
    mvwprintw(anim_win, 1, 1, "%s %c", message, spinner[step % 4]);
    wrefresh(anim_win);
    
    delwin(anim_win);
}

int main() {
    char query[256] = "";
    
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_CYAN);
    
    int height, width;
    getmaxyx(stdscr, height, width);
    
    // info_win est gardé mais ne contiendra plus l'aide utilisateur
    WINDOW *info_win = newwin(1, width, 0, 0); 
    WINDOW *content_win = newwin(height, width, 0, 0); // content_win prend toute la hauteur
    
    int highlight = 0;
    int ch;
    
    load_json("datas/cache/search_results.json");
    
    while(1) {
        // Suppression de l'en-tête d'information
        werase(info_win);
        wrefresh(info_win);
        
        int max_visible = height; // Utilise toute la hauteur

        if (results_count > 0 && highlight >= scroll_offset + max_visible) {
            scroll_offset = highlight - max_visible + 1;
        } else if (results_count > 0 && highlight < scroll_offset) {
            scroll_offset = highlight;
        }

        draw_content(content_win, highlight);

        ch = getch();

        if (ch == 18) { // Ctrl+R
            get_user_input(stdscr, query, sizeof(query));
            
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "python3 search.py \'%s\'", query);

            pid_t pid = fork();
            if (pid == 0) {
                // Processus enfant
                system(cmd);
                exit(0); 
            } else if (pid > 0) {
                // Processus parent: affiche l'animation
                int status;
                int step = 0;
                
                wclear(content_win);
                wrefresh(content_win);

                while (waitpid(pid, &status, WNOHANG) == 0) {
                    char message[256];
                    snprintf(message, sizeof(message), "Recherche de '%s' en cours...", query);
                    show_animation(message, step++); 
                    usleep(100000); 
                }
            }
            
            load_json("datas/cache/search_results.json");
            highlight = 0;
            scroll_offset = 0;
            
        }
        else if(ch == KEY_UP && highlight > 0) highlight--;
        else if(ch == KEY_DOWN && highlight < results_count-1) highlight++;
        else if(ch == 10 && results_count > 0) { // Entrée (Ouverture du lien)
            
            char cmd_get[2048];
            snprintf(cmd_get, sizeof(cmd_get), "python3 GET.py \"%s\"", results[highlight].url);
            
            pid_t pid = fork();
            if (pid == 0) {
                // Processus enfant
                system(cmd_get);
                exit(0); 
            } else if (pid > 0) {
                // Processus parent: affiche l'animation
                int status;
                int step = 0;
                
                wclear(content_win);
                wrefresh(content_win);
                
                // Message d'animation spécifique à l'ouverture du lien
                char message[256];
                snprintf(message, sizeof(message), "Ouverture de: %s", results[highlight].title);

                while (waitpid(pid, &status, WNOHANG) == 0) {
                    show_animation(message, step++);
                    usleep(100000); 
                }
                
                // Après exécution, rafraîchir l'écran pour retirer l'animation
                touchwin(content_win);
            }
            
        }
        else if(ch == 27) break; // ESC
    }

    delwin(info_win);
    delwin(content_win);
    endwin();
    return 0;
}