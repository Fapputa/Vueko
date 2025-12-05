#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int play_terminal(char *gif_path,char *mp3_path) {
    pid_t pid_gif, pid_audio;

    pid_gif = fork();
    if (pid_gif == 0) {
        execlp("chafa", "chafa", "--animate=on", gif_path, NULL);
        perror("Erreur chafa");
        exit(1);
    }

    pid_audio = fork();
    if (pid_audio == 0) {
        execlp("mpg123", "mpg123", mp3_path, NULL);
        perror("Erreur mpg123");
        exit(1);
    }

    wait(NULL);
    wait(NULL);

    return 0;
}

int main(int argc, char *argv[]) {
    
    int result = play_terminal(argv[1],argv[2]);
    return(result);
}
