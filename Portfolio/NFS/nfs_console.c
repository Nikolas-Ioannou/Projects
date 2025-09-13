#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <poll.h>
#include <arpa/inet.h>
#define BUFFER_SIZE 526

// Get formatted timestamp
void get_time(char *buffer, size_t size) {
    time_t raw_time;
    struct tm *time_info;
    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", time_info);
}

// Reads a whole line from a socket
char* read_line(int sockfd) {
    size_t buf_size = BUFFER_SIZE;
    size_t len = 0;
    char *buffer = malloc(BUFFER_SIZE); // Contains the data
    if (!buffer) {
        perror("malloc");
        return NULL;
    }
    while (1) {
        char ch;
        ssize_t bytes_read = recv(sockfd, &ch, 1, 0); 
        if (bytes_read < 0) {
            perror("recv");
            free(buffer);
            return NULL;
        } else if (bytes_read == 0) {
            break;
        }
        buffer[len++] = ch;
        if (len >= buf_size) { // Realloc if needed
            buf_size *= 2;
            char *new_buffer = realloc(buffer, buf_size);
            if (!new_buffer) {
                perror("realloc");
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }
        if (ch == '\n') {
            break;
        }
    }
    buffer[len] = '\0';
    return buffer; // Return the line
}


int main(int argc, char* argv[]) {
    if (argc != 7) { 
        fprintf(stderr, "Wrong syntax on the terminal\n");
        exit(EXIT_FAILURE);
    }
    FILE *log_file = fopen(argv[2], "a");   // Open logfile and add texts at the end
    if (log_file == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
    char host_ip[BUFFER_SIZE]; // Name of the machine which runs the nfs_manager
    int host_port = atoi(argv[6]); // The Port which the nfs_manager will read commands from the nfs_console
    strcpy(host_ip,argv[4]); 
    int sock = socket(AF_INET, SOCK_STREAM, 0); // Set up the socket communication
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(host_port);
    if (inet_pton(AF_INET, host_ip, &serv_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(sock);
        exit(EXIT_FAILURE);
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    char* line; // Store the lines that will come
    while (1) {
        printf("> ");  
        fflush(stdout);
        char input[BUFFER_SIZE]; // This variable gets the input
        char sendinput[BUFFER_SIZE]; // This variable will send the input
        if (fgets(input, sizeof(input), stdin) == NULL) {  // Get user input
            perror("Error reading input");
            break;
        }
        strcpy(sendinput,input);
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';  // Safely remove newline
        }
        char *command = strtok(input, " "); // Check the command
        if (command == NULL) {
            fprintf(stderr, "Error: no command\n");
            continue;  
        }
        char time[25];
        get_time(time, sizeof(time)); // Get the time 
        // Handle commands
        if (strcmp(command, "shutdown") == 0) { // Shutdown
            if (send(sock, sendinput, strlen(sendinput), 0) < 0) { // Send the input to the manager
                perror("Send failed");
                shutdown(sock, SHUT_RDWR);
                close(sock);
                fclose(log_file);
                exit(EXIT_FAILURE);
            }
            while ((line = read_line(sock)) != NULL) { // Get messages until a specific text comes 
                if(line == NULL) { // Something went wrong with the reading 
                    shutdown(sock, SHUT_RDWR);
                    close(sock);
                    fclose(log_file);
                    exit(EXIT_FAILURE);
                }
                printf("%s", line); // Each line includes the '\n'
                if (strstr(line, "Manager shutdown complete.")) { // When it gets this message then it is over
                    free(line);
                    break;
                }
                free(line);
            }
            char cwd[BUFFER_SIZE]; // Will have the current directory at the end
            fprintf(log_file, "%s Command shutdown %s\n", time,getcwd(cwd, sizeof(cwd)) ? cwd : "unknown"); // Write to the console_log_file 
            fflush(log_file);
            break;
        }
        else if (strcmp(command, "add") == 0) { // Add command
            char *source = strtok(NULL, " "); 
            char *arrow = strtok(NULL, " ");
            char *target = strtok(NULL," ");
            if (!source || !target || !arrow) { // Check the directories and if the arrow exists
                fprintf(stderr, "Wrong syntax on add\n"); // If there was an wrong syntax just read again
                continue;
            }
            if (send(sock, sendinput, strlen(sendinput), 0) < 0) { // Send the input to the manager
                perror("Send failed");
                shutdown(sock, SHUT_RDWR);
                close(sock);
                fclose(log_file);
                exit(EXIT_FAILURE);
            }
            line = read_line(sock);
            if(line == NULL) {
                perror("Error on reading\n");
                shutdown(sock, SHUT_RDWR);
                close(sock);
                fclose(log_file);
                exit(EXIT_FAILURE);
            }
            printf("%s",line); // Print the message
            free(line);
            fprintf(log_file, "%s Command add %s -> %s\n", time, source, target);
        }
        else if (strcmp(command, "cancel") == 0) { // Cancel command
            char *target = strtok(NULL, " ");
            if (!target) {
                fprintf(stderr, "Error: cancel requires target path\n");
                continue;
            }
            if (send(sock, sendinput, strlen(sendinput), 0) < 0) { 
                perror("Send failed");
                shutdown(sock, SHUT_RDWR);
                close(sock);
                fclose(log_file);
                exit(EXIT_FAILURE);
            }
            line = read_line(sock);
            if(line == NULL) {
                perror("Error on reading\n");
                shutdown(sock, SHUT_RDWR);
                close(sock);
                fclose(log_file);
                exit(EXIT_FAILURE);
            }
            printf("%s",line); 
            free(line);
            fprintf(log_file, "%s Command cancel %s\n", time, target);
        }
        else {
            fprintf(stderr, "Error: Unknown command '%s'\n", command);
            continue;
        }
        fflush(log_file);
    }
    // Close the socket and the logfile
    shutdown(sock, SHUT_RDWR);
    close(sock);
    fclose(log_file);
    exit(EXIT_SUCCESS);
}