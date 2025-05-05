#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <time.h>
#define ALIVE 1
#define DEAD 0

// Counting the living Neighbours
int countneighbours(int **gen, int n, int x, int y) {
    int count = 0;
    for (int i = -1; i <= 1; i++) { // Fix the loop bounds
        for (int j = -1; j <= 1; j++) {
            if (i == 0 && j == 0) continue;
            int newrow = x + i;
            int newcol = y + j;
            if (newrow >= 0 && newrow < n && newcol >= 0 && newcol < n) {
                count += gen[newrow][newcol]; // Count alive neighbors
            }
        }
    }
    return count;
}

// Update the generation
void updategen(int **gen, int n, int **newgen, int parallelism, int totalthreads) {
    if (parallelism) {
#pragma omp parallel for num_threads(totalthreads)
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                int count = countneighbours(gen, n, i, j);
                if (gen[i][j] == ALIVE) {
                    newgen[i][j] = (count < 2 || count > 3) ? DEAD : ALIVE;
                } else {
                    newgen[i][j] = (count == 3) ? ALIVE : DEAD;
                }
            }
        }
    } else {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                int count = countneighbours(gen, n, i, j);
                if (gen[i][j] == ALIVE) {
                    newgen[i][j] = (count < 2 || count > 3) ? DEAD : ALIVE;
                } else {
                    newgen[i][j] = (count == 3) ? ALIVE : DEAD;
                }
            }
        }
    }

// Update the current generation
#pragma omp parallel for num_threads(totalthreads) if (parallelism)
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            gen[i][j] = newgen[i][j];
        }
    }
}

// Printing the generation based on the way which was choosen
void printgen(int **gen, int n,int method,int totalthreads) {
    if(method) { //Parallelism
        #pragma omp parallel for num_threads(totalthreads) 
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                printf("%c ", gen[i][j] == ALIVE ? 'X' : '.');
            }
            printf("\n");
        }
    }
    else { // Serial
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                printf("%c ", gen[i][j] == ALIVE ? 'X' : '.');
            }
            printf("\n");
        }
    }
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    if (argc < 4 || argc > 5) {
        printf("Usage: %s <generations> <grid_size> <method (0:serial, 1:parallel)> [threads]\n", argv[0]);
        return -1;
    }

    int totalgen = atoi(argv[1]); // Number of generations
    int n = atoi(argv[2]);        // Grid size
    int way = atoi(argv[3]);      // 0: serial, 1: parallel
    int totalthreads = 1;
    if (way == 1) {
        if (argc != 5) {
            printf("Error: Threads count required in parallel mode.\n");
            return -1;
        }
        totalthreads = atoi(argv[4]);
        if (totalthreads <= 0) {
            printf("Error: Thread count must be a positive integer.\n");
            return -1;
        }
    }
    // Allocate memory
    int **gen = malloc(n * sizeof(int *));
    int **new_gen = malloc(n * sizeof(int *));
    if (!gen || !new_gen) {
        printf("Error: Memory allocation failed.\n");
        return -1;
    }

    for (int i = 0; i < n; i++) {
        gen[i] = malloc(n * sizeof(int));
        new_gen[i] = malloc(n * sizeof(int));
        if (!gen[i] || !new_gen[i]) {
            printf("Error: Memory allocation failed.\n");
            return -1;
        }
        for (int j = 0; j < n; j++) {
            gen[i][j] = rand() % 2; // Initialize with random values
        }
    }
    printf("Starting grid:\n");
    printgen(gen, n,way,totalthreads);
    double start_time = omp_get_wtime();
    for (int i = 0; i < totalgen; i++) {
        updategen(gen, n, new_gen, way, totalthreads);
        printf("Generation %d:\n", i + 1);
        printgen(gen, n,way,totalthreads); // Print updated grid
    }
    double end_time = omp_get_wtime();
    printf("Execution Time: %f seconds\n", end_time - start_time);
    // Free allocated memory
    for (int i = 0; i < n; i++) {
        free(gen[i]);
        free(new_gen[i]);
    }
    free(gen);
    free(new_gen);
    return 0;
}