#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "help.c"

int main(int argc, char **argv) {
    if(argc>0) {
        int i= 0;
        while(argv[i]) {
            if(strcmp(argv[i],"-h")==0 || strcmp(argv[i],"--help")==0) {
                help();
            }
            i++;
        }
    }
    
}