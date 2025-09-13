#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <poll.h>
#define BUFFER_SIZE 256

// Get formatted timestamp
void get_time(char *buffer, size_t size) {
    time_t raw_time;
    struct tm *time_info;
    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", time_info);
}

// Delete pipes
void delete_pipes() {
    unlink("fss_in");
    unlink("fss_out");
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Wrong syntax on the terminal\n");
        exit(EXIT_FAILURE);
    }
    // Open log file
    FILE *log_file = fopen(argv[2], "a");
    if (log_file == NULL) {
        perror("Failed to open log file");
        delete_pipes();
        exit(EXIT_FAILURE);
    }
    // Open the pipes and wait for the manager
    int fd_write = open("fss_in", O_WRONLY );
    int fd_read = open("fss_out", O_RDONLY);
    if (fd_write == -1 || fd_read == -1) {
        perror("Failed to open pipes");
        fclose(log_file);
        delete_pipes();
        exit(EXIT_FAILURE);
    }
    char input[BUFFER_SIZE];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) {  // Get user input
            perror("Error reading input");
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        char *command = strtok(input, " ");
        if (command == NULL) {
            continue;  
        }
        char time[25];
        get_time(time, sizeof(time));
        // Handle commands
        if (strcmp(command, "shutdown") == 0) { // Shutdown
            if (write(fd_write, input, strlen(input)+1) == -1) { // Send command
                perror("Write failed");
                break;
            }
            char response[4 * BUFFER_SIZE];
            ssize_t bytes = read(fd_read, response, sizeof(response)-1);
            if (bytes > 0) { 
                response[bytes] = '\0';
                printf("%s\n", response);
            }
            else if(bytes == 0) {
                continue;
            }
            else {
                perror("something went wrong");
                exit(EXIT_FAILURE);
            }
            char cwd[BUFFER_SIZE];
            fprintf(log_file, "%s Command shutdown %s\n", time,getcwd(cwd, sizeof(cwd)) ? cwd : "unknown"); // Write to the console_log_file 
            break;
        }
        else if (strcmp(command, "add") == 0) { // Add command
            char *source = strtok(NULL, " "); 
            char *target = strtok(NULL, " ");
            if (!source || !target) { // Check the directories
                fprintf(stderr, "Error: add requires source and target paths\n");
                continue;
            }
            char command_with_target[BUFFER_SIZE];
            snprintf(command_with_target, sizeof(command_with_target), "%s %s %s", command, source, target);
            if (write(fd_write, command_with_target, strlen(command_with_target) + 1) == -1) {
                perror("Write failed");
                break;
            }
            char response[4 * BUFFER_SIZE];
            ssize_t bytes = read(fd_read, response, sizeof(response)-1);
            if (bytes > 0) {
                response[bytes] = '\0';
                printf("%s\n", response);
            }
            fprintf(log_file, "%s Command add %s -> %s\n", time, source, target);
        }
        else if (strcmp(command, "sync") == 0) { // Sync a directory
            char *target = strtok(NULL, " ");
            if (!target) {
                fprintf(stderr, "Error: sync requires target path\n");
                continue;
            }
            char command_with_target[BUFFER_SIZE];
            snprintf(command_with_target, sizeof(command_with_target), "%s %s", command, target);
            if (write(fd_write, command_with_target, strlen(command_with_target) + 1) == -1) {
                perror("Write failed");
                break;
            }
            char response[4 * BUFFER_SIZE];
            ssize_t bytes = read(fd_read, response, sizeof(response)-1);
            if (bytes > 0) {
                response[bytes] = '\0';
                printf("%s\n", response);
            }
            fprintf(log_file, "%s Command sync %s\n", time, target);
        }
        else if (strcmp(command, "cancel") == 0) { // Cancel command
            char *target = strtok(NULL, " ");
            if (!target) {
                fprintf(stderr, "Error: cancel requires target path\n");
                continue;
            }
            char command_with_target[BUFFER_SIZE];
            snprintf(command_with_target, sizeof(command_with_target), "%s %s", command, target); // Send the command
            if (write(fd_write, command_with_target, strlen(command_with_target) + 1) == -1) {
                perror("Write failed");
                break;
            }
            char response[4 * BUFFER_SIZE];
            ssize_t bytes = read(fd_read, response, sizeof(response)-1);
            if (bytes > 0) {
                response[bytes] = '\0';
                printf("%s\n", response);
            }
            fprintf(log_file, "%s Command cancel %s\n", time, target);
        }
        else if (strcmp(command, "status") == 0) { // Status command
            char *target = strtok(NULL, " ");
            if (!target) {
                fprintf(stderr, "Error: status requires target path\n");
                continue;
            }
            char command_with_target[BUFFER_SIZE];
            snprintf(command_with_target, sizeof(command_with_target), "%s %s", command, target);
            if (write(fd_write, command_with_target, strlen(command_with_target) + 1) == -1) {
                perror("Write failed");
                break;
            }
            char response[4 * BUFFER_SIZE];
            ssize_t bytes = read(fd_read, response, sizeof(response)-1);
            if (bytes > 0) {
                response[bytes] = '\0';
                printf("%s\n", response);
            }
            fprintf(log_file, "%s Command status %s\n", time, target);
        }
        else {
            fprintf(stderr, "Error: Unknown command '%s'\n", command);
        }
        fflush(log_file);
    }
    // Close the pipes,file and then delete the pipes
    close(fd_write);
    close(fd_read);
    fclose(log_file);
    delete_pipes();
    exit(EXIT_SUCCESS);
}