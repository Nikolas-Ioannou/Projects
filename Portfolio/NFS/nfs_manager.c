#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>  
#include <sys/types.h>   
#include <sys/socket.h>  
#include <unistd.h>
#include <netinet/in.h>  
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <inttypes.h>
#define BUFFER_SIZE 526 // The size of the buffers will have a static value

// This struct stores the arguments of the command line 
typedef struct infoargs {
    char logfile[BUFFER_SIZE]; // The logfile of the manager
    char config_file[BUFFER_SIZE]; // The configure file
    int worker_limit; // How many workers there are 
    int port_number; // Port number which the manager will get informations from the console
    int bufferSize; // The max size of the Queue which will not be used anywhere
} infoargs;

// Informations of the sync
typedef struct sync_info_mem_store{
    char source_dir[BUFFER_SIZE];
    char target_dir[BUFFER_SIZE]; // The directories
    char source_host[BUFFER_SIZE];
    char target_host[BUFFER_SIZE]; // The hosts
    int source_port;
    int target_port; // The ports
    int status; // The access of the directories
    char last_sync_time[25]; // The time of the last sync
    int total_files; // The amount of the files that will be sync
    int amount; // Current amount that it is sync
    int error_count; // Total errors
    int source_fd;
    int target_fd;  // For the communication for the source and target client
    pthread_mutex_t command; // This will help with the communication with the source and target client
    uint32_t totalbytes; // The amount of bytes that have been pushed and pulled
    pthread_mutex_t lock_node; // Helps to read and write to the node struct
}sync_info_mem_store;

// Here the informations of the configure file will be stored and also if there is an add command, add it here 
typedef struct node {
    sync_info_mem_store* data;
    struct node* next;
}node;

// Queue reading from the config_file or add command if it is neccesery
typedef struct queue {
    node* data;
    char filename[BUFFER_SIZE]; // The name of the file
    struct queue* next; // The next 
    int addcommand; // If it is from an add command
    struct queue* previous; // The previous will be helpfull for the delete which will happen if there is a cancel command
}queue;

// The Threads that are working
typedef struct {
    pthread_t* threads; // Array with threads
    int totalrunning; // How many threads are working
    pthread_mutex_t datachanging; // If there is change on the total processes use this mutex
    pthread_mutex_t* lockfreethread; // This is for the changing data on freethread 
    int* freethread; // Will know if a thread is free
    FILE* logfile; // Add the logfile
    pthread_mutex_t lock_logfile; // Helps to write to the logfile
} threads_info;

// This will be passed to a worker thread
typedef struct thread_args {
    node* datanode; // The node which has the sync_info_mem_store
    int index; // Which thread is being used
    int socket; // The socket of the console so the manager can send data 
    threads_info* info; // Pointer to threads_info
    char filename[BUFFER_SIZE];// The name of the file
    int addcommand; // If it is 1 then it is an add command from console ,or else it was from the config_file operation
    int done; // If the thread was successfull
    pid_t thread; // The pid
}thread_args;

// Init some variables
node* headnode = NULL; // The node
infoargs informations; // Arguments 
queue* headqueue = NULL; // The Queue

// Closes all the clients and their servers
void shutdowneveryclient() {
    char command[BUFFER_SIZE] = "shutdown"; // The message for ending the clients
    node* cur = headnode;
    int length =  strlen(command);
    int net_length = htonl(length); // Convert the length to network byte 
    while(cur != NULL) {
         if(cur->data->source_fd != -1) {
            if (send(cur->data->source_fd,&net_length, sizeof(net_length), 0) < 0) { // Send first the length of the command
                perror("Error on communication with the source client");
            }
            if (send(cur->data->source_fd,&command, sizeof(command), 0) < 0) { // Send the command then 
                perror("Error on communication with the source client");
            }
            shutdown(cur->data->source_fd, SHUT_WR); // Close the client
            close(cur->data->source_fd);
        }
        if(cur->data->target_fd != -1) {
            if (send(cur->data->target_fd,&net_length, sizeof(net_length), 0) < 0) { 
                perror("Error on communication with the target client");
            }
            if (send(cur->data->target_fd,&command, sizeof(command), 0) < 0) { 
                perror("Error on communication with the target client");
            }
            shutdown(cur->data->target_fd, SHUT_WR);
            close(cur->data->target_fd);
        }
        cur = cur->next;
    }
}

// Free the node
void freenode() {
    node* temp = headnode;
    shutdowneveryclient(); // Closes all the clients first
    while(temp != NULL) {
        node* next = temp->next;
        pthread_mutex_destroy(&temp->data->command);
        pthread_mutex_destroy(&temp->data->lock_node);
        free(temp->data);
        free(temp);
        temp = next;
    }
    headnode = NULL;
}

// Delete all the files on a directory
int delete_files(const char* path) {
    DIR *dir;
    struct dirent *entry;
    char filepath[1024];
    dir = opendir(path);
    if (dir == NULL) {
        perror("opendir failed");
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        struct stat path_stat;
        if (stat(filepath, &path_stat) == -1) {
            perror("stat failed");
            continue;
        }
        if (S_ISREG(path_stat.st_mode)) {
            if (unlink(filepath) != 0) { // Delete the file
                perror("unlink failed");
            }
        }
    }
    closedir(dir);
    return 0;
}

//Get the time
void get_time(char *buffer, size_t size) {
    time_t raw_time;
    struct tm *time_info;
    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", time_info);
}

// This functions helps with getting a whole message based on the size of the message
ssize_t recv_all(int sockfd, void *buffer, size_t length) {
    size_t total_received = 0;
    ssize_t bytes_received;
    char *ptr = buffer;
    while (total_received < length) {
        bytes_received = recv(sockfd, ptr + total_received, length - total_received, 0);
        if (bytes_received <= 0) {
            perror("Error on reading");
            exit(EXIT_FAILURE);
        }
        total_received += bytes_received;
    }
    ptr[length] = '\0'; 
    return total_received;
}

// Creates and initialize a sync_info_mem_store structure for storing infos
sync_info_mem_store* create_sync_info(char* sourcedir,char* targetdir,char* host, char* host2,int port,int port2) {
    sync_info_mem_store* temp = malloc(sizeof(sync_info_mem_store));
    if(temp == NULL) {
        perror("Error on allocationg memory on struct sync_info_mem_store\n");
        return NULL;
    }
    strcpy(temp->source_dir, sourcedir);
    strcpy(temp->target_dir, targetdir);  
    strcpy(temp->source_host,host);
    strcpy(temp->target_host,host2);
    temp->source_port = port;
    temp->target_port = port2;
    temp->error_count = 0;
    strcpy(temp->last_sync_time, "NONE");
    temp->status = 0; // This means that there isn't a full sync between the 2 directories
    temp->amount = 0;
    temp->total_files = 0;
    temp->source_fd = -1;
    temp->target_fd = -1;
    pthread_mutex_init(&temp->command, NULL);
    pthread_mutex_init(&temp->lock_node, NULL);
    temp->totalbytes = 0;
    return temp;
}

// Reads the args of the terminal and stores them to the struct infoargs
int readargs(int argc, char* argv[]) {
    informations.worker_limit = 5;  
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            strcpy(informations.logfile, argv[i + 1]);
        } else if (strcmp(argv[i], "-c") == 0) {
            strcpy(informations.config_file, argv[i + 1]);
        } else if (strcmp(argv[i], "-n") == 0) {
            informations.worker_limit = atoi(argv[i + 1]);
        } 
        else if(strcmp(argv[i], "-p") == 0){
            informations.port_number = atoi(argv[i+1]);
        }
        else if(strcmp(argv[i], "-b") == 0) {
            informations.bufferSize = atoi(argv[i+1]);
        }
        else {
            perror("Wrong syntax on the args\n");
            return -1;
        }
        i++;
    }
    if (informations.worker_limit <= 0 || strlen(informations.config_file) == 0 ||strlen(informations.logfile) == 0 || informations.port_number <= 0 || informations.bufferSize <= 0) {
        perror("Invalid input from the args\n");
        return -1;
    }
    return 0;
}

// Reads the config_file and store the content to a linked list
node* read_config(char* config_file) {
    FILE *file = fopen(config_file, "r");
    if (file == NULL) {
        perror("Error on opening the config_file\n");
        return NULL;
    }
    node* head = NULL;  // Create the head of the linked list
    char line[BUFFER_SIZE];  // Here each line will be stored
    while (fgets(line, sizeof(line), file) != NULL) {  // Read each line
        line[strcspn(line, "\n")] = '\0'; 
        char* source = strtok(line, " ");
        char* target = strtok(NULL, " ");
        if (source == NULL || target == NULL) {
            fprintf(stderr, "Invalid config line: %s\n", line);
            fclose(file);
            exit(EXIT_FAILURE); 
        }
        char sourcedir[BUFFER_SIZE];
        char host[BUFFER_SIZE];
        int port;
        char targetdir[BUFFER_SIZE];
        char host2[BUFFER_SIZE];
        int port2;
        if (sscanf(source, "%[^@]@%[^:]:%d", sourcedir, host, &port) != 3) {
            perror("Invalid syntax on config_file");
            exit(EXIT_FAILURE);
        } 
        if (sscanf(target, "%[^@]@%[^:]:%d", targetdir, host2, &port2) != 3) {
            perror("Invalid syntax on config_file");
            exit(EXIT_FAILURE);
        } 
        // Store the pair in the linked list
        if(head == NULL) {
            head = malloc(sizeof(node));
            if(head == NULL) {
                perror("malloc failed for head");
                fclose(file);
                exit(EXIT_FAILURE);
            }
            head->data = create_sync_info(sourcedir, targetdir,host,host2,port,port2);
            head->next = NULL;
        }
        else {
            node* temp = head;
            while(temp->next != NULL) {
                temp = temp->next;
            }
            node* new_node = malloc(sizeof(node));
            if(new_node == NULL){
                perror("Error on malloc");
                exit(EXIT_FAILURE);
            }
            new_node->data = create_sync_info(sourcedir, targetdir,host,host2,port,port2);
            new_node->next = NULL;
            temp->next = new_node;
        }
    }
    fclose(file);
    return head;
}

// Add items to the queue
int addqueue(node* current,char* filename,int addornot) {
    if(headqueue == NULL) {
        headqueue = malloc(sizeof(queue));
        if(headqueue == NULL) {
            return -1;
        }
        headqueue->data = current; // The node which has the pair of directories
        headqueue->next = NULL;
        headqueue->previous = NULL; // This will help for the cancel operation
        headqueue->addcommand = addornot; // If it is an addoperation it will be 1
        strcpy(headqueue->filename,filename);
    }
    else {
        queue* temp = headqueue;
        queue* new_data = malloc(sizeof(queue));
        if(new_data == NULL) {
            return -1;
        }
        new_data->data = current;
        new_data->next = NULL;
        new_data->addcommand = addornot;
        strcpy(new_data->filename,filename);
        while(temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = new_data;
        new_data->previous = temp;
    }
    return 0;
}

// Read a line from a socket (until it is given \n)
char* read_line(int sockfd) {
    size_t buf_size = BUFFER_SIZE;
    size_t len = 0;
    char *buffer = malloc(buf_size);
    if (!buffer) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    while (1) {
        char ch;
        ssize_t bytes_read = recv(sockfd, &ch, 1, 0);
        if (bytes_read < 0) {
            perror("recv");
            free(buffer);
            exit(EXIT_FAILURE);
        } else if (bytes_read == 0) {
            break; 
        }
        if (len >= buf_size - 1) { // Leave space for null terminator
            buf_size *= 2;
            char *new_buffer = realloc(buffer, buf_size);
            if (!new_buffer) {
                perror("realloc");
                free(buffer);
                exit(EXIT_FAILURE);
            }
            buffer = new_buffer;
        }
        if (ch == '\n') {
            break;  // Stop reading at newline, but don't store it
        }
        buffer[len++] = ch;
    }
    buffer[len] = '\0';  // Null-terminate 
    return buffer;
}

// Worker function
void* worker_function(void* arg) {
    thread_args* args = (thread_args*) arg;
    pid_t tid = syscall(SYS_gettid);
    args->thread = tid; // Store the tid of the thread
    // Sent the command LIST to the source client
    char command[4 * BUFFER_SIZE];
    char time[25];
    // PULL Command in which the thread will take the chunk_size and then the data fo the file
    snprintf(command,sizeof(command),"PULL %s/%s",args->datanode->data->source_dir, args->filename);
    int length =  strlen(command);
    int net_length = htonl(length); // Convert the length to network byte        
    pthread_mutex_lock(&args->datanode->data->command); // Currently the 2 clients are communicating
    if (send(args->datanode->data->source_fd,&net_length, sizeof(net_length), 0) < 0) { // Send first the length of the command so the source client knows if it gets it
        pthread_mutex_unlock(&args->datanode->data->command); 
        get_time(time,sizeof(time));
        char logmess[10 * BUFFER_SIZE];
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        pthread_mutex_lock(&args->info->lock_logfile);
        fprintf(args->info->logfile,"%s\n", logmess); 
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
        fflush(args->info->logfile); // flush the content
        pthread_mutex_unlock(&args->info->lock_logfile);
        get_time(time,sizeof(time));
        pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
        args->datanode->data->status = 0; //sync didnt happen
        args->datanode->data->error_count++;
        pthread_mutex_unlock(&args->datanode->data->lock_node); 
        if(args->datanode->data->error_count == 1) { // Print an error message only one time 
            char sendmessage[5 * BUFFER_SIZE];
            snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
            printf("%s",sendmessage);
            if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                   args->done = -1; //When there isn't a communication with the console then close the program
                   return args;
                }
            }
        }
        pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
        args->info->freethread[args->index] = -2; // The thread is done
        pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
        return args; // It has the 0 value which means there was just an error and move on to the other processes
    }
    if (send(args->datanode->data->source_fd, command, strlen(command), 0) < 0) { // Then send the command PULL to the Source client
        pthread_mutex_unlock(&args->datanode->data->command); 
        get_time(time,sizeof(time));
        char logmess[10 * BUFFER_SIZE];
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        pthread_mutex_lock(&args->info->lock_logfile);
        fprintf(args->info->logfile,"%s\n",logmess); 
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        fprintf(args->info->logfile,"%s\n", logmess); 
        fflush(args->info->logfile);
        pthread_mutex_unlock(&args->info->lock_logfile);
        get_time(time,sizeof(time));
        pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
        args->datanode->data->status = 0;
        args->datanode->data->error_count++;
        if(args->datanode->data->error_count == 1) { // Print an error message only one time 
            char sendmessage[5 * BUFFER_SIZE];
            snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
            printf("%s",sendmessage);
            if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                    args->done = -1;
                    return args;
                }
            }
        }
        pthread_mutex_unlock(&args->datanode->data->lock_node); 
        pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
        args->info->freethread[args->index] = -2; // The thread is done
        pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
        return args;
    }
    int32_t net_chunk_size;
    size_t total_received = 0;
    char *ptr = (char *)&net_chunk_size;
    while (total_received < sizeof(net_chunk_size)) { // Get the first chunk size
        size_t received = recv(args->datanode->data->source_fd, ptr + total_received, sizeof(net_chunk_size) - total_received, 0);
        if (received <= 0) {
            pthread_mutex_unlock(&args->datanode->data->command); 
            get_time(time,sizeof(time));
            char logmess[10 * BUFFER_SIZE];
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Get message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            pthread_mutex_lock(&args->info->lock_logfile);
            fprintf(args->info->logfile,"%s\n", logmess); 
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Get message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
            fflush(args->info->logfile);
            pthread_mutex_unlock(&args->info->lock_logfile);
            get_time(time,sizeof(time));
            char sendmessage[5 * BUFFER_SIZE];
            snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
            printf("%s",sendmessage);
            pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
            args->datanode->data->status = 0;
            args->datanode->data->error_count++;
            if(args->datanode->data->error_count == 1) { // Print an error message only one time 
                char sendmessage[5 * BUFFER_SIZE];
                snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
                printf("%s",sendmessage);
                if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                    if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                        args->done = -1;
                        return args;
                    }
                }
            }
            pthread_mutex_unlock(&args->datanode->data->lock_node); 
            pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
            args->info->freethread[args->index] = -2; // The thread is done
            pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
            return args;
        }
        total_received += received;
    }
    int32_t chunk_size = (int32_t) ntohl(net_chunk_size); // Store it
    if(chunk_size == -1) { // Something went wrong
        pthread_mutex_unlock(&args->datanode->data->command); 
        char* errormes = read_line(args->datanode->data->source_fd);// Get the error message and then add it to the logfile
        char time[25];
        get_time(time,sizeof(time));
        char logmess[10 * BUFFER_SIZE];
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - %s]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename,errormes);
        pthread_mutex_lock(&args->info->lock_logfile);
        fprintf(args->info->logfile,"%s\n",logmess); 
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - %s]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename,errormes);
        fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
        fflush(args->info->logfile);
        pthread_mutex_unlock(&args->info->lock_logfile);
        get_time(time,sizeof(time));
        free(errormes);
        pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
        args->datanode->data->status = 0;
        args->datanode->data->error_count++;
        if(args->datanode->data->error_count == 1) { // Print an error message only one time 
            char sendmessage[5 * BUFFER_SIZE];
            snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
            printf("%s",sendmessage);
            if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                    args->done = -1;
                    return args;
                }
            }
        }
        pthread_mutex_unlock(&args->datanode->data->lock_node); 
        pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
        args->info->freethread[args->index] = -2; // The thread is done
        pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
        return args;
    }
    uint32_t total_chunk_size = (uint32_t) chunk_size; // Add the chunk at the total
    char* sourcedata = malloc(chunk_size + 1);
    if(sourcedata == NULL) {
        pthread_mutex_unlock(&args->datanode->data->command); 
        get_time(time,sizeof(time));
        char logmess[10 * BUFFER_SIZE];
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Malloc error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        pthread_mutex_lock(&args->info->lock_logfile);
        fprintf(args->info->logfile,"%s\n", logmess); 
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Malloc error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
        pthread_mutex_unlock(&args->info->lock_logfile);
        get_time(time,sizeof(time));
        pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
        args->datanode->data->status = 0;
        args->datanode->data->error_count++;
        if(args->datanode->data->error_count == 1) { // Print an error message only one time 
            char sendmessage[5 * BUFFER_SIZE];
            snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
            printf("%s",sendmessage);
            if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                    args->done = -1;
                    return args;
                }
            }
        }
        pthread_mutex_unlock(&args->datanode->data->lock_node); 
        pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
        args->info->freethread[args->index] = -2; // The thread is done
        pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
        return args;
    }
    if(recv_all(args->datanode->data->source_fd, sourcedata, chunk_size) != chunk_size) { // Get the first data based on the chunk_size
        pthread_mutex_unlock(&args->datanode->data->command); 
        get_time(time,sizeof(time));
        char logmess[10 * BUFFER_SIZE];
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Get message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        pthread_mutex_lock(&args->info->lock_logfile);
        fprintf(args->info->logfile,"%s\n", logmess); 
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Get message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
        fflush(args->info->logfile);
        pthread_mutex_unlock(&args->info->lock_logfile);
        get_time(time,sizeof(time));
        pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
        args->datanode->data->status = 0;
        args->datanode->data->error_count++;
        if(args->datanode->data->error_count == 1) { // Print an error message only one time 
            char sendmessage[5 * BUFFER_SIZE];
            snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
            printf("%s",sendmessage);
            if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                    args->done = -1;
                    return args;
                }
            }
        }
        pthread_mutex_unlock(&args->datanode->data->lock_node); 
        pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
        args->info->freethread[args->index] = -2; // The thread is done
        pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
        free(sourcedata);
        return args;
    } 
    snprintf(command,sizeof(command),"PUSH %s/%s -1 %s",args->datanode->data->target_dir ,args->filename, sourcedata);
    free(sourcedata);
    length =  strlen(command);
    net_length = htonl(length); // Convert the length to network byte 
    // If in the target is happening a push command ensure that no other info is going to it
    if (send(args->datanode->data->target_fd,&net_length, sizeof(net_length), 0) < 0) { // Send first the length of the command so the source client knows if he gets it
        pthread_mutex_unlock(&args->datanode->data->command); 
        get_time(time,sizeof(time));
        char logmess[10 * BUFFER_SIZE];
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        pthread_mutex_lock(&args->info->lock_logfile);
        fprintf(args->info->logfile,"%s\n", logmess); 
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
        fflush(args->info->logfile);
        pthread_mutex_unlock(&args->info->lock_logfile);
        get_time(time,sizeof(time));
        pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
        args->datanode->data->status = 0;
        args->datanode->data->error_count++;
        if(args->datanode->data->error_count == 1) { // Print an error message only one time 
            char sendmessage[5 * BUFFER_SIZE];
            snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
            printf("%s",sendmessage);
            if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                    args->done = -1;
                    return args;
                }
            }
        }
        pthread_mutex_unlock(&args->datanode->data->lock_node); 
        pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
        args->info->freethread[args->index] = -2; // The thread is done
        pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
        return args;
    }
    if (send(args->datanode->data->target_fd, command, strlen(command), 0) < 0) { // Then send the command PUSH to the TARGET
        pthread_mutex_unlock(&args->datanode->data->command); 
        get_time(time,sizeof(time));
        char logmess[10 * BUFFER_SIZE];
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        pthread_mutex_lock(&args->info->lock_logfile);
        fprintf(args->info->logfile,"%s\n", logmess); 
        snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
        args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
        fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
        fflush(args->info->logfile);
        pthread_mutex_unlock(&args->info->lock_logfile);
        get_time(time,sizeof(time));
        pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
        args->datanode->data->status = 0;
        args->datanode->data->error_count++;
        if(args->datanode->data->error_count == 1) { // Print an error message only one time 
            char sendmessage[5 * BUFFER_SIZE];
            snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
            printf("%s",sendmessage);
            if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                    args->done = -1;
                    return args;
                }
            }
        }
        pthread_mutex_unlock(&args->datanode->data->lock_node); 
        pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
        args->info->freethread[args->index] = -2; // The thread is done
        pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
        return args;
    }
    int error = 0;
    while(1) { //  Getting data and then add them
        int32_t net_file_size;
        size_t total_received = 0;
        char *ptr2 = (char *)&net_file_size;
        while (total_received < sizeof(net_file_size)) { // Get the first chunk size
            size_t received = recv(args->datanode->data->source_fd, ptr2 + total_received, sizeof(net_file_size) - total_received, 0);
            if (received <= 0) {
                pthread_mutex_unlock(&args->datanode->data->command); 
                get_time(time,sizeof(time));
                char logmess[10 * BUFFER_SIZE];
                snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Get message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
                args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
                pthread_mutex_lock(&args->info->lock_logfile);
                fprintf(args->info->logfile,"%s\n", logmess); 
                snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Get message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
                args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
                fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
                fflush(args->info->logfile);
                pthread_mutex_unlock(&args->info->lock_logfile);
                get_time(time,sizeof(time));
                char sendmessage[5 * BUFFER_SIZE];
                snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
                printf("%s",sendmessage);
                pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
                args->datanode->data->status = 0;
                args->datanode->data->error_count++;
                if(args->datanode->data->error_count == 1) { // Print an error message only one time 
                    char sendmessage[5 * BUFFER_SIZE];
                    snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
                    printf("%s",sendmessage);
                    if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                        if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                            args->done = -1;
                            return args;
                        }
                    }
                }
                pthread_mutex_unlock(&args->datanode->data->lock_node); 
                pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
                args->info->freethread[args->index] = -2; // The thread is done
                pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
                return args;
            }
            total_received += received;
        }
        chunk_size = (int32_t) ntohl(net_file_size);
        total_chunk_size += (uint32_t) chunk_size;
        if(send(args->datanode->data->target_fd, &chunk_size, sizeof(chunk_size), 0) < 0) { // Send the chunk_size 
            get_time(time,sizeof(time));
            char logmess[10 * BUFFER_SIZE];
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            pthread_mutex_lock(&args->info->lock_logfile);
            fprintf(args->info->logfile,"%s\n", logmess); 
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
            fflush(args->info->logfile);
            pthread_mutex_unlock(&args->info->lock_logfile);
            error = 1;
            break;
        }
        if(chunk_size == 0) { // The chunk_size was 0 from the source so end
            char* messagefrom = read_line(args->datanode->data->target_fd);
            if(strcmp(messagefrom,"ERROR\n") == 0) { // Get the message it get for the status of the operation push
                get_time(time,sizeof(time));
                char logmess[10 * BUFFER_SIZE];
                snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
                args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
                pthread_mutex_lock(&args->info->lock_logfile);
                fprintf(args->info->logfile,"%s\n", logmess); 
                snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
                args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
                fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
                fflush(args->info->logfile);
                pthread_mutex_unlock(&args->info->lock_logfile);
                error = 1;
            }
            free(messagefrom);
            break;
        }
        sourcedata = malloc(chunk_size);
        if(sourcedata == NULL) {
            get_time(time,sizeof(time));
            char logmess[10 * BUFFER_SIZE];
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Malloc error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            pthread_mutex_lock(&args->info->lock_logfile);
            fprintf(args->info->logfile,"%s\n", logmess); 
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Malloc error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
            fflush(args->info->logfile);
            pthread_mutex_unlock(&args->info->lock_logfile);
            get_time(time,sizeof(time));
            pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
            args->datanode->data->status = 0;
            args->datanode->data->error_count++;
            if(args->datanode->data->error_count == 1) { // Print an error message only one time 
                char sendmessage[5 * BUFFER_SIZE];
                snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
                printf("%s",sendmessage);
                if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                    if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                        exit(EXIT_FAILURE);
                    }
                }
            }
            pthread_mutex_unlock(&args->datanode->data->lock_node); 
            pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
            args->info->freethread[args->index] = -2; // The thread is done
            pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
            error = 1;
            break; 
        }
        if(recv_all(args->datanode->data->source_fd, sourcedata, chunk_size) != chunk_size) { // Get the first data based on the chunk_size
            pthread_mutex_unlock(&args->datanode->data->command); 
            get_time(time,sizeof(time));
            char logmess[10 * BUFFER_SIZE];
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Get message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            pthread_mutex_lock(&args->info->lock_logfile);
            fprintf(args->info->logfile,"%s\n", logmess); 
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Get message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
            fflush(args->info->logfile);
            pthread_mutex_unlock(&args->info->lock_logfile);
            get_time(time,sizeof(time));
            pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
            args->datanode->data->status = 0;
            args->datanode->data->error_count++;
            if(args->datanode->data->error_count == 1) { // Print an error message only one time 
                char sendmessage[5 * BUFFER_SIZE];
                snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
                printf("%s",sendmessage);
                if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                    if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                        exit(EXIT_FAILURE);
                    }
                }
            }
            pthread_mutex_unlock(&args->datanode->data->lock_node); 
            pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
            args->info->freethread[args->index] = -2; // The thread is done
            pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
            free(sourcedata);
            return NULL;
        } 
        // Then send them to the target_fd
        if(send(args->datanode->data->target_fd, sourcedata,strlen(sourcedata), 0) < 0) { // Send the data
            get_time(time,sizeof(time));
            char logmess[10 * BUFFER_SIZE];
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            pthread_mutex_lock(&args->info->lock_logfile);
            fprintf(args->info->logfile,"%s\n", logmess); 
            snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [ERROR] [File: %s - Send message error]",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,
            args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port,tid,args->filename);
            fprintf(args->info->logfile,"%s\n", logmess); // Write to the logfile for the PULL AND PUSH COMMAND Error
            fflush(args->info->logfile);
            pthread_mutex_unlock(&args->info->lock_logfile);
            error = 1;
            break;
        }
        free(sourcedata);
    }
    pthread_mutex_unlock(&args->datanode->data->command); // Doen using them
    if(error == 0) { // the sync on that file was SUCCESS
        pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
        args->datanode->data->totalbytes+= total_chunk_size;
        args->datanode->data->amount++;
        pthread_mutex_unlock(&args->datanode->data->lock_node); 
        pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
        args->info->freethread[args->index] = -2; // The thread is done
        pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
        args->done = 1; //It was done
    }
    else { // Write on the log the error on the file and also send the message to the console
        pthread_mutex_lock(&args->datanode->data->lock_node); // Changing data on the node
        args->datanode->data->status = 0;
        args->datanode->data->error_count++;
        if(args->datanode->data->error_count == 1) { // Print an error message only one time 
            char sendmessage[5 * BUFFER_SIZE];
            snprintf(sendmessage,sizeof(sendmessage),"[%s] Added file didn't happen: %s@%s:%d -> %s@%s:%d\n",time,args->datanode->data->source_dir,args->datanode->data->source_host,args->datanode->data->source_port,args->datanode->data->target_dir,args->datanode->data->target_host,args->datanode->data->target_port);
            printf("%s",sendmessage);
            if(args->addcommand == 1) { // If it was an add operation from the console must sent tis message
                if(send(args->socket,sendmessage,strlen(sendmessage),0) < 0) { // If there isn't communication with the console exit
                    args->done = -1; 
                    return args;
                }
            }
        }
        pthread_mutex_unlock(&args->datanode->data->lock_node); 
        pthread_mutex_lock(&args->info->lockfreethread[args->index]); 
        args->info->freethread[args->index] = -2; // The thread is done
        pthread_mutex_unlock(&args->info->lockfreethread[args->index]); 
    }
    return args;
}

// Init the pool
void pool_init(threads_info** pooltemp, int totalthreads) {
    *pooltemp = malloc(sizeof(threads_info));
    if (*pooltemp == NULL) {
        perror("Failed to allocate thread pool struct");
        exit(EXIT_FAILURE);
    }
    threads_info* pool = *pooltemp;
    pool->threads = malloc(sizeof(pthread_t) * totalthreads); 
    pool->freethread = malloc(totalthreads * sizeof(int)); 
    pool->lockfreethread = malloc(totalthreads * sizeof(pthread_mutex_t));
    if (pool->threads == NULL || pool->freethread == NULL || pool->lockfreethread == NULL) {
        perror("malloc error");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(&pool->lock_logfile, NULL);
    pthread_mutex_init(&pool->datachanging, NULL); // Changes only on the total workers
    for (int i = 0; i < totalthreads; i++) {
        pool->freethread[i] = -1;  // Mark thread as free
        pthread_mutex_init(&pool->lockfreethread[i], NULL);
    }
    pool->totalrunning = 0;
}

// Free the thread pool
void freepool(threads_info** tempool) {
    threads_info* info = *tempool;
    pthread_mutex_destroy(&info->datachanging);
    for (int i = 0; i < informations.worker_limit ; i++) {
        pthread_mutex_destroy(&info->lockfreethread[i]);
    }
    free(info->lockfreethread);
    pthread_mutex_destroy(&info->lock_logfile); // Free the mutexes
    free(info->threads);
    free(info->freethread);
    if(info->logfile != NULL) {
        fclose(info->logfile); // Close the logfile
    }
    free(info);
}

// For errors free the queue
void freequeue() {
    queue* temp = headqueue;
    while(temp != NULL) {
        queue* next = temp->next;
        free(temp);
        temp = next;
    }
}

// Closes the program with error
void closeall(int client_socket,int server_fd,threads_info** pool) {
    threads_info* temp = *pool;
    if(client_socket != -1) { // Close the connection with the console
        shutdown(client_socket, SHUT_RDWR);
        close(client_socket);
    } 
    if(server_fd != -1) {
        close(server_fd);
    }
    freequeue(); // Free the queue
    freenode(); // Free the node and shutdown the clients
    freepool(&temp); // Free the pool thread
    exit(EXIT_FAILURE);
}

// Create the connection with the host and port
int create_connection(const char* host,int port) {
    int sockfd;
    struct sockaddr_in serv_addr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sockfd);
        return -1;
    }
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

int main(int argc,char* argv[]){
    if(argc != 11) {
        perror("Invalid Input");
        exit(EXIT_FAILURE);
    }
    if(readargs(argc,argv) == -1) { // Read the arguments
        exit(EXIT_FAILURE);
    }
    headnode = read_config(informations.config_file); // Read the config_file
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(informations.port_number);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        freenode();
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        freenode();
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);
    int client_socket = accept(server_fd, (struct sockaddr *)&client_address, &client_len); // Connect with the console
    if (client_socket < 0) {
        perror("accept failed");
        freenode();
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    int shutdown_flag = 0;
    node* last; // Store the pointer of the last node 
    node* current = headnode;
    while(current->next != NULL) { 
        current = current->next;
    }
    last = current; // Store the last pointer
    current = headnode; // Reading from the config_file
    threads_info* pool; // Init the thread pool which will have info for the threads
    pool_init(&pool, informations.worker_limit);  // Initialize thread pool 
    pool->logfile = fopen(informations.logfile,"a"); // Add the logfile pointer so the workers can write
    if(pool->logfile == NULL) {
        perror("error on files");
        closeall(client_socket,server_fd,&pool);
    }
    int done = 0; // This variable will help with knowing if the reading on the config_file is done
    char endbuffer[4 * BUFFER_SIZE];
    // Accept a commands from the console
    // Create the connection for all clients and spawn them
    while(current != NULL) {
        // Create the connections
        // Spawn the client for the source 
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            closeall(client_socket,server_fd,&pool);
        } else if (pid == 0) { 
            char port_str[10];
            snprintf(port_str, sizeof(port_str), "%d", current->data->target_port);
            execl("./nfs_client", "nfs_client", "-p",port_str, (char *)NULL);
            perror("execl failed");
            exit(EXIT_FAILURE);  
        }
        // Spawn the client for the target
        pid = fork();
        if (pid < 0) {
            perror("fork failed");
            closeall(client_socket,server_fd,&pool);
        } else if (pid == 0) { 
            char port_str[10];
            snprintf(port_str, sizeof(port_str), "%d", current->data->source_port);
            execl("./nfs_client", "nfs_client", "-p", port_str, (char *)NULL); // Exec the client with the right port
            perror("execl failed");
            exit(EXIT_FAILURE);  
        }
        sleep(5); //  Let the clients be spawned and then connect
        if((current->data->source_fd = create_connection(current->data->source_host,current->data->source_port)) == -1) {
            closeall(client_socket,server_fd,&pool);
        }
        if((current->data->target_fd = create_connection(current->data->target_host,current->data->target_port)) == -1) {
            closeall(client_socket,server_fd,&pool);
        }
        current = current->next;
    }
    current = headnode;
    while(1) { // Doing operations
        // Check in each loop if there is a thread which ended and change the variables
        for(int j = 0; j < informations.worker_limit; j++) {
            pthread_mutex_lock(&pool->lockfreethread[j]); 
            if(pool->freethread[j] == -2) { // It means that the thread j ended
                pool->freethread[j] = -1; // Make it available again
                pthread_mutex_unlock(&pool->lockfreethread[j]); 
                pthread_mutex_lock(&pool->datachanging); // Reduce the amount of running processes
                pool->totalrunning--;
                pthread_mutex_unlock(&pool->datachanging); 
                void* datathread = NULL;
                pthread_join(pool->threads[j], &datathread); // Clean the thread and get the return value
                thread_args* data = (thread_args* ) datathread;
                if(data->done == -1) { // No communication with the console close the program
                    closeall(client_socket,server_fd,&pool);
                }
                node* temp = data->datanode;
                if(temp->data->amount == temp->data->total_files) { // Full sync happened
                    char buffer[5 * BUFFER_SIZE];
                    char time[25];
                    get_time(time,sizeof(time));
                    char logmess[10 * BUFFER_SIZE];
                    pid_t tid = data->thread;
                    snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PULL] [SUCCESS] [%" PRIu32 " bytes pulled]",time,temp->data->source_dir,temp->data->source_host,temp->data->source_port,
                    temp->data->target_dir,temp->data->target_host,temp->data->target_port,tid,temp->data->totalbytes / 2);
                    pthread_mutex_lock(&pool->lock_logfile);
                    fprintf(pool->logfile,"%s\n", logmess); 
                    snprintf(logmess,10 * BUFFER_SIZE,"[%s] [/%s@%s:%d] [/%s@%s:%d] [%d] [PUSH] [SUCCESS] [%" PRIu32 " bytes pulled]",time,temp->data->source_dir,temp->data->source_host,temp->data->source_port,
                    temp->data->target_dir,temp->data->target_host,temp->data->target_port,tid,temp->data->totalbytes / 2);
                    fprintf(pool->logfile,"%s\n", logmess); 
                    pthread_mutex_unlock(&pool->lock_logfile);
                    snprintf(buffer,5 * BUFFER_SIZE,"[%s] Added file: %s@%s:%d -> %s@%s:%d\n",time,temp->data->source_dir,temp->data->source_host,temp->data->source_port,temp->data->target_dir,temp->data->target_host,temp->data->target_port);
                    printf("%s",buffer);
                    if(data->addcommand == 1) {  // Send message to console if needed 
                        if (send(client_socket,buffer, sizeof(buffer), 0) < 0) { // Send the message to the console if needed
                            perror("Failed to send message to the console");
                            closeall(client_socket,server_fd,&pool);
                        }
                    }
                    pthread_mutex_lock(&data->datanode->data->lock_node);
                    strcpy(data->datanode->data->last_sync_time,time); 
                    data->datanode->data->status = 1; // Full sync
                    data->datanode->data->amount++; // Do that so it doesn't print again the messages
                    pthread_mutex_unlock(&data->datanode->data->lock_node);
                }
                free(data); // Free the args which were passed
            }
            pthread_mutex_unlock(&pool->lockfreethread[j]); 
        }
        // Spawn from the queue
        while(headqueue != NULL && pool->totalrunning < informations.worker_limit) { // Do operations from the queue
            for(int j = 0; j < informations.worker_limit; j++) {
                pthread_mutex_lock(&pool->lockfreethread[j]); 
                if(pool->freethread[j] == -1) { // The j thread is free so spawn a thread for this filename
                    pool->freethread[j] = 1; // Change the status of the thread to on use
                    pthread_mutex_unlock(&pool->lockfreethread[j]); 
                    pthread_mutex_lock(&pool->datachanging); 
                    pool->totalrunning++; // Increase the total processes running
                    pthread_mutex_unlock(&pool->datachanging);
                    queue* queunode = headqueue; // Take the first data from the queue
                    thread_args* arguments = malloc(sizeof(thread_args)); // Create the arguments for the worker
                    if(arguments == NULL) {
                        perror("Error on allocating memory for arguments");
                        closeall(client_socket,server_fd,&pool);
                    }
                    arguments->datanode = queunode->data; // The node which has the data
                    arguments->index = j; //Index of the thread
                    arguments->info = pool; // Thread pool
                    arguments->socket = client_socket; // Communication with the console
                    arguments->done = 0; // If the thread was done right or got an error
                    arguments->addcommand = queunode->addcommand; // If it is an add then send an error message if something happens
                    strcpy(arguments->filename,queunode->filename); // The file which will happen the pull and the push
                    if (pthread_create(&pool->threads[j], NULL, worker_function, arguments) != 0) { // Create the Worker Thread which will sync the the file from one directory to another
                        perror("Failed to create thread");
                        closeall(client_socket,server_fd,&pool);
                    }
                    headqueue = headqueue->next;
                    free(queunode);// Free the queue node
                    if(headqueue != NULL) {
                        headqueue->previous = NULL; 
                    }
                    break;
                }
                pthread_mutex_unlock(&pool->lockfreethread[j]); 
            }
        }
        if(headqueue == NULL && shutdown_flag == 1 && done == 1) { // Done with doing operations from console and from the config_file
            // Check if all threads are done
            int notdone = 0;
            for(int i = 0; i < informations.worker_limit; i++) {
                pthread_mutex_lock(&pool->lockfreethread[i]); 
                if(pool->freethread[i] != -1) { // If it isn't 
                    pthread_mutex_unlock(&pool->lockfreethread[i]); 
                    notdone = 1;
                    break;
                }
                pthread_mutex_unlock(&pool->lockfreethread[i]); 
            }
            if(notdone == 1) { // Not all thread workers are done continue
                continue;
            }
            char time[25];
            get_time(time,sizeof(time));
            snprintf(endbuffer + strlen(endbuffer), sizeof(endbuffer) - strlen(endbuffer),"%s Manager shutdown complete.\n", time);
            printf("%s Manager shutdown complete.\n", time);
            if (send(client_socket, endbuffer, strlen(endbuffer), 0) == -1) {
                perror("Send failed");
                closeall(client_socket,server_fd,&pool);
            }
            break;
        }
        if(done == 0) { // Read from the config_file
            if(last == current) { // If it is true then the reading of the config_file is done 
                done = 1;
            }
            char command[4 * BUFFER_SIZE]; // Send the command LIST to the source client 
            snprintf(command, sizeof(command), "LIST %s", current->data->source_dir); // Creating the command LIST
            int length = strlen(command); 
            int net_length = htonl(length); // Convert the length to network byte 
            if (send(current->data->source_fd,&net_length, sizeof(int), 0) < 0) { // Send first the length of the command so the source client knows if he get it
                perror("Failed to send LIST command");
                closeall(client_socket,server_fd,&pool);
            }
            if (send(current->data->source_fd, command, length, 0) < 0) { // Then send the command LIST to the Source
                perror("Failed to send LIST command");
                closeall(client_socket,server_fd,&pool);
            }
            // Get the filenames
            char buffer[BUFFER_SIZE];
            int line_pos = 0;
            char line[BUFFER_SIZE];
            char filenames[10 * BUFFER_SIZE] = ""; // Here the names of the files will be stored
            int filenames_length = 0;
            ssize_t bytes_received;
            int total = 0; // It contains the total files 
            int done = 0;
            while (!done && (bytes_received = recv(current->data->source_fd, buffer, sizeof(buffer), 0)) > 0) {
                for (int i = 0; i < bytes_received; ++i) {
                    char c = buffer[i];
                    if (line_pos < BUFFER_SIZE - 1) {
                        line[line_pos++] = c;
                    }
                    if (c == '\n') {
                        line[line_pos] = '\0';  // Null-terminate the line
                        total++; // A filename was given or the dot
                        // Check space before appending
                        if (filenames_length + strlen(line) < sizeof(filenames)) {
                            strcat(filenames, line);  // Add line to filenames buffer
                            filenames_length += strlen(line);
                            if (strcmp(line, ".\n") == 0) { 
                                total--; // It was the .\n so delete one
                                done = 1;  // End marker found, stop reading
                                break;
                            }
                        } else {
                            done = 1;  // Buffer full, stop reading
                            break;
                        }
                        line_pos = 0;  // Reset for next line
                    }
                }
            }
            if (bytes_received < 0) {
                perror("recv failed");
                closeall(client_socket,server_fd,&pool);
            }
            filenames[filenames_length] = '\0';
            current->data->total_files = total; // Put the amount of files that the sync has
            // Delete all the files on the target directory 
            if(delete_files(current->data->target_dir) == -1) { // Error on delete
                shutdowneveryclient();
                closeall(client_socket,server_fd,&pool);
            }
            // For each line there will be a thread worker spawned
            char *filename = strtok(filenames, "\n"); // It contains the file
            while(filename != NULL && strcmp(filename, ".") != 0) { // Reading from the buffer until the dot is given
                if(pool->totalrunning < informations.worker_limit) { // Can spawn a thread
                    int i;
                    //Check if there is space to spawn a thread
                    for(i = 0; i < informations.worker_limit; i++) { // Find the thread which will be used (there is an available)
                        pthread_mutex_lock(&pool->lockfreethread[i]); 
                        if(pool->freethread[i] == -1) {
                            pool->freethread[i] = 1; // This thread is on use
                            pthread_mutex_unlock(&pool->lockfreethread[i]); 
                            pthread_mutex_lock(&pool->datachanging);
                            pool->totalrunning++; // A process was added
                            pthread_mutex_unlock(&pool->datachanging); 
                            break;
                        }
                        pthread_mutex_unlock(&pool->lockfreethread[i]); 
                    }
                    // Init the arguments
                    thread_args* arguments = malloc(sizeof(thread_args)); // Create the arguments for the worker
                    if(arguments == NULL) {
                        perror("Error on allocating memory for arguments");
                        close(server_fd);
                        close(client_socket);
                        freenode();
                        freepool(&pool);
                        close(client_socket);
                        exit(EXIT_FAILURE);
                    }
                    arguments->datanode = current; // The node which has the data
                    arguments->index = i; //Index of the thread
                    arguments->info = pool; // Thread pool
                    arguments->socket = client_socket; // Communication with the console
                    arguments->done = 0; 
                    arguments->addcommand = 0; // It isn't an add command
                    strcpy(arguments->filename,filename); // The file which will happen the pull and the push
                    if (pthread_create(&pool->threads[i], NULL, worker_function, arguments) != 0) { // Create the Worker Thread
                        perror("Failed to create thread");
                        shutdowneveryclient();
                        shutdown(client_socket, SHUT_RDWR);
                        close(client_socket);
                        close(server_fd);
                        freenode();
                        freepool(&pool);
                        exit(EXIT_FAILURE);
                    }
                }
                else { // Put it on Queue
                    addqueue(current,filename,0);
                }
                filename = strtok(NULL, "\n"); // It contains the file
            }
            current = current->next;
            if(current == NULL) { // Done reading from the config_file
                done = 1;
            }
        }
        if(shutdown_flag == 1) {// If the last command was shutdown the program doesn't get other commands from the console
            continue;
        }
        fd_set readfds; // For the select
        struct timeval timeout;
        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);
        timeout.tv_sec = 5; 
        timeout.tv_usec = 0;
        int ret = select(client_socket + 1, &readfds, NULL, NULL, &timeout); // Check if the console gave a command within a 5 sec timeout
        if(ret < 0) {
            perror("select error");
            closeall(client_socket,server_fd,&pool);
        }
        else if(ret == 0) { // Move on if there isn't an input
            continue;
        }
        if(FD_ISSET(client_socket, &readfds)) {
            char* inputfromconsole = read_line(client_socket); // Read what the console gave
            char* command = strtok(inputfromconsole," ");
            if(strcmp(command,"shutdown") == 0) { // Shutdown operation
                char time[25];
                get_time(time,sizeof(time));
                snprintf(endbuffer, sizeof(endbuffer),"%s Shutting down manager...\n",time);
                printf("%s Shutting down manager...\n",time);
                snprintf(endbuffer + strlen(endbuffer), sizeof(endbuffer) - strlen(endbuffer),"%s Waiting for all active workers to finish.\n", time);
                printf("%s Waiting for all active workers to finish.\n", time);
                snprintf(endbuffer + strlen(endbuffer), sizeof(endbuffer) - strlen(endbuffer),"%s Processing remaining queued tasks.\n", time);
                printf("%s Processing remaining queued tasks.\n", time);
                shutdown_flag = 1;
                continue;
            }
            else if(strcmp(command,"add") == 0) { // Add operation
                char* source = strtok(NULL," "); // Get the source
                char* arrow = strtok(NULL," "); // Get the arrow 
                char* target = strtok(NULL," "); // Get the target
                if(source == NULL || target == NULL || arrow == NULL) {
                    perror("strtok failed");
                    closeall(client_socket,server_fd,&pool);
                }
                char *dir, *host, *port_str;
                dir = strtok(source, "@"); // From the source get the info
                if (dir == NULL) {
                    perror("error on strtok");
                    closeall(client_socket,server_fd,&pool);
                }
                host = strtok(NULL, ":");
                if (host == NULL) {
                    perror("error on strtok");
                    closeall(client_socket,server_fd,&pool);
                }
                port_str = host + strlen(host) + 1;
                int port = atoi(port_str);
                char* dir2,*host2;
                dir2 = strtok(target, "@"); // From the target get the info
                if (dir2 == NULL) {
                    perror("error on strtok");
                    closeall(client_socket,server_fd,&pool);
                }
                host2 = strtok(NULL, ":");
                if (host2 == NULL) {
                    perror("error on strtok");
                    closeall(client_socket,server_fd,&pool);
                }
                port_str = host2 + strlen(host2) + 1;
                if (port_str == NULL) {
                    perror("error on strtok");
                    closeall(client_socket,server_fd,&pool);
                }
                int port2 = atoi(port_str);
                // Check if it is on the queue that pair
                queue* temp = headqueue;
                int found = 0;
                while(temp != NULL) {
                    if(strcmp(temp->data->data->source_dir,dir) == 0 && strcmp(temp->data->data->target_dir,dir2) == 0 && strcmp(temp->data->data->source_host,host) == 0 && strcmp(temp->data->data->target_host,host2) == 0 
                    && temp->data->data->source_port == port && temp->data->data->target_port == port2) {
                        found = 1;
                        break;
                    }
                    temp = temp->next;
                }
                if(found == 1) { // Send message to console if it is on queue
                    char message[3* BUFFER_SIZE];
                    char time[25];
                    get_time(time,sizeof(time));
                    snprintf(message,3 * BUFFER_SIZE,"[%s] Already in queue: %s@%s:%d\n",time,dir,host,port);
                    ssize_t bytes_sent = send(client_socket, message, strlen(message), 0);
                    if (bytes_sent < 0) {
                        perror("send failed");
                        closeall(client_socket,server_fd,&pool);
                        exit(EXIT_FAILURE);
                    }
                    continue;
                }
                node* temp2 = headnode;
                node* addoperation = NULL; // This will contains the node which has the data of the add operation
                while(temp2 != NULL) { // Check if this pair exists in the list
                    if(strcmp(temp2->data->source_dir,dir) == 0 && strcmp(temp2->data->target_dir,dir2) == 0 && strcmp(temp2->data->source_host,host) == 0 && strcmp(temp2->data->target_host,host2) == 0 
                    && temp2->data->source_port == port && temp2->data->target_port == port2) {
                        found = 1;
                        addoperation = temp2;
                        break;
                    }
                }
                if(found == 0) { // If it doesn't exist create the pair and add it to the list also spawn the clients if they don't exist
                    node* newnode = malloc(sizeof(node));
                    if(newnode == NULL) {
                        closeall(client_socket,server_fd,&pool);
                    }
                    newnode->data = create_sync_info(dir,dir2,host,host2,port,port2);
                    newnode->next = NULL;
                    temp2 = headnode;
                    while(temp2->next != NULL) { // Find the last
                        temp2 = temp2->next;
                    }
                    temp2->next = newnode;
                    addoperation = newnode; // Add it
                    // Spawn the source client
                    pid_t pid = fork();
                    if (pid < 0) {
                        perror("fork failed");
                        closeall(client_socket,server_fd,&pool);
                    } else if (pid == 0) { 
                        char port_str[10];
                        snprintf(port_str, sizeof(port_str), "%d", addoperation->data->source_port);
                        execl("./nfs_client","nfs_client","-p",port_str, NULL); //Exec the client with the right port
                        perror("execl failed");
                        exit(EXIT_FAILURE);  
                    }
                    // Spawn the target client
                    pid = fork();
                    if (pid < 0) {
                        perror("fork failed");
                        closeall(client_socket,server_fd,&pool);
                    } else if (pid == 0) { 
                        char port_str[10];
                        snprintf(port_str, sizeof(port_str), "%d", addoperation->data->target_port);
                        execl("./nfs_client","nfs_client","-p",port_str, NULL); //Exec the client with the right port
                        perror("execl failed");
                        exit(EXIT_FAILURE);  
                    }
                    // Create the connections for the source and target client
                    if((addoperation->data->source_fd = create_connection(addoperation->data->source_host,addoperation->data->source_port)) == -1) {
                        closeall(client_socket,server_fd,&pool);
                    }
                    if((addoperation->data->target_fd = create_connection(addoperation->data->target_host,addoperation->data->target_port)) == -1) {
                        closeall(client_socket,server_fd,&pool);
                        exit(EXIT_FAILURE);
                    }
                }
                //Send the command List to the Source
                char command[4 * BUFFER_SIZE]; // Send the command LIST to the source client
                snprintf(command, sizeof(command), "LIST %s",addoperation->data->source_dir); // Creating the command LIST
                int length = strlen(command);
                int net_length = htonl(length); // Convert the length to network byte 
                if (send(addoperation->data->source_fd,&net_length, sizeof(net_length), 0) < 0) { // Send first the length of the command so the source client knows if he get it
                    perror("Failed to send LIST command");
                    closeall(client_socket,server_fd,&pool);
                }
                if (send(addoperation->data->source_fd, command, strlen(command), 0) < 0) { // Then send the command LIST to the Source
                    perror("Failed to send LIST command");
                    closeall(client_socket,server_fd,&pool);
                }
                // Get the filenames
                char buffer[BUFFER_SIZE];
                int line_pos = 0;
                char line[BUFFER_SIZE];
                char filenames[10 * BUFFER_SIZE]; // Here the names of the files will be stored
                int filenames_length = 0;
                ssize_t bytes_received;
                int total = 0; // It contains the total files 
                int done = 0;
                while (!done && (bytes_received = recv(current->data->source_fd, buffer, sizeof(buffer), 0)) > 0) {
                    for (int i = 0; i < bytes_received; ++i) {
                        char c = buffer[i];
                        if (line_pos < BUFFER_SIZE - 1) {
                            line[line_pos++] = c;
                        }
                        if (c == '\n') {
                            line[line_pos] = '\0';  // Null-terminate the line
                            // Check space before appending
                            if (filenames_length + strlen(line) < sizeof(filenames)) {
                                strcat(filenames, line);  // Add line to filenames buffer
                                filenames_length += strlen(line);
                                total++;
                                if (strcmp(line, ".\n") == 0) { 
                                    total--;
                                    done = 1;  // End marker found, stop reading
                                    break;
                                }
                            } else {
                                done = 1;  // Buffer full, stop reading
                                break;
                            }
                            line_pos = 0;  // Reset for next line
                        }
                    }
                }
                if (bytes_received < 0) {
                    perror("recv failed");
                    closeall(client_socket,server_fd,&pool);
                }
                filenames[filenames_length] = '\0';
                addoperation->data->total_files = total; // The amount of files it has 
                // Delete all the files of the target directory
                if(delete_files(addoperation->data->target_dir) == -1) { // Error on delete
                    closeall(client_socket,server_fd,&pool);
                }
                char *filename = strtok(filenames, "\n"); // It contains the file
                if(filename == NULL) {
                    perror("error on strtok on filename");
                    closeall(client_socket,server_fd,&pool);
                }
                while(strcmp(filename, ".") != 0) { // Reading from the buffer until the dot is given
                    if(pool->totalrunning < informations.worker_limit) { // Can spawn a thread
                        int i;
                        for(i = 0; i < informations.worker_limit; i++) { // Find the thread which will be used
                            if(pool->freethread[i] == -1) {
                                pthread_mutex_lock(&pool->lockfreethread[i]);
                                pool->freethread[i] = 1; // This thread is on use
                                pthread_mutex_unlock(&pool->lockfreethread[i]);
                                pthread_mutex_lock(&pool->datachanging);//Check if there is space to spawn a thread
                                pool->totalrunning++; // A process was added
                                pthread_mutex_unlock(&pool->datachanging); 
                                break;
                            }
                        }
                        // Init the arguments
                        thread_args* arguments = malloc(sizeof(thread_args)); // Create the arguments for the worker
                        if(arguments == NULL) {
                            perror("Error on allocating memory for arguments");
                            closeall(client_socket,server_fd,&pool);
                        }
                        arguments->datanode = addoperation; // The node which has the data
                        arguments->index = i; //Index of the thread
                        arguments->info = pool; // Thread pool
                        arguments->socket = client_socket; // Communication with the console
                        arguments->done = 0;
                        arguments->addcommand = 1; // It is an add operation
                        strcpy(arguments->filename,filename); // The file which will happen the pull and the push
                        if (pthread_create(&pool->threads[i], NULL, worker_function, arguments) != 0) { // Create the Worker Thread
                            perror("Failed to create thread");
                            closeall(client_socket,server_fd,&pool);
                        }
                    }
                    else { // Put it on Queue
                        addqueue(addoperation,filename,1);
                    }
                    filename = strtok(NULL,"\n");
                    if(filename == NULL) {
                        perror("error on strtok on filename");
                        closeall(client_socket,server_fd,&pool);
                    }
                }
            }
            else { // Cancel operation
                char* source = strtok(NULL," "); // Get the source
                char *dir, *host, *port_str;
                dir = strtok(source, "@"); // From the source get the info
                if (dir == NULL) {
                    perror("error on strtok");
                    closeall(client_socket,server_fd,&pool);
                }
                host = strtok(NULL, ":");
                if (host == NULL) {
                    perror("error on strtok");
                    closeall(client_socket,server_fd,&pool);
                }
                port_str = host + strlen(host) + 1;
                int port = atoi(port_str);
                queue* queuedata = headqueue;
                int times = 0;
                while(queuedata != NULL) { // Check the Queue if there are an processes which have that source dir,host and port
                    if(strcmp(queuedata->data->data->source_dir,dir) == 0 && strcmp(queuedata->data->data->source_host,host) == 0 && queuedata->data->data->source_port == port) { // That source dir,host and port
                        // Now remove that operation from the queue
                        queue* next = queuedata->next;  // Save next before freeing
                        if (queuedata == headqueue) {
                            headqueue = next;
                        }
                        if (queuedata->previous != NULL) {
                            queuedata->previous->next = next;
                        }
                        if (next != NULL) {
                            next->previous = queuedata->previous;
                        }
                        free(queuedata);// The data it has i keep them
                        queuedata = next;
                        times++;
                        continue;
                    }
                    queuedata = queuedata->next;
                }
                if(times == 0) { // Wasn't on the Queue
                    char time[25];
                    get_time(time,sizeof(time));
                    char message[4 *BUFFER_SIZE];
                    snprintf(message,4 * BUFFER_SIZE,"[%s] Directory not being synchronized: %s@%s:%d\n",time,dir,host,port);
                    if (send(client_socket, message, strlen(message), 0) < 0) { // Send the message to the console
                        perror("Failed to send message to the console");
                        closeall(client_socket,server_fd,&pool);
                    }
                }
                else { // Was on the Queue
                    char time[25];
                    get_time(time,sizeof(time));
                    char message[4 *BUFFER_SIZE];
                    snprintf(message,4 * BUFFER_SIZE,"[%s] Synchronization stopped for %s@%s:%d\n",time,dir,host,port);
                    if (send(client_socket, message, strlen(message), 0) < 0) { // Send the message to the console
                        perror("Failed to send message to the console");
                        closeall(client_socket,server_fd,&pool);
                    }
                }
            }
        }
    }
    freenode(); // Free the items and also close the clients
    freepool(&pool);
    shutdown(client_socket, SHUT_RDWR); // Close the connection with the console
    close(client_socket);
    close(server_fd);
    exit(EXIT_FAILURE);
}