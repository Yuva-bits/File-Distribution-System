#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>

#define PORT 8009
#define BUFFER_SIZE 1024

//Compares strings for sorting
int compareStrings(const void *a, const void *b)
{
        return strcmp(*(const char **)a, *(const char **)b); //compares strings
}

// Function prototypes
void createDirectories(char *path);
void handleStore(int sockfd, char *destpath);
void handleGet(int sockfd, char *filepath);
void handleRemove(int sockfd, char *filepath);
void handleList(int sockfd, char *dirpath);
void handleTar(int sockfd, char *filetype);
void handleClient(int sockfd);

//Creates directory path recursively
void createDirectories(char *path)
{
        char temp[256];
        char *p = NULL;
        size_t len;

        snprintf(temp, sizeof(temp), "%s", path); //copies path to temp
        len = strlen(temp); //gets length of temp

        //Checks if last character is '/'
        if (temp[len - 1] == '/')
                temp[len - 1] = 0; //sets last character to null
        for (p = temp + 1; *p; p++)
        {
                //Checks if character is '/'
                if (*p == '/')
                {
                        *p = 0; //sets character to null
                        mkdir(temp, 0755); //creates directory
                        *p = '/'; //sets character to '/'
                }
        }
        mkdir(temp, 0755); //creates directory
}

//Handles client connection
void handleClient(int sockfd)
{
        char buffer[BUFFER_SIZE];
        char command[20], arg[512];

        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives command
        //Checks if client disconnected
        if (bytes_received <= 0)
        {
                printf("Client disconnected\n");
                return;
        }
        printf("Received command: %s\n", buffer);

        sscanf(buffer, "%s %s", command, arg); //scans command and argument
        //Checks if command is store`
        if (strcmp(command, "store") == 0)
        {
                printf("Store command - path: %s\n", arg);
                send(sockfd, "Ready for filename", 18, 0); //sends acknowledgment that we received the command
                handleStore(sockfd, arg); //handles store command
        }
        //Checks if command is get
        else if (strcmp(command, "get") == 0)
        {
                handleGet(sockfd, arg); //handles get command
        }
        //Checks if command is remove
        else if (strcmp(command, "remove") == 0)
        {
                handleRemove(sockfd, arg); //handles remove command
        }
        //Checks if command is list
        else if (strcmp(command, "list") == 0)
        {
                handleList(sockfd, arg); //handles list command
        }
        //Checks if command is tar
        else if (strcmp(command, "tar") == 0)
        {
                handleTar(sockfd, arg); //handles tar command
        }
        else
        {
            send(sockfd, "Unknown command", 15, 0);
        }
}

// Function to handle store command (receive file from S1)
void handleStore(int sockfd, char *destpath)
{
        char dirpath[512];
        //Cleans up the path to avoid problems
        if (strncmp(destpath, "~S3/", 4) == 0)
        {
                sprintf(dirpath, "./S3%s", destpath + 3); //converts ~S3 to ./S3
        }
        else
        {
                sprintf(dirpath, "./S3%s", destpath + 3); //converts ~S3 to ./S3
        }
        printf("Destination directory: %s\n", dirpath);
        
        createDirectories(dirpath); //creates directories
        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives filename
        if (bytes_received <= 0)
        {
                printf("Error receiving filename\n");
                return;
        }
        
        char filename[256];
        strncpy(filename, buffer, sizeof(filename) - 1); //copies filename to buffer
        filename[sizeof(filename) - 1] = '\0'; //sets null terminator
        printf("Received filename: %s\n", filename);
        
        char fullpath[512];
        sprintf(fullpath, "%s/%s", dirpath, filename); //completes filepath with filename
        printf("Full path for file: %s\n", fullpath);
        send(sockfd, "Ready for filesize", 18, 0); //sends acknowledgment for filename
        
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives filesize
        if (bytes_received <= 0)
        {
                printf("Error receiving filesize\n");
                return;
        }
        
        long filesize = atol(buffer); //converts filesize to long
        printf("Filesize: %ld bytes\n", filesize); //prints filesize
        
        FILE *fp = fopen(fullpath, "wb"); //opens file for writing
        if (fp == NULL) //checks if error creating file
        {
                printf("Error creating file %s\n", fullpath);
                send(sockfd, "FAILED", 6, 0); //sends failed
                return;
        }
        send(sockfd, "Ready for data", 14, 0); //sends acknowledgment for filesize
        
        int bytes_written;
        long total_received = 0;
        //Receives file content
        while (total_received < filesize)
        {
                bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives file content
                if (bytes_received <= 0)
                {
                        printf("Error receiving file data\n");
                        fclose(fp); //closes file
                        return;
                }
                
                bytes_written = fwrite(buffer, 1, bytes_received, fp); //writes file content
                if (bytes_written != bytes_received)
                {
                        printf("Error writing to file\n");
                        fclose(fp); //closes file
                        return;
                }

                total_received += bytes_received; //updates total received
                printf("\rReceived %ld/%ld bytes", total_received, filesize); //prints total received
                fflush(stdout); //flushes stdout
        }

        printf("\nFile received successfully\n");
        fclose(fp); //closes file
        send(sockfd, "File stored successfully", 24, 0); //sends file stored successfully
}

// Function to handle GET request from server S1
void handleGet(int client_sockfd, char *filepath)
{
        char actual_path[PATH_MAX];
        printf("Original request path: %s\n", filepath);
        //Checks if path starts with ~S3/
        if (strncmp(filepath, "~S3/", 4) == 0) 
        {
                sprintf(actual_path, "./S3%s", filepath + 3); //converts ~S3 to ./S3
        }
        else if (strstr(filepath, "test/") != NULL)
        {
                char *test_path = strstr(filepath, "test/"); //extracts test/ portion
                sprintf(actual_path, "./S3/%s", test_path); //converts ~S3 to ./S3
        }
        else
        {
                sprintf(actual_path, "./S3/%s", filepath + 4); //converts ~S3 to ./S3
        }
        printf("Checking for file: %s\n", actual_path);
        
        struct stat st;
        //Checks if file exists
        if (stat(actual_path, &st) < 0) 
        {
                printf("Error: File '%s' not found (errno: %d - %s)\n", actual_path, errno, strerror(errno));
                send(client_sockfd, "FILE_NOT_FOUND", 14, 0); //sends file not found
                return;
        }

        long filesize = st.st_size;
        printf("File '%s' found, size: %ld bytes\n", actual_path, filesize);
        char *ext = strrchr(actual_path, '.'); //extracts file extension
        //Checks if file extension is .txt  
        if (ext == NULL || strcmp(ext, ".txt") != 0)
        {
                printf("Error: Only .txt files are allowed in S3 (requested: %s)\n", actual_path);
                send(client_sockfd, "FILE_NOT_FOUND", 14, 0); //sends file not found
                return;
        }
        
        FILE *debug_fp = fopen(actual_path, "r"); //opens file for reading
        if (debug_fp) 
        {
                char content[256] = {0}; //clears content
                size_t bytes_read = fread(content, 1, sizeof(content) - 1, debug_fp); //reads file content  
                content[bytes_read] = '\0'; //sets null terminator
                printf("File content (%zu bytes): '%s'\n", bytes_read, content); //prints file content
                fclose(debug_fp); //closes file
        }
        
        FILE *fp = fopen(actual_path, "rb"); //opens file for reading
        if (fp == NULL) 
        {
                printf("Error: Failed to open file '%s' (errno: %d - %s)\n", actual_path, errno, strerror(errno));
                send(client_sockfd, "FILE_NOT_FOUND", 14, 0); //sends file not found
                return;
        }
        
        char buffer[BUFFER_SIZE];
        sprintf(buffer, "%ld", filesize); //converts filesize to string
        printf("Sending file size to client: %s\n", buffer); //prints file size
        int bytes_sent = send(client_sockfd, buffer, strlen(buffer), 0); //sends file size
        if (bytes_sent <= 0) 
        {
                printf("Error: Failed to send file size (errno: %d - %s)\n", errno, strerror(errno));
                fclose(fp); //closes file
                return;
        }
        
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int bytes_received = recv(client_sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment
        if (bytes_received <= 0) 
        {
                printf("Error: No acknowledgment received from client (errno: %d - %s)\n", errno, strerror(errno));
                fclose(fp); //closes file
                return;
        }
        printf("Received acknowledgment from client: '%s'\n", buffer);
        
        rewind(fp); //rewinds to beginning of file
        
        long total_sent = 0; //total sent
        size_t bytes_read; //bytes read
        printf("Starting file transfer for '%s' (%ld bytes)\n", actual_path, filesize);
        // For small files, read the entire content and send it at once
        if (filesize <= BUFFER_SIZE) 
        {
                memset(buffer, 0, BUFFER_SIZE); //clears buffer
                bytes_read = fread(buffer, 1, filesize, fp); //reads file content
                //Checks if file content was read correctly
                if (bytes_read != filesize) 
                {
                        printf("Error: Could only read %zu/%ld bytes from file\n", bytes_read, filesize);
                } 
                else 
                {
                        printf("Read entire file content (%ld bytes): '%.*s'\n", filesize, (int)filesize, buffer);
                }
                bytes_sent = send(client_sockfd, buffer, bytes_read, 0); //sends file content
                //Checks if file content was sent correctly
                if (bytes_sent <= 0)
                {
                        printf("Error: Failed to send file data (errno: %d - %s)\n", errno, strerror(errno));
                }
                else
                {
                        printf("Sent %d bytes to client\n", bytes_sent);
                        total_sent = bytes_sent;
                }
        }
        else
        {
                // For larger files, read and send in chunks
                while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
                {
                        bytes_sent = send(client_sockfd, buffer, bytes_read, 0);
                        if (bytes_sent <= 0)
                        {
                                printf("Error: Failed to send file data at offset %ld (errno: %d - %s)\n", total_sent, errno, strerror(errno));
                                break;
                        }
                        //Checks if file content was sent correctly
                        if (bytes_sent != bytes_read)
                        {
                                printf("Warning: Partial send - only sent %d/%ld bytes\n", bytes_sent, bytes_read); //prints partial send
                        }
                        total_sent += bytes_sent; //updates total sent
                        //Prints transfer progress
                        if (total_sent % (BUFFER_SIZE * 10) == 0 || total_sent == filesize)
                        {
                                printf("\rTransferred: %ld/%ld bytes (%.1f%%)", total_sent, filesize, (float)total_sent / filesize * 100);
                                fflush(stdout); //flushes stdout
                        }
                }
        }
        printf("\nFile transfer complete: %ld/%ld bytes (%.1f%%)\n", total_sent, filesize, (float)total_sent / filesize * 100);
        if (total_sent < filesize)
        {
            printf("Warning: Incomplete file transfer for '%s'\n", actual_path);
        }
        
        fclose(fp); //closes file
}

//Handles remove command
void handleRemove(int sockfd, char *filepath)
{
        char actual_path[512]; 
        //Checks if path starts with ~S3/
        if (strncmp(filepath, "~S3/", 4) == 0)
        {
                sprintf(actual_path, "./%s", filepath + 1); //converts ~S3 to ./S3
        }
        else
        {
                sprintf(actual_path, "./S3/%s", filepath + 4); //converts ~S3 to ./S3
        }
        //Checks if file exists
        if (access(actual_path, F_OK) != 0)
        {
                send(sockfd, "File not found", 14, 0); //sends file not found
                return;
        }
        //Removes file
        if (unlink(actual_path) == 0)
        {
                send(sockfd, "File removed successfully", 24, 0); //sends file removed successfully
        }
        else
        {
                send(sockfd, "Failed to remove file", 21, 0); //sends failed to remove file
        }
}

// Function to handle list command (list files in directory)
void handleList(int sockfd, char *dirpath)
{
        char actual_path[512];
        sprintf(actual_path, "./S3/%s", dirpath); //converts ~S3 to ./S3
        printf("Listing TXT files in: %s\n", actual_path);
        
        char find_cmd[1024];
        sprintf(find_cmd, "find %s -type f -name \"*.txt\" 2>/dev/null | sort", actual_path); //finds TXT files
        
        FILE *find_pipe = popen(find_cmd, "r"); //opens pipe for find command
        if (find_pipe == NULL)
        {
                printf("Error executing find command\n");
                send(sockfd, "", 0, 0);
                return;
        }
        
        char file_list[BUFFER_SIZE] = ""; //clears file list
        char line[256];
        
        while (fgets(line, sizeof(line), find_pipe) != NULL)
        {
                line[strcspn(line, "\n")] = 0; //removes newline
                char *filename = strrchr(line, '/'); //extracts filename
                if (filename)
                {
                        filename++; //Skip the '/'
                }
                else
                {
                        filename = line; //No '/' found, use the whole string
                }
                printf("Found TXT file: %s\n", filename);
                strcat(file_list, filename); //adds filename to list
                strcat(file_list, "\n"); //adds newline to list
        }
        pclose(find_pipe); //closes pipe
        
        //Fallback to direct directory scan if find didn't work
        if (strlen(file_list) == 0) 
        {
                printf("No files found with find, trying direct directory scan\n");
                DIR *dir = opendir(actual_path); //opens directory
                if (dir == NULL)
                {
                        printf("Directory not found: %s\n", actual_path);
                        send(sockfd, "", 0, 0); //sends empty string
                        return;
                }
                
                char *txt_files[100]; //clears txt files
                int txt_count = 0; //clears txt count
                
                struct dirent *entry;
                //Reads directory entries
                while ((entry = readdir(dir)) != NULL && txt_count < 100)
                {
                        if (entry->d_type == DT_REG)
                        {
                                char *ext = strrchr(entry->d_name, '.'); //extracts file extension
                                //Checks if file extension is .txt
                                if (ext != NULL && strcmp(ext, ".txt") == 0)
                                {
                                        printf("Found TXT file: %s\n", entry->d_name); //prints found TXT file
                                        txt_files[txt_count++] = strdup(entry->d_name); //adds filename to list
                                }
                        }
                }
                closedir(dir); //closes directory
                qsort(txt_files, txt_count, sizeof(char*), compareStrings); //sorts txt files  
                
                file_list[0] = '\0'; //clears the string
                //Adds sorted txt files to list
                for (int i = 0; i < txt_count; i++)
                {
                        strcat(file_list, txt_files[i]); //adds filename to list
                        strcat(file_list, "\n"); //adds newline to list
                        free(txt_files[i]); //frees txt files
                }
        }
        send(sockfd, file_list, strlen(file_list), 0); //sends file list
}

// Function to handle tar command (create tar of TXT files)
void handleTar(int sockfd, char *filetype)
{
        //Checks if file type is .txt
        if (strcmp(filetype, ".txt") != 0)
        {
                printf("Unsupported file type for tar: %s (only .txt supported)\n", filetype);
                send(sockfd, "0", 1, 0); //sends 0
                return;
        }        
        printf("Creating tar of all TXT files in S3 directory\n");

        FILE *check = popen("find ./S3 -name \"*.txt\" -type f | wc -l", "r");
        char count_str[20];
        fgets(count_str, sizeof(count_str), check); //reads count string
        pclose(check); //closes check

        int file_count = atoi(count_str); //converts count string to int
        printf("Found %d .txt files in S3 directory\n", file_count);
        
        //If no .txt files exist, create a test file
        if (file_count == 0)
        {
            printf("No .txt files found, creating test file\n");
            mkdir("./S3", 0755);//creates S3 directory
            mkdir("./S3/test", 0755); //creates test directory

            FILE *test_fp = fopen("./S3/test/test.txt", "w"); //creates test file
            if (test_fp) 
            {
                    fprintf(test_fp, "This is a test file created for tar demonstration.\n"); //writes test file
                    fprintf(test_fp, "This file was automatically generated since no .txt files were found in the S3 directory.\n"); //writes test file
                    fclose(test_fp); //closes test file
                    printf("Created test file ./S3/test/test.txt\n"); //prints created test file
            } 
            else 
            {
                    printf("Error creating test file: %s\n", strerror(errno)); //prints error creating test file
            }
        }
        system("find ./S3 -name \"*.txt\" -type f | tar -cvf text.tar -T -"); //creates tar of all TXT files
        
        struct stat st;
        //Checks if tar file was created successfully
        if (stat("text.tar", &st) != 0 || st.st_size == 0) 
        {
                printf("Error creating tar file or no .txt files found\n"); //prints error creating tar file
                send(sockfd, "0", 1, 0); //sends 0
                return;
        }
        printf("Successfully created text.tar with size %ld bytes\n", (long)st.st_size); //prints successfully created tar file
        printf("Tar file contents:\n");
        system("tar -tvf text.tar");
        
        char buffer[BUFFER_SIZE];
        FILE *fp = fopen("text.tar", "rb"); //opens tar file
        if (fp == NULL) 
        {
                printf("Error opening tar file: %s\n", strerror(errno));
                send(sockfd, "0", 1, 0); //sends 0
                return;
        }
        
        fseek(fp, 0, SEEK_END); //moves to end of file
        long filesize = ftell(fp); //gets file size
        rewind(fp); //moves to beginning of file
        
        sprintf(buffer, "%ld", filesize); //converts file size to string
        printf("Sending file size to S1: %s\n", buffer);
        send(sockfd, buffer, strlen(buffer), 0); //sends file size
        
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        //Checks if acknowledgment was received from S1
        if (recv(sockfd, buffer, BUFFER_SIZE, 0) <= 0) 
        {
                printf("No acknowledgment received from S1\n");
                fclose(fp); //closes file
                unlink("text.tar"); //removes tar file
                return;
        }
        printf("Received acknowledgment from S1: %s\n", buffer);
        
        // Send file content in smaller chunks to avoid buffer issues
        long total_sent = 0;
        int bytes_read;
        int chunk_size = 1024; // Use 1KB chunks
        printf("Starting file transfer to S1\n");
        
        while ((bytes_read = fread(buffer, 1, chunk_size, fp)) > 0)
        {
            int bytes_sent = send(sockfd, buffer, bytes_read, 0); //sends file content
            if (bytes_sent <= 0)
            {
                    printf("Error sending file data: %s\n", strerror(errno));
                    break;
            }
            total_sent += bytes_sent; //updates total sent
            printf("\rTransferred: %ld/%ld bytes (%.1f%%)", total_sent, filesize, (float)total_sent / filesize * 100);
            fflush(stdout); //flushes stdout
        }
        printf("\nFile transfer complete: %ld/%ld bytes\n", total_sent, filesize);
        fclose(fp); //closes file

        printf("Keeping temporary tar file for debugging: %s\n", getcwd(NULL, 0));
}

int main()
{
        int server_fd, client_fd;
        struct sockaddr_in server_addr, client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        mkdir("S3", 0755); //creates S3 directory
        
        server_fd = socket(AF_INET, SOCK_STREAM, 0); //creates socket
        if (server_fd < 0) 
        {
                perror("Socket creation failed");
                exit(1);
        }
        
        memset(&server_addr, 0, sizeof(server_addr)); //clears server address
        server_addr.sin_family = AF_INET; //sets server address family
        server_addr.sin_port = htons(PORT); //sets server port
        server_addr.sin_addr.s_addr = INADDR_ANY; //sets server address
        
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
        {
                perror("Setsockopt failed");
                close(server_fd); //closes socket
                exit(1);
        }
        
        if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) 
        {
                perror("Bind failed");
                close(server_fd); //closes socket
                exit(1);
        }
        
        if (listen(server_fd, 5) < 0) 
        {
                perror("Listen failed");
                close(server_fd); //closes socket
                exit(1);
        }
        printf("Server S3 started. Listening on port %d...\n", PORT);
        
        while (1) 
        {
                client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len); //accepts connections
                if (client_fd < 0) 
                {
                        perror("Accept failed");
                        continue;
                }
                
                printf("S1 connected\n");
                handleClient(client_fd); //handles client request
                close(client_fd); //closes connection
        }
        close(server_fd); //closes server
        return 0;
}
