#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "my_rand.h"
#include "timer.h"

/* Random ints are less than MAX_KEY */
const int MAX_KEY = 100000000;


/* Struct for list nodes */
struct list_node_s {
   int    data;
   struct list_node_s* next;
};

typedef struct {
    pthread_cond_t cond_readers;
    pthread_cond_t cond_writers;
    pthread_mutex_t mtx;
    int readers_active, readers_wait;
    int is_writer_active, writers_wait;
}rwlock_t;

void rwlock_init(rwlock_t*);
void rwlock_rdlock(rwlock_t*);
void rwlock_wrlock(rwlock_t*);
void rwlock_unlock(rwlock_t*);
void rwlock_destroy(rwlock_t*);

/* Shared variables */
struct      list_node_s* head = NULL;  
int         thread_count;
int         total_ops;
double      insert_percent;
double      search_percent;
double      delete_percent;
int         priority;
rwlock_t rwlock;
pthread_mutex_t     count_mutex;
int         member_count = 0, insert_count = 0, delete_count = 0;

/* Setup and cleanup */
void        Usage(char* prog_name);
void        Get_input(int* inserts_in_main_p);

/* Thread function */
void*       Thread_work(void* rank);

/* List operations */
int         Insert(int value);
void        Print(void);
int         Member(int value);
int         Delete(int value);
void        Free_list(void);
int         Is_empty(void);

/*-----------------------------------------------------------------*/
int main(int argc, char* argv[]) {
   long i; 
   int key, success, attempts;
   pthread_t* thread_handles;
   int inserts_in_main;
   unsigned seed = 1;
   double start, finish;

   if (argc != 2) Usage(argv[0]);
   thread_count = strtol(argv[1],NULL,10);

   Get_input(&inserts_in_main);

   /* Try to insert inserts_in_main keys, but give up after */
   /* 2*inserts_in_main attempts.                           */
   i = attempts = 0;
   while ( i < inserts_in_main && attempts < 2*inserts_in_main ) {
      key = my_rand(&seed) % MAX_KEY;
      success = Insert(key);
      attempts++;
      if (success) i++;
   }
   printf("Inserted %ld keys in empty list\n", i);

#  ifdef OUTPUT
   printf("Before starting threads, list = \n");
   Print();
   printf("\n");
#  endif

   thread_handles = malloc(thread_count*sizeof(pthread_t));
   pthread_mutex_init(&count_mutex, NULL);
   rwlock_init(&rwlock);

   GET_TIME(start);
   for (i = 0; i < thread_count; i++)
      pthread_create(&thread_handles[i], NULL, Thread_work, (void*) i);

   for (i = 0; i < thread_count; i++)
      pthread_join(thread_handles[i], NULL);
   GET_TIME(finish);
   printf("Elapsed time = %e seconds\n", finish - start);
   printf("Total ops = %d\n", total_ops);
   printf("member ops = %d\n", member_count);
   printf("insert ops = %d\n", insert_count);
   printf("delete ops = %d\n", delete_count);

#  ifdef OUTPUT
   printf("After threads terminate, list = \n");
   Print();
   printf("\n");
#  endif

   Free_list();
   rwlock_destroy(&rwlock);
   pthread_mutex_destroy(&count_mutex);
   free(thread_handles);

   return 0;
}  /* main */


/*-----------------------------------------------------------------*/
void Usage(char* prog_name) {
   fprintf(stderr, "usage: %s <thread_count>\n", prog_name);
   exit(0);
}  /* Usage */

/*-----------------------------------------------------------------*/
void Get_input(int* inserts_in_main_p) {

   printf("How many keys should be inserted in the main thread?\n");
   scanf("%d", inserts_in_main_p);
   printf("How many ops total should be executed?\n");
   scanf("%d", &total_ops);
   printf("Percent of ops that should be searches? (between 0 and 1)\n");
   scanf("%lf", &search_percent);
   printf("Percent of ops that should be inserts? (between 0 and 1)\n");
   scanf("%lf", &insert_percent);
   printf("Readers or writers priority? (0 for readers, 1 for writers)\n");
   scanf("%d", &priority);
   delete_percent = 1.0 - (search_percent + insert_percent);
}  /* Get_input */

/*-----------------------------------------------------------------*/
/* Insert value in correct numerical location into list */
/* If value is not in list, return 1, else return 0 */
int Insert(int value) {
   struct list_node_s* curr = head;
   struct list_node_s* pred = NULL;
   struct list_node_s* temp;
   int rv = 1;
   
   while (curr != NULL && curr->data < value) {
      pred = curr;
      curr = curr->next;
   }

   if (curr == NULL || curr->data > value) {
      temp = malloc(sizeof(struct list_node_s));
      temp->data = value;
      temp->next = curr;
      if (pred == NULL)
         head = temp;
      else
         pred->next = temp;
   } else { /* value in list */
      rv = 0;
   }

   return rv;
}  /* Insert */

/*-----------------------------------------------------------------*/
void Print(void) {
   struct list_node_s* temp;

   printf("list = ");

   temp = head;
   while (temp != (struct list_node_s*) NULL) {
      printf("%d ", temp->data);
      temp = temp->next;
   }
   printf("\n");
}  /* Print */


/*-----------------------------------------------------------------*/
int  Member(int value) {
   struct list_node_s* temp;

   temp = head;
   while (temp != NULL && temp->data < value)
      temp = temp->next;

   if (temp == NULL || temp->data > value) {
#     ifdef DEBUG
      printf("%d is not in the list\n", value);
#     endif
      return 0;
   } else {
#     ifdef DEBUG
      printf("%d is in the list\n", value);
#     endif
      return 1;
   }
}  /* Member */

/*-----------------------------------------------------------------*/
/* Deletes value from list */
/* If value is in list, return 1, else return 0 */
int Delete(int value) {
   struct list_node_s* curr = head;
   struct list_node_s* pred = NULL;
   int rv = 1;

   /* Find value */
   while (curr != NULL && curr->data < value) {
      pred = curr;
      curr = curr->next;
   }
   
   if (curr != NULL && curr->data == value) {
      if (pred == NULL) { /* first element in list */
         head = curr->next;
#        ifdef DEBUG
         printf("Freeing %d\n", value);
#        endif
         free(curr);
      } else { 
         pred->next = curr->next;
#        ifdef DEBUG
         printf("Freeing %d\n", value);
#        endif
         free(curr);
      }
   } else { /* Not in list */
      rv = 0;
   }

   return rv;
}  /* Delete */

/*-----------------------------------------------------------------*/
void Free_list(void) {
   struct list_node_s* current;
   struct list_node_s* following;

   if (Is_empty()) return;
   current = head; 
   following = current->next;
   while (following != NULL) {
#     ifdef DEBUG
      printf("Freeing %d\n", current->data);
#     endif
      free(current);
      current = following;
      following = current->next;
   }
#  ifdef DEBUG
   printf("Freeing %d\n", current->data);
#  endif
   free(current);
}  /* Free_list */

/*-----------------------------------------------------------------*/
int  Is_empty(void) {
   if (head == NULL)
      return 1;
   else
      return 0;
}  /* Is_empty */

/*-----------------------------------------------------------------*/
void* Thread_work(void* rank) {
   long my_rank = (long) rank;
   int i, val;
   double which_op;
   unsigned seed = my_rank + 1;
   int my_member_count = 0, my_insert_count=0, my_delete_count=0;
   int ops_per_thread = total_ops/thread_count;

   for (i = 0; i < ops_per_thread; i++) {
      which_op = my_drand(&seed);
      val = my_rand(&seed) % MAX_KEY;
      if (which_op < search_percent) {
         rwlock_rdlock(&rwlock);
         Member(val);
         rwlock_unlock(&rwlock);
         my_member_count++;
      } else if (which_op < search_percent + insert_percent) {
         rwlock_wrlock(&rwlock);
         Insert(val);
         rwlock_unlock(&rwlock);
         my_insert_count++;
      } else { /* delete */
         rwlock_wrlock(&rwlock);
         Delete(val);
         rwlock_unlock(&rwlock);
         my_delete_count++;
      }
   }  /* for */

   pthread_mutex_lock(&count_mutex);
   member_count += my_member_count;
   insert_count += my_insert_count;
   delete_count += my_delete_count;
   pthread_mutex_unlock(&count_mutex);

   return NULL;
}  /* Thread_work */


/*-----------------------------------------------------------------*/
void rwlock_init(rwlock_t* rw) {
    pthread_cond_init(&rw->cond_readers, NULL);
	pthread_cond_init(&rw->cond_writers, NULL);
    pthread_mutex_init(&rw->mtx, NULL);
    rw->readers_active= 0;
    rw->readers_wait= 0;
    rw->is_writer_active= 0;
    rw->writers_wait= 0;
}


/*-----------------------------------------------------------------*/
void rwlock_rdlock(rwlock_t* rwlock) {
    pthread_mutex_lock(&rwlock->mtx);
    // while there is an active writer, reader thread waits
    while(rwlock->is_writer_active) {
        rwlock->readers_wait++;
        pthread_cond_wait(&rwlock->cond_readers, &rwlock->mtx);
        rwlock->readers_wait--;
    }
    rwlock->readers_active++;
    pthread_mutex_unlock(&rwlock->mtx);
}

void rwlock_wrlock(rwlock_t* rwlock) {
    pthread_mutex_lock(&rwlock->mtx);
    // only one writer, so if there is already an active writer (or readers), writer thread waits
    while(rwlock->readers_active > 0 || rwlock->is_writer_active) {
        rwlock->writers_wait++;
        pthread_cond_wait(&rwlock->cond_writers, &rwlock->mtx);
        rwlock->writers_wait--;
    }
    rwlock->is_writer_active= 1;
    pthread_mutex_unlock(&rwlock->mtx);
}

void rwlock_unlock(rwlock_t* rwlock) {
    pthread_mutex_lock(&rwlock->mtx);
    // Only 1 writer can be active. If there is no active writer,
    // then this function has been called by a reader.
    if(rwlock->is_writer_active) {
        rwlock->is_writer_active= 0;
        if(priority==1) {
            if(rwlock->writers_wait)
               pthread_cond_signal(&rwlock->cond_writers);
            else  // even if writer has priority, if there are none waiting we signal readers instead to avoid reader starvation
               pthread_cond_broadcast(&rwlock->cond_readers);
        }
        else {
            if(rwlock->readers_wait)
               pthread_cond_broadcast(&rwlock->cond_readers);
            else
               pthread_cond_signal(&rwlock->cond_writers);
        }
    }
    else {
        rwlock->readers_active--;
        //  if no active readers & there are waiting writers, signal one of them
        if(rwlock->readers_active == 0 && rwlock->writers_wait)
            pthread_cond_signal(&rwlock->cond_writers);
    }

    pthread_mutex_unlock(&rwlock->mtx);
}
/*-----------------------------------------------------------------*/


void rwlock_destroy(rwlock_t* rwlock) {
    pthread_mutex_destroy(&rwlock->mtx);
    pthread_cond_destroy(&rwlock->cond_writers);
    pthread_cond_destroy(&rwlock->cond_readers);
}