#include <stdio.h>
#include <stdlib.h>

// Fichier pour la recherche de sites (moteur de recherche)

// Arbre pour les recherches
typedef struct noeud{
    char *word;
    int count;
    noeud *next;
} noeud;

    