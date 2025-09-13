#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <sys/poll.h>
#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUFFER_SIZE 1024


//This struct stores the arguments of the command line 
typedef struct infoargs {
    char logfile[BUFFER_SIZE]; // The logfile of the manager
    char config_file[BUFFER_SIZE]; // The configure file
    int worker_limit; // How many workers there are 
} infoargs;

// Informations of the sync
typedef struct sync_info_mem_store{
    char source_dir[BUFFER_SIZE];
    char target_dir[BUFFER_SIZE]; // The directories
    int status; // The access of the directories
    char last_sync_time[25]; // The time of the last sync
    int active; // If the source is monitored
    int watch_fd; // Watch descriptor 
    int error_count; //The amount of errors
}sync_info_mem_store;

// Here the informations of the configure file will be stored and also the commands that will come from the console if they are sync and data
typedef struct node{
    sync_info_mem_store* data;
    struct node* next;
}node;

// Queue some reading from the config_file if it is neccesery
typedef struct queue {
    node* data;
    int type;// If it is for the configure or is an operation
    char operation[25]; // The operation that it will happen
    char file[BUFFER_SIZE]; // The file if the operation is delete or modify or add
    struct queue* next;
}queue;

// Stores info and communicatinos with the workers
typedef struct {
    pid_t pid; 
    int pipefd[2][2];  // 2 pipes for parent sends to child and child sends to parent
    char source[BUFFER_SIZE];  
    char target[BUFFER_SIZE]; // The directories
    int console; //This stores from which determined the operation -1 from inotify,0 from config_file and 1 from fss_console
    char operation[BUFFER_SIZE]; // The operation that it does
    char filename[BUFFER_SIZE]; // The name of the file which was changed if there is happened something on the inotify
} WorkerInfo;




// Initialize some global variables
WorkerInfo* running_workers = NULL; // Contains Informations of the running Workers
int totalprocess; // How many processes are running
infoargs informations; // The informations of the terminal
node* headnode = NULL;  
queue* queuedata = NULL; // The Queue
pid_t* ExitedPIDS; // Contains the PIDS fo the exited workers processes
int inotify_fd; // The inotify
int fd_read; // The fss_in
int fd_write; // The fss_out

//Free the node
void freenode(node** head) {
    node* temp = *head;
    while(temp != NULL) {
        node* next = temp->next;
        free(temp->data);
        free(temp);
        temp = next;
    }
    *head = NULL;
}

// Delete the pipes
void delete_pipes() {
    if (unlink("fss_in") == -1) {
        perror("Error deleting fss_in");
    }
    if (unlink("fss_out") == -1) {
        perror("Error deleting fss_out");
    }
}

//Get the time
void get_time(char *buffer, size_t size) {
    time_t raw_time;
    struct tm *time_info;
    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", time_info);
}


// Add to the queue a node
void addqueue(queue** data, node* data2,int x,char* operation, char* filename) {
    queue* new_queue = malloc(sizeof(queue));
    if (new_queue == NULL) {
        perror("malloc failed for queue");
        return;
    }
    new_queue->data = data2;
    new_queue->next = NULL;
    if (*data == NULL) {
        *data = new_queue;
    } else {
        queue* temp = *data;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = new_queue;
    }
    new_queue->type = x; // Store the type
    strcpy(new_queue->operation,operation);
    if(x == -1) {  
        strcpy(new_queue->file,filename);
    }
}

// Creates the 2 pipes
int create_pipes() {
    unlink("fss_in");
    unlink("fss_out");
    if (mkfifo("fss_in", 0666) == -1) {
        if (errno != EEXIST) {
            perror("Cannot create the fss_in");
            return -1;
        }
    }
    if (mkfifo("fss_out", 0666) == -1) {
        if (errno != EEXIST) {
            perror("Cannot create the fss_out");
            return -1;
        }
    }
    return 0;
}

// Return the next pair of the Queue
queue* takequeue(queue** head) {
    queue* temp = *head;
    *head = temp->next;
    return temp;
}

// Read the report the worker returned
void read_report(const char* report, int i) {
    const char *start = strstr(report, "EXEC_REPORT_START");
    const char *end = strstr(report, "EXEC_REPORT_END");
    if (!start || !end) {
        fprintf(stderr, "Invalid report format\n");
        return;
    }
    int status = -1;
    int copied = 0, skipped = 0, errors = 0;
    char operation[BUFFER_SIZE] = "";
    char temp[BUFFER_SIZE];
    char error_files[BUFFER_SIZE * 4] = "";
    int in_error_section = 0;
    char *content = strndup(start + strlen("EXEC_REPORT_START"), end - start - strlen("EXEC_REPORT_START"));
    if (!content) {
        perror("Memory allocation failed");
        return;
    }
    char *line = strtok(content, "\n");
    while (line != NULL) {
        if (strncmp(line, "STATUS:", 7) == 0) {
            if (strstr(line, "SUCCESS")) status = 0;
            else if (strstr(line, "PARTIAL")) status = 1;
            else status = -1;
        }
        else if (strncmp(line, "DETAILS:", 8) == 0) {
            if (sscanf(line, "DETAILS: %d files %[^,], %d skipped", &copied, operation, &skipped) != 3) {
                fprintf(stderr, "Failed to parse DETAILS line: %s\n", line);
                free(content);
                return;
            }
            operation[strcspn(operation, ",")] = '\0';  // remove any trailing comma
        }
        else if (strncmp(line, "ERRORS:", 7) == 0) {
            in_error_section = 1;
        }
        else if (in_error_section && strncmp(line, "- ", 2) == 0) {
            if (status != 0) {
                const char *colon = strchr(line, ':');
                if (colon && colon > line + 2) {
                    int len = colon - (line + 2);
                    if (len < BUFFER_SIZE - 1) {
                        strncpy(temp, line + 2, len);
                        temp[len] = '\0';
                        if (strlen(error_files) > 0) {
                            strncat(error_files, ", ", sizeof(error_files) - strlen(error_files) - 1);
                        }
                        strncat(error_files, temp, sizeof(error_files) - strlen(error_files) - 1);
                    }
                }
            }
            errors++;
        }

        line = strtok(NULL, "\n");
    }
    node* cur = headnode;
    while (cur != NULL) { // Find the source and target
        sync_info_mem_store* datatemp = cur->data;
        if (strcmp(datatemp->source_dir, running_workers[i].source) == 0 && strcmp(datatemp->target_dir, running_workers[i].target) == 0) {
            char time[25];
            get_time(time,sizeof(time));
            datatemp->error_count += errors;
            datatemp->status = status;
            char buffer[4 * BUFFER_SIZE];
            char logbuffer[8 * BUFFER_SIZE];
            if(running_workers[i].console != -1 ) { // From console or from reading config_file 
                if(status != -1) { //Succes or partial
                    get_time(time,sizeof(time));
                    if(strcmp(running_workers[i].operation,"sync") == 0 || strcmp(running_workers[i].operation,"syn1") == 0) {
                        int offset = snprintf(buffer, sizeof(buffer), "[%s] Added directory: %s -> %s\n",time,running_workers[i].source,running_workers[i].target); // Printing the message 
                        snprintf(buffer + offset, sizeof(buffer) - offset, "[%s] Monitoring started for %s\n",time,running_workers[i].source); // Storing the content which will it be going to the logfile
                    }
                    else {
                        int offset = snprintf(buffer,sizeof(buffer), "[%s] Syncing directory: %s -> %s\n",time,running_workers[i].source,running_workers[i].target);
                        snprintf(buffer + offset, sizeof(buffer) - offset, "[%s] Sync completed %s -> %s\n",time,running_workers[i].source,running_workers[i].target);
                    }
                    if(status == 0) { // Partial
                        snprintf(logbuffer,sizeof(logbuffer),"[%s] [%s] [%s] [%d] [FULL] [PARTIAL] [%d files copied, %d skipped]\n",time,datatemp->source_dir,datatemp->target_dir,running_workers[i].pid,copied,skipped);
                    }
                    else {
                        snprintf(logbuffer,sizeof(logbuffer),"[%s] [%s] [%s] [%d] [FULL] [SUCCESS] [%d files copied]\n",time,datatemp->source_dir,datatemp->target_dir,running_workers[i].pid,copied);
                    }
                    strcpy(cur->data->last_sync_time,time);
                    cur->data->active = 1;// This means that the directory source is monitored
                    int wd =  inotify_add_watch(inotify_fd,cur->data->source_dir, IN_CREATE | IN_MODIFY | IN_DELETE);//Monitor the dir
                    if (wd < 0) {
                        perror("inotify_add_watch");
                        exit(EXIT_FAILURE);
                    }
                    cur->data->watch_fd = wd;
                }
                else { // Got an error
                    get_time(time,sizeof(time));
                    if(strcmp(running_workers[i].operation,"sync") == 0 || strcmp(running_workers[i].operation,"syn1") == 0) {
                        int offset = snprintf(buffer, sizeof(buffer), "[%s] Didn't add directory: %s -> %s\n",time, running_workers[i].source, running_workers[i].target);
                        snprintf(buffer + offset, sizeof(buffer) - offset, "[%s] Monitoring didn't start for %s\n",time, running_workers[i].source);
                    }
                    else {
                        int offset = snprintf(buffer,sizeof(buffer), "[%s] Syncing didn't happen on directory: %s -> %s\n",time,running_workers[i].source,running_workers[i].target);
                        snprintf(buffer + offset, sizeof(buffer) - offset, "[%s] Sync didn't complete %s -> %s \n",time,running_workers[i].source,running_workers[i].target);
                    }
                    snprintf(logbuffer,sizeof(logbuffer),"[%s] [%s] [%s] [%d] [FULL] [ERROR] [0 files copied]\n",time,datatemp->source_dir,datatemp->target_dir,running_workers[i].pid);
                }
                printf("%s",buffer);
                if(running_workers[i].console == 1) { // If it is a console operation write it to the fss_out pipe
                    ssize_t bytes_written = write(fd_write, buffer, sizeof(buffer)); 
                    if (bytes_written == -1) {
                        perror("Write failed\n");
                        close(fd_read);
                        close(fd_write);
                        close(inotify_fd);
                        freenode(&headnode);
                        exit(EXIT_FAILURE);
                    }
                }
            }
            else { // From the inotify something has changed
                get_time(time,sizeof(time));
                if(status == 1) {
                    snprintf(logbuffer,sizeof(logbuffer),"[%s] [%s] [%s] [%d] [%s] [SUCCESS] [File: %s]\n",time,datatemp->source_dir,datatemp->target_dir,running_workers[i].pid,running_workers[i].operation,running_workers[i].filename);
                }
                else {
                    snprintf(logbuffer,sizeof(logbuffer),"[%s] [%s] [%s] [%d] [%s] [ERROR] [File: %s - Permission Denied]\n",time,datatemp->source_dir,datatemp->target_dir,running_workers[i].pid,running_workers[i].operation,running_workers[i].filename);
                }
            }
            cur->data->status = status; // See if the directories are active
            cur->data->error_count+= errors; // Add the errors
            FILE *log_file = fopen(informations.logfile, "a"); // Write the info on the logfile
            if (log_file) {
                fputs(logbuffer, log_file);
                fclose(log_file);
            } else {
                perror("Could not open log file");
            }
            break;  
        }
        cur = cur->next;
    }
}


// Signal Handling
void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { // Get the pid of the Exited worker process
        for(int i = 0; i < informations.worker_limit; i++) {
            if(ExitedPIDS[i] == -1) {
                ExitedPIDS[i] = pid;
                break;
            }
        }
    }
}


// Creates and initialize a sync_info_mem_store structure for storing infos
sync_info_mem_store* create_sync_info(char* source,char* target) {
    sync_info_mem_store* temp = malloc(sizeof(sync_info_mem_store));
    if(temp == NULL) {
        perror("Error on allocationg memory on struct sync_info_mem_store\n");
        return NULL;
    }
    strcpy(temp->source_dir, source);
    strcpy(temp->target_dir, target);  
    temp->error_count = 0;
    strcpy(temp->last_sync_time, "NONE");
    temp->status = 0;
    temp->active = 1;
    return temp;
}

// Reads the args of the terminal and stores them to the struct infoargs
int readargs(int argc, char* argv[], infoargs* informations) {
    informations->worker_limit = 5;  
    if (argc != 7) {
        perror("Erron on the args\n");
        return -1;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            strcpy(informations->logfile, argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            strcpy(informations->config_file, argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            informations->worker_limit = atoi(argv[i + 1]);
            i++;
        } else {
           perror("Wrong syntax on the args\n");
            return -1;
        }
    }
    if (informations->worker_limit <= 0 ||strlen(informations->config_file) == 0 ||strlen(informations->logfile) == 0) {
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
        // Store the pair in the linked list
        if(head == NULL) {
            head = malloc(sizeof(node));
            if(head == NULL) {
                perror("malloc failed for head");
                fclose(file);
                exit(EXIT_FAILURE);
            }
            head->data = create_sync_info(source, target);
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
            new_node->data = create_sync_info(source, target);
            new_node->next = NULL;
            temp->next = new_node;
        }
    }
    fclose(file);
    return head;
}




int main(int argc,char* argv[]) {
    struct sigaction sa; // Set up SIGCHLD handler
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if (create_pipes() != 0) { // Create the pipes
        close(inotify_fd);
        exit(EXIT_FAILURE);  
    }
    if ((inotify_fd = inotify_init1(IN_NONBLOCK)) == -1) {   // Do initialize the inotify and without blocking the read
        perror("inotify_init1");
        exit(EXIT_FAILURE);  
    }
    if(readargs(argc,argv,&informations) == -1) { // Read the args 
        close(inotify_fd);
        exit(EXIT_FAILURE);
    }
    // Create the struct in which there will be info of each worker which is running
    running_workers = malloc(informations.worker_limit * sizeof(WorkerInfo));
    ExitedPIDS = malloc(informations.worker_limit  * sizeof(pid_t)); // Here it stores the workers which ended
    for (int i = 0; i < informations.worker_limit; i++) {
        running_workers[i].pid = -1;
        ExitedPIDS[i] = -1; // That means that there isn't a worker there
        if(pipe(running_workers[i].pipefd[0]) == -1) {
            perror("pipe creation failed");
            close(inotify_fd);
            exit(EXIT_FAILURE);
        }
        if(pipe(running_workers[i].pipefd[1]) == -1) { 
            perror("pipe creation failed");
            close(inotify_fd);
            exit(EXIT_FAILURE);
        }
    }
    // Read the config_file
    if((headnode = read_config(informations.config_file)) == NULL) {
        close(inotify_fd);
        exit(EXIT_FAILURE);
    }
    node* current = headnode;
    queue* queuedata = NULL;
    fd_read = open("fss_in", O_RDONLY);  // Open the pipes with the correct way
    fd_write = open("fss_out", O_WRONLY);
    if (fd_write == -1 || fd_read == -1) {
        perror("Failed to open the pipes\n");
        delete_pipes();
        close(inotify_fd);
        exit(EXIT_FAILURE);
    }
    struct pollfd fds[3]; // Creating the poll
    fds[0].fd = fd_read;  // Reads from the fss_in
    fds[0].events = POLLIN; 
    fds[1].fd = inotify_fd;  // Reads the event from the monitoring
    fds[1].events = POLLIN;
    int shutdown = 0; // This variable will know if it is given the shutdown operation
    int time = 1;
    int donereading = 0; // If it is 1 the reading on the config_file is done
    char lastsource[BUFFER_SIZE];
    while(current->next != NULL) {
        current = current->next;
    }
    strcpy(lastsource,current->data->source_dir); // Get the last source file to know where the end is at the node
    current = headnode;
    char endbuffer[4 * BUFFER_SIZE];
    // Doing the operations
    while(1) { 
        for(int i = 0; i < informations.worker_limit; i++) { // Check if a worker ended
            if(ExitedPIDS[i] != - 1) { // There is a child which ended
                for(int j = 0; j < informations.worker_limit; j++) { // Find the process which did it
                    if (running_workers[j].pid == ExitedPIDS[i]) {
                        close(running_workers[j].pipefd[1][1]);
                        char buffer[BUFFER_SIZE];
                        ssize_t bytes = read(running_workers[j].pipefd[1][0], buffer, sizeof(buffer)-1); // Read the exec_report from the pipe
                        if (bytes <= 0) {
                            perror("Error on reading from the pipe\n");
                            exit(EXIT_FAILURE);
                        }
                        buffer[bytes] = '\0';
                        close(running_workers[j].pipefd[1][0]);
                        read_report(buffer,j); // This function will undertand the content of the report and it will send,write the correct messages
                        totalprocess--;  
                        running_workers[j].pid = -1;
                        ExitedPIDS[i] = -1; 
                        if(pipe(running_workers[j].pipefd[0]) == -1) {
                            perror("pipe creation failed");
                            close(inotify_fd);
                            exit(EXIT_FAILURE);
                        }
                        if(pipe(running_workers[j].pipefd[1]) == -1) { 
                            perror("pipe creation failed");
                            close(inotify_fd);
                            exit(EXIT_FAILURE);
                        }
                        break;
                    }
                }
                break;
            }
        }
        while(queuedata != NULL && totalprocess < informations.worker_limit){ // Do the operation the queue has first before moving on
            queue* cur = takequeue(&queuedata);
            int i = 0;
            while (i < informations.worker_limit && running_workers[i].pid != -1) { // Find the right place for the worker it has a free space
                i++;
            }
            if(strcmp(cur->operation,"sync") == 0 || strcmp(cur->operation,"sync1") || strcmp(cur->operation,"sync2")) { // The operation is sync
                pid_t pid = fork();  // Create the worker
                if (pid == 0) {  // Worker process
                    close(running_workers[i].pipefd[0][1]);
                    close(running_workers[i].pipefd[1][0]);
                    char buffer3[4 * BUFFER_SIZE];
                    ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); // Read from the parent
                    if (bytes < 0) {
                        perror("Worker failed to read from the pipe\n");
                        exit(EXIT_FAILURE);
                    }
                    buffer3[bytes] = '\0'; 
                    // Extract source and target directories from the buffer
                    char source[BUFFER_SIZE], target[BUFFER_SIZE];
                    char *token = strtok(buffer3, "\n");
                    if (token) {
                        while (*token == ' ') token++; 
                        strncpy(source, token, BUFFER_SIZE - 1);
                        source[BUFFER_SIZE - 1] = '\0';
                    }
                    token = strtok(NULL, "\n");
                    if (token) {
                        while (*token == ' ') token++;
                        strncpy(target, token, BUFFER_SIZE - 1);
                        target[BUFFER_SIZE - 1] = '\0';
                    }
                    if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                        perror("Worker: stdout redirection failed");
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[1][1]);
                    execl("./worker", "worker", source, target, "ALL", "FULL", NULL); // Execute the worker.c with the right operation
                    perror("execl error\n");  
                    exit(1);
                } else if (pid > 0) {  // Parent process
                    close(running_workers[i].pipefd[0][0]);
                    char buffer3[4 * BUFFER_SIZE]; // Send the source and the target dir to the worker process
                    strcpy(running_workers[i].source, cur->data->data->source_dir);
                    strcpy(running_workers[i].target, cur->data->data->target_dir);
                    snprintf(buffer3, sizeof(buffer3), "%s\n%s\n", running_workers[i].source, running_workers[i].target);
                    ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); // Write data to the pipe
                    if (write_bytes == -1) {
                        perror("Error on writing to the pipe for worker communication\n");
                        close(fd_read);
                        close(fd_write);
                        close(inotify_fd);
                        freenode(&headnode);
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[0][1]);  
                    running_workers[i].pid = pid;
                    strcpy(running_workers[i].operation,cur->operation);
                    if(strcmp(cur->operation,"sync") == 0) {
                        running_workers[i].console = 0;
                    }
                    else {
                        running_workers[i].console = 1;
                    }
                    totalprocess++;
                    ExitedPIDS[i] = pid;
                } else { // Error happened
                    perror("Fork failed\n");
                    close(fd_read);
                    close(fd_write);
                    close(inotify_fd);
                    freenode(&headnode);
                    exit(EXIT_FAILURE);
                }
            }
            else if(strcmp(cur->operation,"ADDED") == 0) { // Add a file which happened (the inotify detected it)
                pid_t pid = fork();  // Create the worker
                if (pid == 0) {  // Worker process
                    close(running_workers[i].pipefd[0][1]);
                    close(running_workers[i].pipefd[1][0]);
                    char buffer3[4 * BUFFER_SIZE];
                    ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); 
                    if (bytes < 0) {
                        perror("Worker failed to read from the pipe\n");
                        exit(EXIT_FAILURE);
                    }
                    buffer3[bytes] = '\0'; 
                    char source[BUFFER_SIZE], target[BUFFER_SIZE],filename[BUFFER_SIZE];
                    char *token = strtok(buffer3, "\n");
                    if (token) {
                        while (*token == ' ') token++; 
                        strncpy(source, token, BUFFER_SIZE - 1);
                        source[BUFFER_SIZE - 1] = '\0';
                    }
                    token = strtok(NULL, "\n");
                    if (token) {
                        while (*token == ' ') token++;
                        strncpy(target, token, BUFFER_SIZE - 1);
                        target[BUFFER_SIZE - 1] = '\0';
                    }
                    token = strtok(NULL, "\n");
                    if (token) {
                        while (*token == ' ') token++;
                        strncpy(filename, token, BUFFER_SIZE - 1);
                        filename[BUFFER_SIZE - 1] = '\0';
                    }
                    if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                        perror("Worker: stdout redirection failed");
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[1][1]);
                    execl("./worker", "worker", source, target, "ADDED",filename, NULL); // Execute the worker.c with the right way
                    perror("execl error\n");  
                    exit(1);
                } else if (pid > 0) {  // Parent process
                    close(running_workers[i].pipefd[0][0]);
                    char buffer3[4 * BUFFER_SIZE]; 
                    strcpy(running_workers[i].source, cur->data->data->source_dir);
                    strcpy(running_workers[i].target, cur->data->data->target_dir);
                    snprintf(buffer3, sizeof(buffer3), "%s\n%s\n%s\n", running_workers[i].source, running_workers[i].target,cur->file);
                    ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); 
                    if (write_bytes == -1) {
                        perror("Error on writing to the pipe for worker communication\n");
                        close(fd_read);
                        close(fd_write);
                        close(inotify_fd);
                        freenode(&headnode);
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[0][1]);  
                    running_workers[i].pid = pid;
                    strcpy(running_workers[i].operation,cur->operation);
                    running_workers[i].console = -1;
                    strcpy(running_workers[i].filename,cur->file);
                    totalprocess++;
                }
            }
            else if(strcmp(cur->operation,"DELETED") == 0) { // Delete a file which happened 
                pid_t pid = fork();  
                if (pid == 0) {  
                    close(running_workers[i].pipefd[0][1]);
                    close(running_workers[i].pipefd[1][0]);
                    char buffer3[4 * BUFFER_SIZE];
                    ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); 
                    if (bytes < 0) {
                        perror("Worker failed to read from the pipe\n");
                        exit(EXIT_FAILURE);
                    }
                    buffer3[bytes] = '\0'; 
                    char source[BUFFER_SIZE], target[BUFFER_SIZE],filename[BUFFER_SIZE];
                    char *token = strtok(buffer3, "\n");
                    if (token) {
                        while (*token == ' ') token++; 
                        strncpy(source, token, BUFFER_SIZE - 1);
                        source[BUFFER_SIZE - 1] = '\0';
                    }
                    token = strtok(NULL, "\n");
                    if (token) {
                        while (*token == ' ') token++;
                        strncpy(target, token, BUFFER_SIZE - 1);
                        target[BUFFER_SIZE - 1] = '\0';
                    }
                    token = strtok(NULL, "\n");
                    if (token) {
                        while (*token == ' ') token++;
                        strncpy(filename, token, BUFFER_SIZE - 1);
                        filename[BUFFER_SIZE - 1] = '\0';
                    }
                    if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                        perror("Worker: stdout redirection failed");
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[1][1]);
                    execl("./worker", "worker", source, target, "DELETED",filename, NULL); // Execute the worker.c
                    perror("execl error\n");  
                    exit(1);
                } else if (pid > 0) { 
                    close(running_workers[i].pipefd[0][0]);
                    char buffer3[4 * BUFFER_SIZE]; 
                    strcpy(running_workers[i].source, cur->data->data->source_dir);
                    strcpy(running_workers[i].target, cur->data->data->target_dir);
                    snprintf(buffer3, sizeof(buffer3), "%s\n%s\n%s\n", running_workers[i].source, running_workers[i].target,cur->file);
                    ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); 
                    if (write_bytes == -1) {
                        perror("Error on writing to the pipe for worker communication\n");
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[0][1]);  
                    running_workers[i].pid = pid;
                    strcpy(running_workers[i].operation,cur->operation);
                    running_workers[i].console = -1;
                    strcpy(running_workers[i].filename,cur->file);
                    totalprocess++;
                }
            }
            else  { // Modify a file which happened 
                pid_t pid = fork(); 
                if (pid == 0) {  
                    close(running_workers[i].pipefd[0][1]);
                    close(running_workers[i].pipefd[1][0]);
                    char buffer3[4 * BUFFER_SIZE];
                    ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); 
                    if (bytes < 0) {
                        perror("Worker failed to read from the pipe\n");
                        exit(EXIT_FAILURE);
                    }
                    buffer3[bytes] = '\0'; 
                    char source[BUFFER_SIZE], target[BUFFER_SIZE],filename[BUFFER_SIZE];
                    char *token = strtok(buffer3, "\n");
                    if (token) {
                        while (*token == ' ') token++; 
                        strncpy(source, token, BUFFER_SIZE - 1);
                        source[BUFFER_SIZE - 1] = '\0';
                    }
                    token = strtok(NULL, "\n");
                    if (token) {
                        while (*token == ' ') token++;
                        strncpy(target, token, BUFFER_SIZE - 1);
                        target[BUFFER_SIZE - 1] = '\0';
                    }
                    token = strtok(NULL, "\n");
                    if (token) {
                        while (*token == ' ') token++;
                        strncpy(filename, token, BUFFER_SIZE - 1);
                        filename[BUFFER_SIZE - 1] = '\0';
                    }
                    if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                        perror("Worker: stdout redirection failed");
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[1][1]);
                    execl("./worker", "worker", source, target, "MODIFIED",filename, NULL); 
                    perror("execl error\n");  
                    exit(1);
                } else if (pid > 0) {  
                    close(running_workers[i].pipefd[0][0]);
                    char buffer3[4 * BUFFER_SIZE]; 
                    strcpy(running_workers[i].source, cur->data->data->source_dir);
                    strcpy(running_workers[i].target, cur->data->data->target_dir);
                    snprintf(buffer3, sizeof(buffer3), "%s\n%s\n%s\n", running_workers[i].source, running_workers[i].target,cur->file);
                    ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); 
                    if (write_bytes == -1) {
                        perror("Error on writing to the pipe for worker communication\n");
                        close(fd_read);
                        close(fd_write);
                        close(inotify_fd);
                        freenode(&headnode);
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[0][1]);  
                    running_workers[i].pid = pid;
                    strcpy(running_workers[i].operation,cur->operation);
                    running_workers[i].console = -1;
                    strcpy(running_workers[i].filename,cur->file);
                    totalprocess++;
                }
            }
            free(cur);
        }
        if(shutdown == 1) { // If we have shutdown we have to run the remaining tasks
            if(current != NULL && donereading != 1) { // Reading from the config_file and if i find something which is already monitored it means that it was from an operation
                if(strcmp(lastsource,current->data->source_dir) == 0) { // The last item
                    donereading = 1;
                }
                int i = 0;
                while (i < informations.worker_limit && running_workers[i].pid != -1) { // Find the right place for the worker
                    i++;
                }
                if (i == informations.worker_limit) {   // If there are no free slots for workers, queue the current node
                    addqueue(&queuedata, current,0,"sync",NULL);
                    current = current->next;
                    continue;
                }
                pid_t pid = fork();  // Create the worker
                if (pid == 0) {  // Worker process
                    close(running_workers[i].pipefd[0][1]);
                    close(running_workers[i].pipefd[1][0]);
                    char buffer3[4 * BUFFER_SIZE];
                    ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); // Read from the parent
                    if (bytes < 0) {
                        perror("Worker failed to read from the pipe\n");
                        exit(EXIT_FAILURE);
                    }
                    buffer3[bytes] = '\0'; 
                    // Extract source and target directories from the buffer
                    char source[BUFFER_SIZE], target[BUFFER_SIZE];
                    char *token = strtok(buffer3, "\n");
                    if (token) {
                        while (*token == ' ') token++; 
                        strncpy(source, token, BUFFER_SIZE - 1);
                        source[BUFFER_SIZE - 1] = '\0';
                    }
                    token = strtok(NULL, "\n");
                    if (token) {
                        while (*token == ' ') token++;
                        strncpy(target, token, BUFFER_SIZE - 1);
                        target[BUFFER_SIZE - 1] = '\0';
                    }
                    if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                        perror("Worker: stdout redirection failed");
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[1][1]);
                    execl("./worker", "worker", source, target, "ALL", "FULL", NULL); // Execute the worker.c
                    perror("execl error\n");  
                    exit(EXIT_FAILURE);
                } else if (pid > 0) {  // Parent process
                    close(running_workers[i].pipefd[0][0]);
                    char buffer3[4 * BUFFER_SIZE]; // Send the source and the target dir to the worker process
                    strcpy(running_workers[i].source, current->data->source_dir);
                    strcpy(running_workers[i].target, current->data->target_dir);
                    snprintf(buffer3, sizeof(buffer3), "%s\n%s\n", running_workers[i].source, running_workers[i].target);
                    ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); // Write data to the pipe
                    if (write_bytes == -1) {
                        perror("Error on writing to the pipe for worker communication\n");
                        close(fd_read);
                        close(fd_write);
                        close(inotify_fd);
                        freenode(&headnode);
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[0][1]); 
                    running_workers[i].console = 0;
                    strcpy(running_workers[i].operation,"sync");
                    running_workers[i].pid = pid;
                    totalprocess++;
                } else {
                    perror("Fork failed\n");
                    close(fd_read);
                    close(fd_write);
                    close(inotify_fd);
                    freenode(&headnode);
                    exit(EXIT_FAILURE);
                }
                current = current->next;
                continue;
            }
            if(queuedata == NULL && donereading == 1) { // The Queue is done and reading the config_file is done
                char time[25];
                get_time(time,sizeof(time));
                snprintf(endbuffer + strlen(endbuffer), sizeof(endbuffer) - strlen(endbuffer),"%s Manager shutdown complete.\n", time);
                printf("%s Manager shutdown complete.\n", time);
                ssize_t bytes_written = write(fd_write,endbuffer, sizeof(endbuffer)); // Write to the fss_out the result
                if (bytes_written == -1) {
                    perror("Write failed\n");
                    close(fd_read);
                    close(fd_write);
                    close(inotify_fd);
                    freenode(&headnode);
                    exit(EXIT_FAILURE);
                }
            }
            continue;
        }
        int ret; 
        if(time == 1) { // Add something before it get inputs from the console
            ret = 0;
            time = 0; 
        }
        else {
            ret = poll(fds, 2, 1); 
        }
        if(ret == -1) {
            if (errno == EINTR) {// Interrupted by SIGCHLD signal
                continue;
            }
            else {
                perror("Error on poll\n");
                exit(EXIT_FAILURE);
            }
        }
        if(ret == 0) { // I read from the config_file if there isnt an input
            if(current != NULL && donereading == 0) { // Reading from the config_file 
                if(strcmp(lastsource,current->data->source_dir) == 0) { // The last item
                    donereading = 1;
                }
                int i = 0;
                while (i < informations.worker_limit && running_workers[i].pid != -1) { // Find the right place for the worker
                    i++;
                }
                if (i == informations.worker_limit) {   // If there are no free slots for workers, queue the current node
                    addqueue(&queuedata, current,0,"sync",NULL);
                    current = current->next;
                    continue;
                }
                pid_t pid = fork();  // Create the worker
                if (pid == 0) {  // Worker process
                    close(running_workers[i].pipefd[0][1]);
                    close(running_workers[i].pipefd[1][0]);
                    char buffer3[4 * BUFFER_SIZE];
                    ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); // Read from the parent
                    if (bytes < 0) {
                        perror("Worker failed to read from the pipe\n");
                        exit(EXIT_FAILURE);
                    }
                    buffer3[bytes] = '\0'; 
                    char source[BUFFER_SIZE], target[BUFFER_SIZE];   // Extract source and target directories from the buffer
                    char *token = strtok(buffer3, "\n");
                    if (token) {
                        while (*token == ' ') token++; 
                        strncpy(source, token, BUFFER_SIZE - 1);
                        source[BUFFER_SIZE - 1] = '\0';
                    }
                    token = strtok(NULL, "\n");
                    if (token) {
                        while (*token == ' ') token++;
                        strncpy(target, token, BUFFER_SIZE - 1);
                        target[BUFFER_SIZE - 1] = '\0';
                    }
                    if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                        perror("Worker: stdout redirection failed");
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[1][1]);
                    execl("./worker", "worker", source, target, "ALL", "FULL", NULL); // Execute the worker.c for FULL SYNC
                    perror("execl error\n");  
                    exit(EXIT_FAILURE);
                } else if (pid > 0) {  // Parent process
                    close(running_workers[i].pipefd[0][0]);
                    char buffer3[4 * BUFFER_SIZE]; // Send the source and the target dir to the worker process
                    strcpy(running_workers[i].source, current->data->source_dir);
                    strcpy(running_workers[i].target, current->data->target_dir);
                    snprintf(buffer3, sizeof(buffer3), "%s\n%s\n", running_workers[i].source, running_workers[i].target);
                    ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); // Write data to the pipe
                    if (write_bytes == -1) {
                        perror("Error on writing to the pipe for worker communication\n");
                        close(fd_read);
                        close(fd_write);
                        close(inotify_fd);
                        freenode(&headnode);
                        exit(EXIT_FAILURE);
                    }
                    close(running_workers[i].pipefd[0][1]);  
                    running_workers[i].pid = pid;
                    running_workers[i].console = 0; // It is reading a config_file
                    strcpy(running_workers[i].operation,"sync"); // Doing FULL SYNC
                    totalprocess++;
                } else {
                    perror("Fork failed\n");
                    close(fd_read);
                    close(fd_write);
                    close(inotify_fd);
                    freenode(&headnode);
                    exit(EXIT_FAILURE);
                }
                current = current->next;
                if(current == NULL) { // It means that we are done reading 
                    donereading = 1;
                }
            }
            continue;
        }
        if (fds[0].revents & POLLIN) { // Got input from the fss_in and now it does the command
            char buffer[4 * BUFFER_SIZE] ; // This will contain the content which will be given to fss_out
            buffer[0] = '\0';
            char buffer2[BUFFER_SIZE]; // This stores the content of the pipe fss_in
            ssize_t bytes_read = read(fd_read, buffer2, sizeof(buffer2) - 1); // Read the command
            if (bytes_read == -1) {
                perror("Failed to open fss_in");
                close(fd_read);
                close(fd_write);
                close(inotify_fd);
                freenode(&headnode);
                exit(EXIT_FAILURE);
            }
            else {
                buffer2[bytes_read] = '\0';
            }
            char *command = strtok(buffer2, " ");  // Extract first word which is the command
            if (command == NULL) {
                perror("Invalid input");
                close(fd_read);
                close(fd_write);
                close(inotify_fd);
                freenode(&headnode);
                exit(EXIT_FAILURE);
            }
            if(strcmp(command,"shutdown") == 0) { // Shutdown the program and print the right messages
                char time[25];
                get_time(time,sizeof(time));
                snprintf(endbuffer, sizeof(endbuffer),"%s Shutting down manager...\n",time);
                printf("%s Shutting down manager...\n",time);
                snprintf(endbuffer + strlen(endbuffer), sizeof(endbuffer) - strlen(endbuffer),"%s Waiting for all active workers to finish.\n", time);
                printf("%s Waiting for all active workers to finish.\n", time);
                snprintf(endbuffer + strlen(endbuffer), sizeof(endbuffer) - strlen(endbuffer),"%s Processing remaining queued tasks.\n", time);
                printf("%s Processing remaining queued tasks.\n", time);
                shutdown = 1;
                continue;
            }
            else if(strcmp(command,"add") == 0) { // Sync 2 directories
                char source[BUFFER_SIZE];
                char target[BUFFER_SIZE];
                char *src_token = strtok(NULL, " ");
                char *tgt_token = strtok(NULL, " ");
                if (src_token == NULL || tgt_token == NULL) {
                    fprintf(stderr, "Missing source or target in input.\n");
                    close(fd_read);
                    close(fd_write);
                    close(inotify_fd);
                    freenode(&headnode);
                    exit(EXIT_FAILURE);
                }
                strncpy(source, src_token, BUFFER_SIZE - 1);
                source[BUFFER_SIZE - 1] = '\0'; 
                strncpy(target, tgt_token, BUFFER_SIZE - 1);
                target[BUFFER_SIZE - 1] = '\0';   // Get the Source and the Target dir
                node* cur = headnode;
                int found = 0;
                while(cur != NULL) { // If it is on the config_file Don't do the Sync
                    if(strcmp(source,cur->data->source_dir) == 0 && strcmp(target,cur->data->target_dir) == 0 && cur->data->active == 1) {
                        char time[25];
                        get_time(time,sizeof(time));
                        snprintf(buffer, 4 * BUFFER_SIZE, "%s Already in queue: %s", time, source);
                        printf("%s",buffer);
                        ssize_t bytes_written = write(fd_write, buffer, sizeof(buffer)); // Write to the fss_out the result and send to the console
                        if (bytes_written == -1) {
                            perror("Write failed\n");
                            close(fd_read);
                            close(fd_write);
                            close(inotify_fd);
                            freenode(&headnode);
                            exit(EXIT_FAILURE);
                        }
                        found = 1;
                        break;
                    }
                    cur = cur->next;
                }
                if(found == 0) { // Do the fyll sync
                    node* temp = malloc(sizeof(node)); // Add the new fyll sync to the list
                    temp->data = create_sync_info(source,target);
                    temp->next = NULL;
                    node* cur = headnode;
                    while(cur->next != NULL) {
                        cur = cur->next;
                    }
                    cur->next = temp; // Add to the nodes the new sync 
                    int i = 0;
                    while (i < informations.worker_limit && running_workers[i].pid != -1) { // Find the right place for the worker
                        i++;
                    }
                    if (i == informations.worker_limit) {   // If there are no free slots for workers, queue the current node
                        addqueue(&queuedata, temp,1,"sync1", NULL); 
                        continue;
                    }
                    pid_t pid = fork();  // Create the worker
                    if (pid == 0) {  // Worker process
                        close(running_workers[i].pipefd[0][1]);
                        close(running_workers[i].pipefd[1][0]);
                        char buffer3[4 * BUFFER_SIZE];
                        ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); // Read from the parent
                        if (bytes < 0) {
                            perror("Worker failed to read from the pipe\n");
                            exit(EXIT_FAILURE);
                        }
                        buffer3[bytes] = '\0'; 
                        // Extract source and target directories from the buffer
                        char source[BUFFER_SIZE], target[BUFFER_SIZE];
                        char *token = strtok(buffer3, "\n");
                        if (token) {
                            while (*token == ' ') token++; 
                            strncpy(source, token, BUFFER_SIZE - 1);
                            source[BUFFER_SIZE - 1] = '\0';
                        }
                        token = strtok(NULL, "\n");
                        if (token) {
                            while (*token == ' ') token++;
                            strncpy(target, token, BUFFER_SIZE - 1);
                            target[BUFFER_SIZE - 1] = '\0';
                        }
                        if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                            perror("Worker: stdout redirection failed");
                            exit(EXIT_FAILURE);
                        }
                        close(running_workers[i].pipefd[1][1]);
                        execl("./worker", "worker", source, target, "ALL", "FULL", NULL); // Execute the worker.c
                        perror("execl error\n");  
                        exit(EXIT_FAILURE);
                    } else if (pid > 0) {  // Parent process
                        close(running_workers[i].pipefd[0][0]);
                        char buffer3[4 * BUFFER_SIZE]; // Send the source and the target dir to the worker process
                        strcpy(running_workers[i].source, temp->data->source_dir);
                        strcpy(running_workers[i].target, temp->data->target_dir);
                        snprintf(buffer3, sizeof(buffer3), "%s\n%s\n", running_workers[i].source, running_workers[i].target);
                        ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); // Write data to the pipe
                        if (write_bytes == -1) {
                            perror("Error on writing to the pipe for worker communication\n");
                            close(fd_read);
                            close(fd_write);
                            close(inotify_fd);
                            freenode(&headnode);
                            exit(EXIT_FAILURE);
                        }
                        close(running_workers[i].pipefd[0][1]);  
                        running_workers[i].pid = pid;
                        running_workers[i].console = 1;
                        strcpy(running_workers[i].operation,"sync1");
                        totalprocess++;
                    } else {
                        perror("Fork failed\n");
                        close(fd_read);
                        close(fd_write);
                        close(inotify_fd);
                        freenode(&headnode);
                        exit(EXIT_FAILURE);
                    }
                }
            }
            else if(strcmp(command,"status") == 0) { // Status for a directory
                char *source = strtok(NULL, " "); // Take the next word
                if(source == NULL) {
                    perror("Error on reading the pipe");
                    exit(EXIT_FAILURE);
                }
                node* cur = headnode;
                char time[25];
                while(cur != NULL) { // Check the list
                    if(strcmp(cur->data->source_dir, source) == 0) {
                        if (cur->data->active != 1) { // Find the correct message 
                            get_time(time, sizeof(time));
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"[%s] Directory not monitored: %s\n", time, source);
                        }
                        else {
                            get_time(time, sizeof(time));
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"[%s] Status requested for %s\n", time, source);
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"Directory: %s\n", source);
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"Target: %s", cur->data->target_dir);
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"Last Sync: %s\n", cur->data->last_sync_time);
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"Errors: %d\n", cur->data->error_count);
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"Status: Active\n");
                        }
                        break;
                    }
                    else {
                        cur = cur->next;
                    }
                }
                if(cur == NULL) { // Directory not found
                    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"Directory not found\n");
                    printf("%s\n",buffer);
                }
                printf("%s\n",buffer);
            }
            else if(strcmp(command,"cancel") == 0) { // Cancel the monitoring
                char *source = strtok(NULL, " ");
                node* cur = headnode;
                char time[25];
                while(cur != NULL) {
                   if(strcmp(cur->data->source_dir,source) == 0) {
                        if(cur->data->active == 1) {
                            int result = inotify_rm_watch(inotify_fd, cur->data->watch_fd); // Removing monitoring from one directory
                            if (result == -1) {
                                perror("inotify_rm_watch failed");
                                close(fd_read);
                                close(fd_write);
                                close(inotify_fd);
                                freenode(&headnode);
                                exit(EXIT_FAILURE);
                            }
                            cur->data->active = 0;
                            get_time(time, sizeof(time));
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"%s Monitoring stopped for  %s\n", time, source);
                            printf("%s",buffer);
                        }
                        else {
                            get_time(time, sizeof(time));
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"%s Directory not monitored:  %s\n", time, source);
                            printf("%s",buffer);
                        }
                        break;
                   }
                }
            }
            else if(strcmp(command,"sync") == 0) { // Sync a directory
                char source[BUFFER_SIZE];
                char *src_token = strtok(NULL, " ");
                if (src_token == NULL) {
                    perror("Missing source or target in input.\n");
                    close(fd_read);
                    close(fd_write);
                    close(inotify_fd);
                    freenode(&headnode);
                    exit(EXIT_FAILURE);
                }
                strncpy(source, src_token, BUFFER_SIZE - 1);
                source[BUFFER_SIZE - 1] = '\0';  
                node* cur = headnode;
                char time[25];
                int queueornot = 0;
                for(int i = 0; i < informations.worker_limit; i++) { // Check if the sync is happening right now
                    if(running_workers[i].pid != -1) {
                        if(strcmp(running_workers[i].source,source) == 0) {
                            get_time(time,sizeof(time));
                            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"%s Sync already in progress  %s\n", time, source);
                            queueornot = 1;
                        }
                    }
                }
                while(cur != NULL && queueornot != 1) {
                    if(strcmp(cur->data->source_dir,source) == 0) {
                        int i = 0;
                        while (i < informations.worker_limit && running_workers[i].pid != -1) { // Find the right place for the worker
                            i++;
                        }
                        if (i == informations.worker_limit) {   // If there are no free slots for workers, queue the current node
                            addqueue(&queuedata,cur,1,"sync2",NULL);
                            break; // Break from the loop because it is on the queue
                        }
                        pid_t pid = fork();  // Create the worker
                        if (pid == 0) {  // Worker process
                            close(running_workers[i].pipefd[0][1]);
                            close(running_workers[i].pipefd[1][0]);
                            char buffer3[4 * BUFFER_SIZE];
                            ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); 
                            if (bytes < 0) {
                                exit(EXIT_FAILURE);
                            }
                            buffer3[bytes] = '\0'; 
                            char source[BUFFER_SIZE], target[BUFFER_SIZE];
                            char *token = strtok(buffer3, "\n");
                            if (token) {
                                while (*token == ' ') token++; 
                                strncpy(source, token, BUFFER_SIZE - 1);
                                source[BUFFER_SIZE - 1] = '\0';
                            }
                            token = strtok(NULL, "\n");
                            if (token) {
                                while (*token == ' ') token++;
                                strncpy(target, token, BUFFER_SIZE - 1);
                                target[BUFFER_SIZE - 1] = '\0';
                            }
                            if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                                perror("Worker: stdout redirection failed");
                                exit(EXIT_FAILURE);
                            }
                            close(running_workers[i].pipefd[1][1]);
                            execl("./worker", "worker", source, target, "ALL", "FULL", NULL); // Execute the worker.c
                            perror("execl error\n");  
                            exit(1);
                        } else if (pid > 0) {  // Parent process
                            close(running_workers[i].pipefd[0][0]);
                            char buffer3[4 * BUFFER_SIZE]; 
                            strcpy(running_workers[i].source, cur->data->source_dir);
                            strcpy(running_workers[i].target, cur->data->target_dir);
                            snprintf(buffer3, sizeof(buffer3), "%s\n%s\n", running_workers[i].source, running_workers[i].target);
                            ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); 
                            if (write_bytes == -1) {
                                perror("Error on writing to the pipe for worker communication\n");
                                close(fd_write);
                                close(inotify_fd);
                                freenode(&headnode);
                                
                                exit(EXIT_FAILURE);
                            }
                            close(running_workers[i].pipefd[0][1]);  
                            running_workers[i].pid = pid;
                            running_workers[i].console = 1;
                            strcpy(running_workers[i].operation,"sync2");
                            totalprocess++;
                        } else {
                            perror("Fork failed\n");
                            close(fd_write);
                            close(inotify_fd);
                            freenode(&headnode);
                            exit(EXIT_FAILURE);
                        }
                        break;
                    }
                    cur = cur->next;
                }
                if(cur == NULL) {
                    char time[25];
                    get_time(time,sizeof(time));
                    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer),"%s The directory hasn't been found %s \n", time,source );
                }
                if(queueornot == 0 && cur != NULL){ // Add the condition so after the manager send the error message
                    continue;
                }
            }
            else { // Error
                perror("Invalid command was given\n");
                close(fd_read);
                close(fd_write);
                close(inotify_fd);
                freenode(&headnode);
                exit(EXIT_FAILURE);
            }
            continue;
        }
        if(fds[0].revents & POLLIN) { // Something was changed in the monitoring
            char buffer[BUFFER_SIZE];
            int length = read(inotify_fd, buffer, sizeof(buffer));
            if (length == -1) {
                perror("Error on reading the inotify events");
                close(fd_read);
                close(fd_write);
                close(inotify_fd);
                freenode(&headnode);      
                exit(EXIT_FAILURE);
            } 
            // Process events
            for (char *ptr = buffer; ptr < buffer + length; ) { //Get the events that happpened
                struct inotify_event *event = (struct inotify_event *)ptr;
                // Determine which directory this event belongs to based on the watch_fd
                node* cur = headnode;
                while(cur != NULL) {
                    if(cur->data->watch_fd == event->wd) {
                        break;
                    }
                    cur = cur->next;
                }
                if(cur == NULL) {
                    fprintf(stderr, "Warning: Unknown watch descriptor %d\n", event->wd);
                    close(fd_read);
                    close(fd_write);
                    close(inotify_fd);
                    freenode(&headnode);
                    exit(EXIT_FAILURE);
                }
                // Handle the event
                int i = 0;
                while (i < informations.worker_limit && running_workers[i].pid != -1) { // Find the right place for the worker
                    i++;
                }
                if (event->len) {
                    if (event->mask & IN_CREATE) {
                        if(i == informations.worker_limit) { // Add to the Queue
                            addqueue(&queuedata,cur,-1,"ADDED",event->name);
                        }
                        else {
                            pid_t pid = fork();  // Create the worker
                            if (pid == 0) {  // Worker process
                                close(running_workers[i].pipefd[0][1]);
                                close(running_workers[i].pipefd[1][0]);
                                char buffer3[4 * BUFFER_SIZE];
                                ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); // Read from the parent
                                if (bytes < 0) {
                                    perror("Worker failed to read from the pipe\n");
                                    exit(EXIT_FAILURE);
                                }
                                buffer3[bytes] = '\0'; 
                                // Extract source and target directories from the buffer
                                char source[BUFFER_SIZE], target[BUFFER_SIZE],filename[BUFFER_SIZE];
                                char *token = strtok(buffer3, "\n");
                                if (token) {
                                    while (*token == ' ') token++; 
                                    strncpy(source, token, BUFFER_SIZE - 1);
                                    source[BUFFER_SIZE - 1] = '\0';
                                }
                                token = strtok(NULL, "\n");
                                if (token) {
                                    while (*token == ' ') token++;
                                    strncpy(target, token, BUFFER_SIZE - 1);
                                    target[BUFFER_SIZE - 1] = '\0';
                                }
                                token = strtok(NULL, "\n");
                                if (token) {
                                    while (*token == ' ') token++;
                                    strncpy(filename, token, BUFFER_SIZE - 1);
                                    filename[BUFFER_SIZE - 1] = '\0';
                                }
                                if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                                    perror("Worker: stdout redirection failed");
                                    exit(EXIT_FAILURE);
                                }
                                close(running_workers[i].pipefd[1][1]);
                                execl("./worker", "worker", source, target, "ADDED",filename, NULL); // Execute the worker.c
                                perror("execl error\n");  
                                exit(EXIT_FAILURE);
                            } else if (pid > 0) {  // Parent process
                                close(running_workers[i].pipefd[0][0]);
                                char buffer3[4 * BUFFER_SIZE]; // Send the source and the target dir to the worker process
                                strcpy(running_workers[i].source, cur->data->source_dir);
                                strcpy(running_workers[i].target, cur->data->target_dir);
                                snprintf(buffer3, sizeof(buffer3), "%s\n%s\n%s\n", running_workers[i].source, running_workers[i].target,event->name);
                                ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); // Write data to the pipe
                                if (write_bytes == -1) {
                                    perror("Error on writing to the pipe for worker communication\n");
                                    close(fd_write);
                                    close(inotify_fd);
                                    freenode(&headnode);
                                    
                                    exit(EXIT_FAILURE);
                                }
                                close(running_workers[i].pipefd[0][1]);  
                                running_workers[i].pid = pid;
                                strcpy(running_workers[i].operation,"ADDED");
                                running_workers[i].console = -1;
                                strcpy(running_workers[i].filename,event->name);
                                totalprocess++;
                            }
                        }
                    } else if (event->mask & IN_DELETE) { // A file was deleted
                        if(i == informations.worker_limit) { // Add to the Queue
                            addqueue(&queuedata,cur,-1,"DELETED",event->name);
                        }
                        else {
                            pid_t pid = fork();  // Create the worker
                            if (pid == 0) {  // Worker process
                                close(running_workers[i].pipefd[0][1]);
                                close(running_workers[i].pipefd[1][0]);
                                char buffer3[4 * BUFFER_SIZE];
                                ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); // Read from the parent
                                if (bytes < 0) {
                                    perror("Worker failed to read from the pipe\n");
                                    exit(EXIT_FAILURE);
                                }
                                buffer3[bytes] = '\0'; 
                                // Extract source and target directories from the buffer
                                char source[BUFFER_SIZE], target[BUFFER_SIZE],filename[BUFFER_SIZE];
                                char *token = strtok(buffer3, "\n");
                                if (token) {
                                    while (*token == ' ') token++; 
                                    strncpy(source, token, BUFFER_SIZE - 1);
                                    source[BUFFER_SIZE - 1] = '\0';
                                }
                                token = strtok(NULL, "\n");
                                if (token) {
                                    while (*token == ' ') token++;
                                    strncpy(target, token, BUFFER_SIZE - 1);
                                    target[BUFFER_SIZE - 1] = '\0';
                                }
                                token = strtok(NULL, "\n");
                                if (token) {
                                    while (*token == ' ') token++;
                                    strncpy(filename, token, BUFFER_SIZE - 1);
                                    filename[BUFFER_SIZE - 1] = '\0';
                                }
                                if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                                    perror("Worker: stdout redirection failed");
                                    close(fd_write);
                                    close(inotify_fd);
                                    
                                    freenode(&headnode);
                                    exit(EXIT_FAILURE);
                                }
                                close(running_workers[i].pipefd[1][1]);
                                execl("./worker", "worker", source, target, "DELETED",filename, NULL); // Execute the worker.c
                                perror("execl error\n");
                                exit(EXIT_FAILURE);
                            } else if (pid > 0) {  // Parent process
                                close(running_workers[i].pipefd[0][0]);
                                char buffer3[4 * BUFFER_SIZE]; // Send the source and the target dir to the worker process
                                strcpy(running_workers[i].source, cur->data->source_dir);
                                strcpy(running_workers[i].target, cur->data->target_dir);
                                snprintf(buffer3, sizeof(buffer3), "%s\n%s\n%s\n", running_workers[i].source, running_workers[i].target,event->name);
                                ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); // Write data to the pipe
                                if (write_bytes == -1) {
                                    perror("Error on writing to the pipe for worker communication\n");
                                    close(fd_write);
                                    close(inotify_fd);
                                    freenode(&headnode);  
                                    exit(EXIT_FAILURE);
                                }
                                close(running_workers[i].pipefd[0][1]);  
                                running_workers[i].pid = pid;
                                strcpy(running_workers[i].operation,"DELETED");
                                running_workers[i].console = -1;
                                strcpy(running_workers[i].filename,event->name);
                                totalprocess++;
                            }
                        }
                    } else if (event->mask & IN_MODIFY) { // Modified a file
                        if(i == informations.worker_limit) { // Add to the Queue
                            addqueue(&queuedata,cur,-1,"MODIFIED",event->name);
                        }
                        else {
                            pid_t pid = fork();  // Create the worker
                            if (pid == 0) {  // Worker process
                                close(running_workers[i].pipefd[0][1]);
                                close(running_workers[i].pipefd[1][0]);
                                char buffer3[4 * BUFFER_SIZE];
                                ssize_t bytes = read(running_workers[i].pipefd[0][0], buffer3, sizeof(buffer3) - 1); // Read from the parent
                                if (bytes < 0) {
                                    perror("Worker failed to read from the pipe\n");
                                    exit(EXIT_FAILURE);
                                }
                                buffer3[bytes] = '\0'; 
                                // Extract source and target directories from the buffer
                                char source[BUFFER_SIZE], target[BUFFER_SIZE],filename[BUFFER_SIZE];
                                char *token = strtok(buffer3, "\n");
                                if (token) {
                                    while (*token == ' ') token++; 
                                    strncpy(source, token, BUFFER_SIZE - 1);
                                    source[BUFFER_SIZE - 1] = '\0';
                                }
                                token = strtok(NULL, "\n");
                                if (token) {
                                    while (*token == ' ') token++;
                                    strncpy(target, token, BUFFER_SIZE - 1);
                                    target[BUFFER_SIZE - 1] = '\0';
                                }
                                token = strtok(NULL, "\n");
                                if (token) {
                                    while (*token == ' ') token++;
                                    strncpy(filename, token, BUFFER_SIZE - 1);
                                    filename[BUFFER_SIZE - 1] = '\0';
                                }
                                if (dup2(running_workers[i].pipefd[1][1], STDOUT_FILENO) == -1) {
                                    perror("Worker: stdout redirection failed");
                                    exit(EXIT_FAILURE);
                                }
                                close(running_workers[i].pipefd[1][1]);
                                execl("./worker", "worker", source, target, "MODIFIED",filename, NULL); // Execute the worker.c
                                perror("execl error\n");  
                                exit(EXIT_FAILURE);
                            } else if (pid > 0) {  // Parent process
                                close(running_workers[i].pipefd[0][0]);
                                char buffer3[4 * BUFFER_SIZE]; // Send the source and the target dir to the worker process
                                strcpy(running_workers[i].source, cur->data->source_dir);
                                strcpy(running_workers[i].target, cur->data->target_dir);
                                snprintf(buffer3, sizeof(buffer3), "%s\n%s\n%s\n", running_workers[i].source, running_workers[i].target,event->name);
                                ssize_t write_bytes = write(running_workers[i].pipefd[0][1], buffer3, strlen(buffer3)); // Write data to the pipe
                                if (write_bytes == -1) {
                                    perror("Error on writing to the pipe for worker communication\n");
                                    exit(EXIT_FAILURE);
                                }
                                close(running_workers[i].pipefd[0][1]);  
                                running_workers[i].pid = pid;
                                strcpy(running_workers[i].operation,"MODIFIED");
                                strcpy(running_workers[i].filename,event->name);
                                running_workers[i].console = -1;
                                totalprocess++;
                            }
                        }
                    }
                    else {
                        printf("This command can not happen\n");
                    }
                }
                ptr += EVENT_SIZE + event->len;
            }
        }
    }
    // Free the content
    for (int i = 0; i < informations.worker_limit; i++) {
        close(running_workers[i].pipefd[0][0]);
        close(running_workers[i].pipefd[0][1]);
        close(running_workers[i].pipefd[1][0]);
        close(running_workers[i].pipefd[1][1]);
    }
    free(running_workers);
    close(fd_read);
    close(fd_write);
    freenode(&headnode);
    free(ExitedPIDS);
    exit(EXIT_SUCCESS);
}
