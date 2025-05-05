#include <stdio.h>
#include <string.h>          // strerror
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

int thread_count;
int loops;

pthread_mutex_t mtx;

void* thread_wrok(void* arg);

int main(int argc, char* argv[]) {

    if (argc != 3) {
        fprintf(stderr,"usage: %s <thread_count> <loops>\n", argv[0]);
        exit(1);
    }
    
    //##### preprocessing #####

    thread_count = atoi(argv[1]);
    if(thread_count<= 0) {
        fprintf(stderr,"thread_count must be positive\n");
        exit(1);
    }
    loops = atoi(argv[2]);

    pthread_mutex_init(&mtx, NULL);

    int counter= 0;
    int err;
    pthread_t threads[thread_count];
    //##########################

    struct timespec start, finish;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for(int i= 0; i< thread_count; i++) {
        if((err= pthread_create(&threads[i], NULL, thread_wrok, &counter)) != 0) {
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

    printf("Total sum: %d\n", counter);
    printf("%d threads, %d loops: %f\n",thread_count, loops, time);

    return 0;
}


void* thread_wrok(void* arg) {
    for(int i= 0; i< loops; i++) {
        pthread_mutex_lock(&mtx);
        (*(int*)arg)++;
        pthread_mutex_unlock(&mtx);
    }

    return NULL;
}