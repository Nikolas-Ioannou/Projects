#include "back_subst.h"

// ###########################################
// Helper functions

// Generate random integer in range [-range, range]
int rand_int(int range) {
    return (rand() % (2 * range + 1)) - range;
}

void swap(float** A, int a, int b) {
    float* temp= A[a];
    A[a]= A[b];
    A[b]= temp;
}

void error_handle(char* msg) {
    fprintf(stderr, "%s\n", msg);
}
// ###########################################

void init_args(Args* args, int argc, char** argv) {
    args->n= atoi(argv[1]);
    args->method= argv[2];
    args->algorithm= argv[3];
    args->thread_count= argc==5 ? atoi(argv[4]) : 1;
    
    int f= 0;

    if(args->n<= 0) {
        f= 1;
        error_handle("Size must be positive");
    }
    if(strcmp(args->method, "serial") && strcmp(args->method, "parallel")) {
        f= 1;
        error_handle("Method should be 'serial' or 'parallel'");
    }
    if(strcmp(args->algorithm, "row") && strcmp(args->algorithm, "column")) {
        f= 1;
        error_handle("Algorithm should be 'row' or 'column'");
    }
    if(!strcmp(args->method, "parallel") && argc!= 5) {
        f= 1;
        error_handle("Thread count required in parallel mode");
    }
    if(args->thread_count<= 0) {
        f= 1;
        error_handle("Thread count must be positive");
    }

    // Display all the erros first before exiting
    if(f)
        exit(1);
}

void generate_matrix(float** A, int n) {
    for(int i= 0; i< n; i++)
        A[0][i]= rand_int(n);   // n also used as range for simplicity
    A[0][n]= rand_int(n);   // b[0]

    for(int i= 1; i< n; i++) {
        A[i][0]=rand_int(n);
        for(int j= 1; j< n+1; j++)
            A[i][j]= rand_int(n);
    }
}

void gauss_elimination(float** A, int n) {
    int h= 0;   // pivot row
    int k= 0;   // pivot column
    while(h< n && k< n) {
        // find max value in k-th column to be k-th pivot
        int i_max= h;
        for(int i= h+1; i< n; i++)
            if(fabs(A[i][k]) > fabs(A[i_max][k]))
                i_max= i;
        
        // if no pivot found
        if(A[i_max][k]==0) {
            k++;
            continue;
        }

        swap(A, i_max, h);
        // for all rows below pivot:
        for(int i= h+1; i< n; i++) {
            float f= A[i][k]/ A[h][k];
            // fill with 0 below pivot
            A[i][k]= 0;

            for(int j= k+1; j<= n; j++)
                A[i][j]-= A[h][j]*f;
        }
        h++;
        k++;
    }
}

void backward_substitution_row(float** A, float* x, int n) {
    for(int row= n-1; row>= 0; row--) {
        x[row]= A[row][n]; // A[row][n] == b[row]
        for(int col= row+1; col< n; col++)
            x[row]-= A[row][col]* x[col];
        x[row]/= A[row][row];
    }
}

void parallel_backward_substitution_row(float** A, float* x, int n, int thread_count) {
    #pragma omp parallel num_threads(thread_count)
    {
        #pragma omp single  // x[row] depends on the computation of all x[col] for col > row, so cannot be parallelized
        for(int row= n-1; row>= 0; row--) {
            x[row]= A[row][n];  // A[row][n] == b[row]
            for(int col= row+ 1; col< n; col++) {
                x[row]-= A[row][col]* x[col];
            }
            x[row]/= A[row][row];
        }
    }
}

void backward_substitution_column(float** A, float* x, int n) {
    for(int row= 0; row< n; row++)
        x[row]= A[row][n]; // A[row][n] == b[row]

    for(int col= n-1; col>= 0; col--) {
        x[col]/= A[col][col];
        for(int row= 0; row< col; row++) {
            x[row]-= A[row][col]* x[col];
        }
    }
}

void parallel_backward_substitution_column(float** A, float* x, int n, int thread_count) {
    #pragma omp parallel num_threads(thread_count)
    {
    #pragma omp for
    for(int row= 0; row< n; row++) // static schedule
        x[row]= A[row][n]; // A[row][n] == b[row]

    for(int col= n-1; col>= 0; col--) {
        #pragma omp single
        x[col]/= A[col][col];

        #pragma omp for
        for(int row= 0; row< col; row++) {
            x[row]-= A[row][col]* x[col];
        }
    }
    }
}

void print_matrix(float** A, int m, int n) {
    for (int i = 0; i < m; i++) {
        for (int j = 0; j <= n; j++) {
            printf("%.4f ", A[i][j]);
        }
        printf("\n");
    }
}