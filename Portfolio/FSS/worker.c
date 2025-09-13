#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#define BUFFER_SIZE 1024
#define ERROR_BUFFER_SIZE 4 * BUFFER_SIZE


// Copy a file to the target
void copyfile(const char* source, char* target, char* error_buffer, int* error_count, int* total_copy, int* total_skip) {
    int source_fd = -1;
    int target_fd = -1;
    char buffer[4 * BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    source_fd = open(source, O_RDONLY);
    if (source_fd == -1) {
        size_t len = strlen(error_buffer);
        snprintf(error_buffer + len, ERROR_BUFFER_SIZE - len, "- File %s: Failed to open\n", source);
        (*error_count)++;
        (*total_skip)++;
        return;
    }
    target_fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (target_fd == -1) {
        size_t len = strlen(error_buffer);
        snprintf(error_buffer + len, ERROR_BUFFER_SIZE - len, "- File %s: Failed to open\n", target);
        (*error_count)++;
        (*total_skip)++;
        close(source_fd);
        return;
    }
    while ((bytes_read = read(source_fd, buffer, sizeof(buffer))) > 0) { //Reading from the source file to the target
        bytes_written = write(target_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            size_t len = strlen(error_buffer);
            snprintf(error_buffer + len, ERROR_BUFFER_SIZE - len, "- File %s: Write error\n", target);
            (*error_count)++;
            (*total_skip)++;
            close(source_fd);
            close(target_fd);
            return;
        }
    }
    if (bytes_read == -1) {
        size_t len = strlen(error_buffer);
        snprintf(error_buffer + len, ERROR_BUFFER_SIZE - len, "- File %s: Read error\n", source);
        (*error_count)++;
        (*total_skip)++;
    } else {
        (*total_copy)++;
    }
    close(source_fd);
    close(target_fd);
}

// Add a file to the target dir
void addfile(char* source, char* target, char* filename) {
    char source_path[BUFFER_SIZE];
    char destination_path[BUFFER_SIZE];
    char error_buf[ERROR_BUFFER_SIZE] = "";
    int error_count = 0, copy = 0, skip = 0;
    // Build the full source and destination paths
    snprintf(source_path, sizeof(source_path), "%s/%s", source, filename);
    snprintf(destination_path, sizeof(destination_path), "%s/%s", target, filename);
    struct stat stat_buf;
    // Check if the file already exists in the target
    if (stat(destination_path, &stat_buf) == 0) {
        size_t len = strlen(error_buf);
        snprintf(error_buf + len, sizeof(error_buf) - len, "- File %s: Already exists in target\n", filename);
        error_count++;
        skip++;
    } else {
        // Check if source exists
        if (stat(source_path, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode)) {
            copyfile(source_path, destination_path, error_buf, &error_count, &copy, &skip); // Do the copying
        } else {
            size_t len = strlen(error_buf);
            snprintf(error_buf + len, sizeof(error_buf) - len, "- File %s: Not found in source\n", filename);
            error_count++;
            skip++;
        }
    }
    // Print the execution report 
    fprintf(stdout, "EXEC_REPORT_START\n");
    fprintf(stdout, "STATUS: %s\n", (error_count == 0) ? "SUCCESS" : "ERROR");
    fprintf(stdout, "DETAILS: %d files added, %d skipped\n", copy, skip);
    fprintf(stdout,"ERRORS:\n");
    if (error_count > 0) {
        fprintf(stdout, "%s", error_buf);
    }
    fprintf(stdout, "EXEC_REPORT_END\n");
}


// Modify a file
void modifyfile(char* source, char* target, char* filename) {
    char source_path[BUFFER_SIZE];
    char destination_path[BUFFER_SIZE];
    char error_buf[ERROR_BUFFER_SIZE] = "";
    int error_count = 0, copy = 0, skip = 0;
    snprintf(source_path, sizeof(source_path), "%s/%s", source, filename); // Build full paths
    snprintf(destination_path, sizeof(destination_path), "%s/%s", target, filename);
    struct stat stat_buf;
    // Check if the file exists in the source directory
    if (stat(source_path, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode)) { 
        // Check if the file exists in the target directory
        if (access(destination_path, F_OK) == 0) {
            copyfile(source_path, destination_path, error_buf, &error_count, &copy, &skip);
        } else {
            size_t len = strlen(error_buf);
            snprintf(error_buf + len, sizeof(error_buf) - len, "- File %s: Not found in target for modification\n", filename);
            error_count++;
            skip++;
        }
    } else {
        size_t len = strlen(error_buf);
        snprintf(error_buf + len, sizeof(error_buf) - len, "- File %s: Not found in source\n", filename);
        error_count++;
        skip++;
    }
    // Write the report
    fprintf(stdout, "EXEC_REPORT_START\n");
    fprintf(stdout, "STATUS: %s\n", (error_count == 0) ? "SUCCESS" : "ERROR");
    fprintf(stdout, "DETAILS: %d files modified, %d skipped\n", copy, skip);
    fprintf(stdout,"ERRORS:\n");
    if (error_count > 0) {
        fprintf(stdout, "%s", error_buf);
    }
    fprintf(stdout, "EXEC_REPORT_END\n");
}


// Delete a file based on the target
void deletefile(char* target, char* filename) {
    char target_path[BUFFER_SIZE];
    char error_buf[ERROR_BUFFER_SIZE] = "";
    int error_count = 0, deleted = 0, skipped = 0;
    snprintf(target_path, sizeof(target_path), "%s/%s", target, filename);     // Create full file path for target
    if (unlink(target_path) == 0) { // Deleting the file
        deleted++;
    } else {
        size_t len = strlen(error_buf);
        snprintf(error_buf + len, sizeof(error_buf) - len, "- File %s: Delete failed (%s)\n", filename, strerror(errno));
        error_count++;
        skipped++;
    }
    // Write the report
    fprintf(stdout, "EXEC_REPORT_START\n");
    if (error_count == 0) {
        fprintf(stdout, "STATUS: SUCCESS\n");
        fprintf(stdout, "DETAILS: %d files deleted, %d skipped\n", deleted, skipped);
    } else {
        fprintf(stdout, "STATUS: ERROR\n");
        fprintf(stdout, "DETAILS: %d files deleted, %d skipped\n", deleted, skipped);
    }
    fprintf(stdout,"ERRORS:\n");
    if (error_count > 0) {
        fprintf(stdout, "%s", error_buf);
    }
    fprintf(stdout, "EXEC_REPORT_END\n");
}

// Full sync
void fullsync(char* source, char* target) {
    DIR* source_dir;
    DIR* target_dir;
    struct dirent* source_entry;
    struct dirent* target_entry;
    char source_path[BUFFER_SIZE], target_path[BUFFER_SIZE];
    char error_buf[ERROR_BUFFER_SIZE] = "";
    int error_count = 0, copy = 0, skip = 0;
    // Delete all the files from the target dir
    target_dir = opendir(target);
    if (!target_dir) {
        size_t len = strlen(error_buf);
        snprintf(error_buf + len, sizeof(error_buf) - len, "- Failed to open target directory %s: %s\n", target, strerror(errno));
        error_count++;
        // Write the report
        fprintf(stdout, "EXEC_REPORT_START\n");
        fprintf(stdout, "STATUS: ERROR\n");
        fprintf(stdout, "DETAILS: 0 files copied, 0 skipped\n");
        fprintf(stdout, "ERRORS:\n");
        if (error_count > 0) {
            fprintf(stdout, "%s", error_buf);
        }
        fprintf(stdout, "EXEC_REPORT_END\n");
        closedir(target_dir);
        return;
    } else {
        while ((target_entry = readdir(target_dir)) != NULL) {
            if (strcmp(target_entry->d_name, ".") == 0 || strcmp(target_entry->d_name, "..") == 0)
                continue;
            snprintf(target_path, sizeof(target_path), "%s/%s", target, target_entry->d_name);
            struct stat st;
            if (stat(target_path, &st) == 0 && S_ISREG(st.st_mode)) {
                if (unlink(target_path) != 0) {
                    size_t len = strlen(error_buf);
                    snprintf(error_buf + len, sizeof(error_buf) - len, "- Failed to delete %s: %s\n", target_path, strerror(errno));
                    error_count++;
                    skip++;
                }
            }
        }
    }
    // Copy all files from source_dir to target_dir
    source_dir = opendir(source);
    if (!source_dir) {
        size_t len = strlen(error_buf);
        snprintf(error_buf + len, sizeof(error_buf) - len, "- Failed to open source directory %s: %s\n", source, strerror(errno));
        error_count++;
    } else {
        while ((source_entry = readdir(source_dir)) != NULL) {
            if (strcmp(source_entry->d_name, ".") == 0 || strcmp(source_entry->d_name, "..") == 0)
                continue;
            snprintf(source_path, sizeof(source_path), "%s/%s", source, source_entry->d_name);
            snprintf(target_path, sizeof(target_path), "%s/%s", target, source_entry->d_name);
            struct stat st;
            if (stat(source_path, &st) == 0 && S_ISREG(st.st_mode)) {
                copyfile(source_path, target_path, error_buf, &error_count, &copy, &skip);
            }
        }
    }
    closedir(source_dir);
    closedir(target_dir);
    // Write the report
    fprintf(stdout, "EXEC_REPORT_START\n");
    fprintf(stdout, "STATUS: %s\n", (error_count == 0) ? "SUCCESS" : "PARTIAL");
    fprintf(stdout, "DETAILS: %d files copied, %d skipped\n", copy, skip);
    fprintf(stdout,"ERRORS:\n");
    if (error_count > 0) {
        fprintf(stdout, "%s", error_buf);
    }
    fprintf(stdout, "EXEC_REPORT_END\n");
}


int main(int argc, char* argv[]) {
    if (argc != 5) {
        perror("Wrong syntax on calling the worker.c");
        exit(EXIT_FAILURE);
    }
    char* source = argv[1];
    char* target = argv[2];
    char* filename = argv[3];
    char* operation = argv[4];
    if (strcmp(operation, "FULL") == 0 && strcmp(filename, "ALL") == 0) { // Full sync
        fullsync(source, target);
    } else if (strcmp(operation, "ADDED") == 0) { // Add a file
        addfile(source, target, filename);
    } else if (strcmp(operation, "MODIFIED") == 0) { // Modify a file
        modifyfile(source, target, filename);
    } else if (strcmp(operation, "DELETED") == 0) { // Delete a file
        deletefile(target, filename);
    } else { // Unknown operation
        fprintf(stdout, "EXEC_REPORT_START\n");
        fprintf(stdout, "STATUS: DENIED\n");
        fprintf(stdout, "DETAILS: NONE\n");
        fprintf(stdout, "ERRORS:\n -Invalid operation\n");
        fprintf(stdout, "EXEC_REPORT_END\n");
    }
    fflush(stdout);
    exit(EXIT_SUCCESS);
}
