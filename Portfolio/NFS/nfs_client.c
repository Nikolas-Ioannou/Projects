#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#define BUFFER_SIZE 526

int new_socket;
int server_fd; // Make the sockets global in order to handle errors

// Send a message to a socket
void send_message(int socket, const char *message) {
    if (send(socket, message, strlen(message), 0) < 0) {
        perror("Send failed");
        close(new_socket);
        close(server_fd);
        exit(EXIT_FAILURE);
    }
}

// Get whole data from the socket based on the size
ssize_t recv_all(int sockfd, void *buffer, size_t length) {
    size_t total_received = 0;
    ssize_t bytes_received;
    char *ptr = (char*) buffer;
    while (total_received < length) {
        bytes_received = recv(sockfd, ptr + total_received, length - total_received, 0);
        if (bytes_received <= 0) {
            perror("Error on reading");
            close(new_socket);
            close(server_fd);
            exit(EXIT_FAILURE);
        }
        total_received += bytes_received;
    }
    return total_received;
}


// LIST Operation
void list_files(int socket, char *source_dir) {
    DIR *dir = opendir(source_dir);
    if (dir == NULL) {  // Error opening the directory
        char error_msg[] = ".\n"; // Just send the .\n 
        send_message(socket,error_msg); 
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) { // Getting all the filenames
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, sizeof(buffer), "%s\n", entry->d_name);  // File name and add a newline so the manager can know where the filename ends
            send_message(socket,buffer);
        }
    }
    // Send the end marker ".\n" after the list of files
    char end_marker[] = ".\n";
    send_message(socket,end_marker);
    closedir(dir);
}

// PULL Operation
void pull_file(int socket, char* filepath) {
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd == -1) {
        int32_t err = htonl((int32_t)-1); // Send error message
        send(socket, &err, sizeof(err), 0);
        const char* error_msg = "File doesn't exist\n";
        send_message(socket, error_msg);
        return;
    }
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) { // Reading data from the file and send first the chunk_size
        int32_t net_chunk_size = htonl((int32_t)bytes_read);
        if (send(socket, &net_chunk_size, sizeof(net_chunk_size), 0) < 0) {
            perror("Send error (chunk size)");
            close(file_fd);
            close(new_socket);
            close(server_fd);
            exit(EXIT_FAILURE);
        }
        ssize_t total_sent = 0;
        while (total_sent < bytes_read) { // And then send the data
            ssize_t sent = send(socket, buffer + total_sent, bytes_read - total_sent, 0);
            if (sent < 0) {
                perror("Send error (chunk data)");
                close(file_fd);
                close(new_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
            }
            total_sent += sent;
        }
    }
    if (bytes_read < 0) {
        perror("Read error");
        close(file_fd);
        close(new_socket);
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    // Send termination chunk size = 0 to signal end of file
    int32_t zero = 0;
    zero = htonl(zero);
    if (send(socket, &zero, sizeof(zero), 0) < 0) {
        perror("Send error");
        close(file_fd);
        close(new_socket);
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    close(file_fd);
}

// PUSH Operation
void push_file(int socket, char *filepath, int32_t chunk_size, char *data) {
    char *done_message = "DONE\n";  // Success message
    char *error_message = "ERROR\n"; // Error message
    if (chunk_size == -1) { // First open the file and rewrite it
        int file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (file_fd == -1) {
            perror("Failed to open file for writing");
            send_message(socket, error_message);
            return;
        }
        ssize_t bytes_written = write(file_fd, data, strlen(data)); // Write to the file
        if (bytes_written == -1) {
            perror("Failed to write data to the file");
            send_message(socket, error_message);
            close(file_fd);
            return;
        }
        while (1) { // Now getting the size and the data 
            int32_t net_size;
            if (recv_all(socket, &net_size, sizeof(int32_t)) != sizeof(int32_t)) { // First get chunk size
                perror("Failed to receive size");
                send_message(socket, error_message);
                close(file_fd);
                return;
            }
            chunk_size = ntohl(net_size); // Store the chunk size
            if (chunk_size == 0) { // If it is 0 then end the operation
                break;
            }
            // Allocate memory for the chunk
            char *line = malloc(chunk_size + 1);
            if (!line) {
                perror("malloc failed");
                send_message(socket, error_message);
                close(file_fd);
                return;
            }
            // Receive the data the data
            if (recv_all(socket, line, chunk_size) != chunk_size) {
                perror("Failed to receive chunk data");
                send_message(socket, error_message);
                free(line);
                close(file_fd);
                return;
            }
            line[chunk_size] = '\0'; 
            bytes_written = write(file_fd, line, chunk_size);  // Write the data to the file
            if (bytes_written == -1) {
                perror("Failed to write data to the file");
                send_message(socket, error_message);
                free(line);
                close(file_fd);
                return;
            }
            free(line);
        }
        send_message(socket, done_message); // Send success message 
        close(file_fd); 
    }
    else if (chunk_size == 0) { // Create an empty file in this case
        int file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (file_fd == -1) {
            perror("Failed to open file for writing");
            send_message(socket, error_message);
            return;
        }
        close(file_fd); 
        send_message(socket, done_message); 
    }
    else { 
        perror("Unknown chunk_size");
        send_message(socket, error_message);
    }
}

int main(int argc, char* argv[]) {
    if(argc != 3) {
        perror("Wrong syntax on command line");
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[2]);
    struct sockaddr_in address, client_address;
    int opt = 1;
    socklen_t client_addrlen = sizeof(client_address);
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    // Prepare server address
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    // Listen
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    // Accept
    new_socket = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen);
    if (new_socket < 0) {
        perror("accept failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    while(1) { // Reading from the host multiple times
        int net_size;
        if (recv_all(new_socket, &net_size, sizeof(int)) != sizeof(int)) { // Get the size of the command first
            perror("Failed to receive size");
            close(new_socket);
            exit(EXIT_FAILURE);
        }
        int size = ntohl(net_size); // Store it to a variable
        char *line = malloc(size + 1);  // Allocate memory for the command
        if (!line) {
            perror("malloc failed");
            close(server_fd);
            close(new_socket);
            exit(EXIT_FAILURE);
        }
        if (recv_all(new_socket, line, size) != size) { // Get the whole command based on the size
            perror("Failed to receive command");
            free(line);
            close(new_socket);
            close(server_fd);
            exit(EXIT_FAILURE);
        }
        line[size] = '\0';
        char* command = strtok(line," ");
        if(strcmp(command, "LIST") == 0) { // List command
            char* source_dir = strtok(NULL, " ");
            if(!source_dir) {
                perror("Wrong syntax on the command LIST");
                free(line);
                close(new_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
            }
            list_files(new_socket,source_dir);
        }
        else if(strcmp(command, "PULL") == 0) { // PULL command
            char* targetpath = strtok(NULL," ");
            if(!targetpath) {
                perror("Wrong syntax on the command PULL");
                free(line);
                close(new_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
            }
            pull_file(new_socket,targetpath);
        }
        else if(strcmp(command,"PUSH") == 0) {
            char* targetpath = strtok(NULL," ");
            char* chunk_size = strtok(NULL," ");
            if (!targetpath || !chunk_size) {
                perror("Wrong syntax on the command PUSH");
                free(line);
                close(new_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
            }
            char *data = chunk_size + strlen(chunk_size) + 1;
            if(data == NULL) {
                perror("Wrong syntax on the command PUSH");
                free(line);
                close(new_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
            }
            push_file(new_socket,targetpath,(int32_t) atoi(chunk_size),data);
        }
        else { // The command shutdown or an unknown operation was given
            free(command);
            break;
        }
        free(command);
    }
    close(new_socket);
    close(server_fd);
    exit(EXIT_SUCCESS);
}