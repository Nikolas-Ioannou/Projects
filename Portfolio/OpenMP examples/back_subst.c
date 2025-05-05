#include <time.h>
#include "back_subst.h"


int main(int argc, char* argv[]) {
    if(argc < 4 || argc > 5) {
        fprintf(stderr, "Usage: %s <size> <method ('serial' or 'parallel')> <algorithm ('row' or 'column')> <thread_count>\n", argv[0]);
        exit(1);
    }

    Args args;
    init_args(&args, argc, argv);

    int n= args.n;
    char* algorithm= args.algorithm; // 'r': row, 'c': column
    char* method= args.method; // 0: serial, 1: parallel
    int thread_count= args.thread_count;

    float** A;
    float* x;
    A= malloc(n*sizeof(float*));
    for(int i= 0; i< n; i++)
        A[i]= malloc(sizeof(float)* (n+1)); // n + 1 to include constant vector b
    x= malloc(n*sizeof(float));

    srand(time(NULL));
    generate_matrix(A, n);
    gauss_elimination(A, n);

    struct timespec start, finish;
    clock_gettime(CLOCK_MONOTONIC, &start);

    if(!strcmp(method, "serial")) {   // serial
        if(!strcmp(algorithm, "row"))
            backward_substitution_row(A, x, n);
        else
            backward_substitution_column(A, x, n);
    }
    else {  // parallel
        if(!strcmp(algorithm, "row"))
            parallel_backward_substitution_row(A, x, n, thread_count);
        else
            parallel_backward_substitution_column(A, x, n, thread_count);
    }

    clock_gettime(CLOCK_MONOTONIC, &finish);
    double time= (finish.tv_sec- start.tv_sec) + (finish.tv_nsec- start.tv_nsec)/1e9;

    printf("Time: %f\n", time);
    printf("\nx vector:\n");
    print_matrix(&x, 1, n);

    for(int i= 0; i< n; i++)
        free(A[i]);
    free(A);
    free(x);

    return 0;
}