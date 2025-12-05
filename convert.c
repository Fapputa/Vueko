#include <stdlib.h>
#include <stdio.h> 
#include <unistd.h>
#include <sys/wait.h>

#define CMD_BUFFER_SIZE 1024 

int convert_mp4(char *filename)
{
    pid_t pid_gif, pid_mp3;
    int status_gif, status_mp3;

    char input_mp4[512];
    char output_gif[512];
    char output_mp3[512];

    snprintf(input_mp4, sizeof(input_mp4), "%s", filename);
    snprintf(output_gif, sizeof(output_gif), "%s.gif", filename);
    snprintf(output_mp3, sizeof(output_mp3), "%s.mp3", filename);

    pid_gif = fork();
    if (pid_gif == 0)
    {
        char *args_gif[] = {
            "ffmpeg",
            "-i", input_mp4,
            "-vf",
	    "fps=30,scale=1280:720:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse",
            "-loop", "0",
            output_gif,
            NULL
        };

        execve("/usr/bin/ffmpeg", args_gif, NULL);
        perror("execve gif");
        exit(1);
    }

    pid_mp3 = fork();
    if (pid_mp3 == 0)
    {
        char *args_mp3[] = {
            "ffmpeg",
            "-i", input_mp4,
            output_mp3,
            NULL
        };

        execve("/usr/bin/ffmpeg", args_mp3, NULL);
        perror("execve mp3");
        exit(1);
    }

    waitpid(pid_gif, &status_gif, 0);
    waitpid(pid_mp3, &status_mp3, 0);

    if (WIFEXITED(status_gif) && WEXITSTATUS(status_gif) == 0 &&
        WIFEXITED(status_mp3) && WEXITSTATUS(status_mp3) == 0)
    {
        return 0;
    }

    return 1;
}



int main(int argc, char *argv[]) {
    if (argc < 2) {
        return 1;
    }
    char *filename_base = argv[1];
    int status = convert_mp4(filename_base);
    if(status == 0) {printf("yes\n");} else {printf("err\n");}
    return status;
}
