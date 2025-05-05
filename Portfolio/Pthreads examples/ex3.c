#include <stdio.h>
#include <string.h>          // strerror
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

int thread_count;
int loops;

void* thread_work(void* arg);

int main(int argc, char* argv[]) {

    if (argc != 3) {
        fprintf(stderr,"usage: %s <thread_count> <total_loops>\n", argv[0]);
        exit(1);
    }
    
    //##### preprocessing #####

    thread_count = atoi(argv[1]);
    if(thread_count<= 0) {
        fprintf(stderr,"thread_count must be positive\n");
        exit(1);
    }

    int total_loops= atoi(argv[2]);
    if(total_loops<= 0) {
        fprintf(stderr,"total_loops must be positive\n");
        exit(1);
    }

    loops = atoi(argv[2])/thread_count;
    int true_total_loops= loops*thread_count;
    if(true_total_loops!= total_loops)
        fprintf(stderr,"total_loops is not evenly distributed among threads. Using instead %d total loops for fairness\n", true_total_loops);


    int* counter_arr= calloc(thread_count, sizeof(int));
    int err;
    pthread_t threads[thread_count];
    //##########################

    struct timespec start, finish;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for(int i= 0; i< thread_count; i++) {
        if((err= pthread_create(&threads[i], NULL, thread_work, &counter_arr[i])) != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            exit(1);
        }
    }

    for(int i= 0; i< thread_count; i++) {
        if((err= pthread_join(threads[i], NULL)) != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            exit(1);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &finish);
    double time= (finish.tv_sec- start.tv_sec) + (finish.tv_nsec- start.tv_nsec)/1e9;
    
    int sum= 0;
    for(int i= 0; i< thread_count; i++) {
        printf("%d ", counter_arr[i]);
        sum+= counter_arr[i];
    }
    printf("\nTotal sum: %d\n", sum);
    printf("%d threads, %d loops per thread: %f\n",thread_count, loops, time);

    free(counter_arr);
    return 0;
}


void* thread_work(void* arg) {
    for(int i= 0; i< loops; i++)
        (*(int*)arg)++;

    return NULL;
}