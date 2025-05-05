/*1.1*/
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#define MR_MULTIPLIER 279470273 
#define MR_MODULUS 4294967291U
#define MR_DIVISOR ((double) 4294967291U)



typedef struct { // This will hold for each thread some informations
    long long int darts_in_thread; // How many darts it will process
    long long int darts_in_circle;// How many darts where on the circle
} ThreadData;

/*The functions which my_rand.c had which will help for findind a random double number between -1 and 1*/
unsigned my_rand(unsigned* seed_p) {
   long long z = *seed_p;
   z *= MR_MULTIPLIER; 
   z %= MR_MODULUS;
   *seed_p = z;
   return *seed_p;
}

double my_drand(unsigned* seed_p) {
   unsigned x = my_rand(seed_p);
   double y = x/MR_DIVISOR;
   return 2.0 * y - 1.0;
}

// Monte Carlo simulation for a thread
void *monte_carlo_thread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    long long int darts_in_circle = 0;
    unsigned seed = 1;
    unsigned tempx,tempy;
    tempx = my_rand(&seed); 
    tempy = my_rand(&seed);
    double x,y;
    for (long long int i = 0; i < data->darts_in_thread; i++) {
        tempx = my_rand(&tempx);
        tempy = my_rand(&tempy);
        y = my_drand(&tempy);
        x = my_drand(&tempx);
        if (x * x + y * y <= 1.0) {
            darts_in_circle++;
        }
    }
    data->darts_in_circle = darts_in_circle;
    pthread_exit(NULL);
}

// Function to calculate the run time for each way
double calculate_elapsed_time(struct timespec start, struct timespec finish) {
    double elapsed = finish.tv_sec - start.tv_sec;
    elapsed += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
    return elapsed;
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr,"usage: %s <total_darts> <total_threads>\n", argv[0]);
        exit(1);
    }
    long long int total_darts = atoll(argv[1]); // Get the amount of darts
    int total_threads = atoi(argv[2]);// How many threads will there be
    if (total_darts <= 0 || total_threads <= 0) {
        printf("The number of total darts or the number of total threads is invalid\n");
        return 1;
    }
    // Sequential way
    long long int darts_in_circle_seq = 0;
    struct timespec start_seq, finish_seq;
    clock_gettime(CLOCK_MONOTONIC, &start_seq); // To know the time when it started
    double x,y;
    unsigned tempx,tempy;
    unsigned seed = 1;
    tempx = my_rand(&seed); //Using the functions from the my_rand.c so we can get a random double in (-1,1)
    tempy = my_rand(&seed);
    int i;
    for (i = 0; i < total_darts; i++) {
        tempx = my_rand(&tempx);
        tempy = my_rand(&tempy);
        y = my_drand(&tempy);
        x = my_drand(&tempx);
        if (x * x + y * y <= 1.0) {
            darts_in_circle_seq++;
        }
    }
    double pi_seq = 4.0 * darts_in_circle_seq / total_darts;
    clock_gettime(CLOCK_MONOTONIC, &finish_seq);// Sequential way ended
    double time_seq = calculate_elapsed_time(start_seq, finish_seq);
    // Parallel way
    pthread_t* threads = malloc(total_threads * sizeof(pthread_t)); // Allocate the threads
    ThreadData* thread_data = malloc (total_threads * sizeof(ThreadData)); // Allocate memory for the data of its thread
    long long int darts_per_thread = total_darts / total_threads;
    int status = 1;// If we find a decimal number we will put plus 1 to the first thread so set this to 1 as it is
    if(total_darts % total_threads == 0) {
        status = 0;// It isn't decimal
    }
    if(total_threads > total_darts ) { // If the amount of threads which will be using on the program is more than the amount of darts 
        total_threads = total_darts;// Set the total threads with the amount of total darts
    }
    long long int total_darts_in_circle_threads = 0;
    struct timespec start_par, finish_par;
    clock_gettime(CLOCK_MONOTONIC, &start_par);
    for (int i = 0; i < total_threads; i++) {
        if(i == 0 && status) {
            thread_data[i].darts_in_thread = darts_per_thread + 1; // Plus 1 because if it a decimal we need one more process  
        }
        else {
            thread_data[i].darts_in_thread = darts_per_thread; // How many darts we will run at the thread
        }
        thread_data[i].darts_in_circle = 0; // How many darts were found inside the cyrcle by the thread
        if (pthread_create(&threads[i], NULL, monte_carlo_thread, &thread_data[i]) != 0) {
            printf("Error on creating the thread %d\n", i);
            return 1;
        }
    }
    for (int i = 0; i < total_threads; i++) {// Start the process 
        pthread_join(threads[i], NULL);
        total_darts_in_circle_threads += thread_data[i].darts_in_circle;
    }
    double pi_par = 4.0 * total_darts_in_circle_threads / total_darts;
    clock_gettime(CLOCK_MONOTONIC, &finish_par); // Parallel way ended
    double time_par = calculate_elapsed_time(start_par, finish_par);
    // Print  the results
    printf("Total darts: %lld \n",total_darts);
    printf("Estimated π with Sequential Monte Carlo: %f\n", pi_seq);
    printf(" Execution time: %f seconds\n", time_seq);
    printf("Estimated π with Parallel Monte Carlo (with %d threads): %f\n", total_threads,pi_par);
    printf(" Execution time: %f seconds\n", time_par);
    free(threads);
    free(thread_data);
    return 0;
}