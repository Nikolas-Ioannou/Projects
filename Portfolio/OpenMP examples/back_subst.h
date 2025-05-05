#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include <math.h>

//  command line arguments
typedef struct {
    int n;
    char* algorithm;
    char* method;
    int thread_count;
}Args;

// Initializing with validation in a function rather than in main, for clarity
void init_args(Args* args, int argc, char** argv);

// Generate (n x n+1) random values for an augmented matrix
void generate_matrix(float** A, int n);

// Gaussian elimination on an augmented matrix
void gauss_elimination(float** A, int n);

// Backward substitution on an upper triangular augmented matrix
void backward_substitution_row(float** A, float* x, int n);

// Parallel backward substitution on an upper triangular augmented matrix
void parallel_backward_substitution_row(float** A, float* x, int n, int num_threads);

// Backward substitution on an upper triangular augmented matrix
void backward_substitution_column(float** A, float* x, int n);

// Parallel backward substitution on an upper triangular augmented matrix
void parallel_backward_substitution_column(float** A, float* x, int n, int thread_count);

// Print an m x n matrix
void print_matrix(float** A, int m, int n);