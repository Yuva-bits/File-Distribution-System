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

#define PORT 8010
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
        
        if (temp[len - 1] == '/')
                temp[len - 1] = 0;
        //Creates directories recursively
        for (p = temp + 1; *p; p++)
        {
                //Checks if current character is a '/'
                if (*p == '/')
                {
                        *p = 0; //sets current character to null
                        mkdir(temp, 0755); //creates directory
                        *p = '/'; //sets current character to '/'
                }
        }
        mkdir(temp, 0755);
}

//Handles client connection
void handleClient(int sockfd) 
{
        char buffer[BUFFER_SIZE];
        char command[20] = "";
        char arg[512] = "";
        
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives command
        if (bytes_received <= 0)
        {
                printf("Client disconnected\n");
                return;
        }
        printf("Received command: %s\n", buffer);
        sscanf(buffer, "%s %s", command, arg); //scans command and argument

        //Checks if command is store
        if (strcmp(command, "store") == 0)
        {
                printf("Store command - path: %s\n", arg);
                send(sockfd, "Ready for filename", 18, 0); //sends acknowledgment
                handleStore(sockfd, arg);
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
                printf("List command - path: '%s'\n", arg);
                //Checks if path is empty or just a space
                if (strlen(arg) == 0 || strcmp(arg, " ") == 0) 
                {
                        arg[0] = '\0';  // Ensures it's an empty string
                        printf("Empty path detected, listing root directory\n");
                }
                handleList(sockfd, arg); //handles list command
        }
        //Checks if command is tar
        else if (strcmp(command, "tar") == 0)
        {
                handleTar(sockfd, arg); //handles tar command
        }
        else
        {
                printf("Unknown command: %s\n", command);
                send(sockfd, "Unknown command", 15, 0); //sends unknown command
        }
}

//Handles store command (receive file from S1)
void handleStore(int sockfd, char *destpath)
{
        char dirpath[512];
        
        //Cleans up the path to avoid problems
        if (strncmp(destpath, "~S4/", 4) == 0) 
        {
                sprintf(dirpath, "./S4%s", destpath + 3); //converts ~S4 to ./S4
        }
        else
        {
                sprintf(dirpath, "./S4%s", destpath + 3); //converts ~S4 to ./S4
        }
        printf("Destination directory: %s\n", dirpath); //prints destination directory
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
        filename[sizeof(filename) - 1] = '\0'; //null-terminates filename
        printf("Received filename: %s\n", filename);
        
        char fullpath[512];
        sprintf(fullpath, "%s/%s", dirpath, filename); //creates full path
        printf("Full path for file: %s\n", fullpath);
        send(sockfd, "Ready for filesize", 18, 0); //sends acknowledgment
        
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives filesize
        if (bytes_received <= 0)
        {
                printf("Error receiving filesize\n");
                return;
        }
        
        long filesize = atol(buffer); //converts filesize to long
        printf("Filesize: %ld bytes\n", filesize);
        FILE *fp = fopen(fullpath, "wb"); //creates file
        if (fp == NULL)
        {
                printf("Error creating file %s\n", fullpath);
                send(sockfd, "FAILED", 6, 0); //sends failed
                return;
        }
        send(sockfd, "Ready for data", 14, 0); //sends acknowledgment
        
        int bytes_written; //bytes written
        long total_received = 0; //total received
        //Receives file content
        while (total_received < filesize)
        {
                bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives file data
                if (bytes_received <= 0) 
                {
                        printf("Error receiving file data\n");
                        fclose(fp); //closes file
                        return;
                }

                bytes_written = fwrite(buffer, 1, bytes_received, fp); //writes to file
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
        fclose(fp);
        send(sockfd, "File stored successfully", 24, 0);
}

// Function to handle get command (send file to S1)
void handleGet(int sockfd, char *filepath)
{
        char actual_path[512];
        //Converts ~S4 to ./S4
        if (strncmp(filepath, "~S4/", 4) == 0)
        {
                sprintf(actual_path, "./%s", filepath + 1); //converts ~S4 to ./S4
        }
        else
        {
                sprintf(actual_path, "./S4/%s", filepath + 4); //converts ~S4 to ./S4
        }
        printf("Looking for file: %s\n", actual_path);
        
        //Checks if file
        if (access(actual_path, F_OK) != 0)
        {
                printf("File not found: %s\n", actual_path);
                send(sockfd, "FILE_NOT_FOUND", 14, 0); //sends file not found
                return;
        }
        
        char buffer[BUFFER_SIZE];
        FILE *fp = fopen(actual_path, "rb"); //opens file
        if (fp == NULL)
        {
                printf("Error opening file %s\n", actual_path);
                send(sockfd, "FILE_NOT_FOUND", 14, 0); //sends file not found
                return;
        }
        
        fseek(fp, 0, SEEK_END); //seeks to end of file
        long filesize = ftell(fp); //gets file size
        rewind(fp); //rewinds to beginning of file
        
        sprintf(buffer, "%ld", filesize); //converts filesize to string
        printf("Sending file size: %s\n", buffer);
        send(sockfd, buffer, strlen(buffer), 0); //sends file size
        
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment
        if (bytes_received <= 0)
        {
                printf("Error receiving acknowledgment\n");
                fclose(fp); //closes file
                return;
        }
        printf("Received acknowledgment: %s\n", buffer);
        
        int bytes_read; //bytes read
        long total_sent = 0; //total sent
        
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
        {
                int bytes_sent = send(sockfd, buffer, bytes_read, 0); //sends file data
                if (bytes_sent != bytes_read)
                {
                        printf("Error sending file data\n");
                        break;
                }
                total_sent += bytes_sent; //updates total sent
                printf("\rSent: %ld/%ld bytes (%.1f%%)", total_sent, filesize, (float)total_sent / filesize * 100); //prints total sent
                fflush(stdout); //flushes stdout
        }
        
        printf("\nFile transfer complete\n");
        fclose(fp); //closes file
}

// Function to handle remove command
void handleRemove(int sockfd, char *filepath)
{
        char actual_path[512];
        if (strncmp(filepath, "~S4/", 4) == 0)
        {
                sprintf(actual_path, "./%s", filepath + 1); //converts ~S4 to ./S4
        }
        else
        {
                sprintf(actual_path, "./S4/%s", filepath + 4); //converts ~S4 to ./S4
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
        //Handles empty path or just '/'
        if (strlen(dirpath) == 0 || strcmp(dirpath, "/") == 0)
        {
                sprintf(actual_path, "./S4"); //converts ~S4 to ./S4
        }
        else
        {
                sprintf(actual_path, "./S4/%s", dirpath); //converts ~S4 to ./S4
        }
        printf("Listing ZIP files in: %s\n", actual_path);

        char find_cmd[1024];
        sprintf(find_cmd, "find %s -type f -name \"*.zip\" 2>/dev/null | sort", actual_path);
        FILE *find_pipe = popen(find_cmd, "r"); //executes find command
        if (find_pipe == NULL)
        {
                printf("Error executing find command\n");
                send(sockfd, "", 0, 0); //sends empty string
                return;
        }
        
        char file_list[BUFFER_SIZE] = ""; //file list
        char line[256];
        while (fgets(line, sizeof(line), find_pipe) != NULL)
        {
                line[strcspn(line, "\n")] = 0; //removes newline
                char *filename = strrchr(line, '/'); //extracts filename
                if (filename) 
                {
                        filename++; //skips '/'
                } 
                else 
                {
                        filename = line; //uses whole string
                }
                printf("Found ZIP file: %s\n", filename);

                strcat(file_list, filename); //adds filename to file list
                strcat(file_list, "\n"); //adds newline to file list
        }
        pclose(find_pipe); //closes find pipe
        
        //Checks if file list is empty
        if (strlen(file_list) == 0) 
        {
                printf("No files found with find, trying direct directory scan\n");
                DIR *dir = opendir(actual_path);
                if (dir == NULL)
                {
                        printf("Directory not found: %s\n", actual_path);
                        send(sockfd, "", 0, 0); //sends empty string
                        return;
                }

                char *zip_files[100];
                int zip_count = 0;
                struct dirent *entry;
                //Reads directory entries
                while ((entry = readdir(dir)) != NULL && zip_count < 100)
                {
                        //Checks if entry is a regular file
                        if (entry->d_type == DT_REG) 
                        {
                                char *ext = strrchr(entry->d_name, '.'); //extracts file extension
                                //Checks if file extension is .zip
                                if (ext != NULL && strcmp(ext, ".zip") == 0) 
                                {
                                    printf("Found ZIP file: %s\n", entry->d_name);
                                    zip_files[zip_count++] = strdup(entry->d_name); //adds filename to zip files
                                }
                        }
                }
                closedir(dir); //closes directory

                qsort(zip_files, zip_count, sizeof(char*), compareStrings); //sorts zip files

                file_list[0] = '\0'; //clears string
                for (int i = 0; i < zip_count; i++) 
                {
                        strcat(file_list, zip_files[i]); //adds filename to file list
                        strcat(file_list, "\n"); //adds newline to file list
                        free(zip_files[i]); //frees allocated memory
                }
        }

        printf("ZIP files found: %s\n", file_list);
        if (strlen(file_list) == 0)
        {
                printf("No ZIP files found in directory\n");
        }
        send(sockfd, file_list, strlen(file_list), 0); //sends file list
}

//Handles tar command (create tar of ZIP files)
void handleTar(int sockfd, char *filetype)
{
        //Checks if file type is .zip
        if (strcmp(filetype, ".zip") != 0)
        {
                send(sockfd, "0", 1, 0); //sends 0
                return;
        }
        system("find ./S4 -name \"*.zip\" | tar -cf zip.tar -T -"); //

        char buffer[BUFFER_SIZE];
        FILE *fp = fopen("zip.tar", "rb"); //opens tar file
        if (fp == NULL) 
        {
                printf("Error creating tar file\n");
                send(sockfd, "0", 1, 0); //sends 0
                return;
        }
        
        fseek(fp, 0, SEEK_END); //seeks to end of file
        long filesize = ftell(fp); //gets file size
        rewind(fp); //rewinds to beginning of file

        sprintf(buffer, "%ld", filesize); //converts filesize to string
        send(sockfd, buffer, BUFFER_SIZE, 0); //sends file size
        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment

        int bytes_read;
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
        {
                send(sockfd, buffer, bytes_read, 0); //sends file content
        }
        
        fclose(fp); //closes tar file
        printf("zip.tar kept for debugging\n");
}

int main() 
{
        int server_fd, client_fd;
        struct sockaddr_in server_addr, client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        mkdir("S4", 0755); //creates S4 directory
        
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
        //Sets socket options
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
        {
                perror("Setsockopt failed");
                close(server_fd); //closes server socket
                exit(1);
        }
        
        //Binds socket
        if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) 
        {
                perror("Bind failed");
                close(server_fd); //closes server socket
                exit(1);
        }
        
        //Listens for connections
        if (listen(server_fd, 5) < 0) 
        {
                perror("Listen failed");
                close(server_fd); //closes server socket
                exit(1);
        }
        printf("Server S4 started. Listening on port %d...\n", PORT);
        
        // Accept connections
        while (1)
        {
                client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len); //accepts connection
                if (client_fd < 0) 
                {
                        perror("Accept failed");
                        continue;
                }
                printf("S1 connected\n");
                handleClient(client_fd); //handles client request
                close(client_fd); //closes connection
        }
        
        close(server_fd);
        return 0;
}
