#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>

void remove_mp4_extension(char *filename)
{
    char *dot = strrchr(filename, '.');
    if (dot && strcmp(dot, ".mp4") == 0)
        *dot = '\0';
}

int convert_mp4(char *filename)
{
    pid_t pid_gif, pid_mp3;
    int status_gif = 0, status_mp3 = 0;

    char base[512];
    char input_mp4[512];
    char output_gif[512];
    char output_mp3[512];

    snprintf(base, sizeof(base), "%s", filename);
    remove_mp4_extension(base);

    snprintf(input_mp4, sizeof(input_mp4), "%s.mp4", base);
    snprintf(output_gif, sizeof(output_gif), "%s.gif", base);
    snprintf(output_mp3, sizeof(output_mp3), "%s.mp3", base);

    pid_gif = fork();
    if (pid_gif == 0)
    {
        int fd = open("/dev/null", O_RDWR);
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        char *args_gif[] = {
            "ffmpeg",
            "-loglevel", "quiet",
            "-i", input_mp4,
            "-vf", "fps=24,scale=480:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse",
            "-loop", "0",
            output_gif,
            NULL
        };

        execvp("ffmpeg", args_gif);
        exit(1);
    }
    else if (pid_gif < 0)
    {
        return 1;
    }

    pid_mp3 = fork();
    if (pid_mp3 == 0)
    {
        int fd = open("/dev/null", O_RDWR);
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        char *args_mp3[] = {
            "ffmpeg",
            "-loglevel", "quiet",
            "-i", input_mp4,
            output_mp3,
            NULL
        };

        execvp("ffmpeg", args_mp3);
        exit(1);
    }
    else if (pid_mp3 < 0)
    {
        return 1;
    }

    waitpid(pid_gif, &status_gif, 0);
    waitpid(pid_mp3, &status_mp3, 0);

    if (WIFEXITED(status_gif) && WEXITSTATUS(status_gif) == 0 &&
        WIFEXITED(status_mp3) && WEXITSTATUS(status_mp3) == 0)
        return 0;

    return 1;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
        return 1;

    return convert_mp4(argv[1]);
}
