#include <stdlib.h>
#include <stdio.h> 

#define CMD_BUFFER_SIZE 1024 

int convert_mp4(char *filename) {
    char command_gif[CMD_BUFFER_SIZE];
    int status_gif;
    snprintf(command_gif, CMD_BUFFER_SIZE, 
        "ffmpeg -i \"%s.mp4\" -vf 'fps=30,scale=1280:720:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse' -loop 0 \"%s.gif\"", 
        filename, filename);
    status_gif = system(command_gif);
    return status_gif == 0 ? 0 : 1; 
}
int main(int argc, char *argv[]) {
    if (argc < 2) {
        return 1;
    }
    char *filename_base = argv[1];
    int status = convert_mp4(filename_base);
    return status;
}