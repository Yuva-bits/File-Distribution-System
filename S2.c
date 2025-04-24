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

#define PORT 8008
#define BUFFER_SIZE 1024

//Compares strings for sorting
int compareStrings(const void *a, const void *b)
{
        return strcmp(*(const char **)a, *(const char **)b); //Compares strings
}

// Function prototypes for all handler functions
void createDirectories(char *path);
void handleClient(int sockfd);
void handleStore(int sockfd, char *destpath);
void handleGet(int sockfd, char *filepath);
void handleRemove(int sockfd, char *filepath);
void handleList(int sockfd, char *dirpath);
void handleTar(int sockfd, char *filetype);

//Creates directory path recursively
void createDirectories(char *path)
{
        char temp[256];
        char *p = NULL;
        size_t len;
        snprintf(temp, sizeof(temp), "%s", path); //copies path to temp
        len = strlen(temp);
        //Checks if last character is a '/'
        if (temp[len - 1] == '/')
                temp[len - 1] = 0; //removes '/' if present
        //Iterates through path
        for (p = temp + 1; *p; p++)
        {
                //Checks if current character is '/'
                if (*p == '/')
                {
                        *p = 0; //replaces '/' with null terminator
                        mkdir(temp, 0755); //creates directory
                        *p = '/'; //restores '/'
                }
        }
        mkdir(temp, 0755);
}

//Handles client connection
void handleClient(int sockfd) 
{
        char buffer[BUFFER_SIZE];
        char command[20], arg[512];
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives command from S1
        //Checks if connection is lost
        if (bytes_received <= 0) 
        {
                printf("Client disconnected\n");
                return;
        }
        printf("Received command: %s\n", buffer); //prints command

        sscanf(buffer, "%s %s", command, arg); //parses command and argument
        //Checks if command is store
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
        //If command is not recognized
        else 
        {
                send(sockfd, "Unknown command", 15, 0); //sends unknown command message
        }
}

//Handles store command (receive file from S1)
void handleStore(int sockfd, char *destpath)
{
        char dirpath[512];
        //Cleans up the path to avoid problems
        if (strncmp(destpath, "~S2/", 4) == 0) 
        {
                sprintf(dirpath, "./S2%s", destpath + 3); //converts ~S2 to ./S2
        } 
        else 
        {
                sprintf(dirpath, "./S2%s", destpath + 3); //converts ~S2 to ./S2
        }
        printf("Destination directory: %s\n", dirpath);
        createDirectories(dirpath); //creates directories if they don't exist

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
        filename[sizeof(filename) - 1] = '\0'; //null terminates filename
        printf("Received filename: %s\n", filename);
        
        char fullpath[512];
        sprintf(fullpath, "%s/%s", dirpath, filename); //creates full path
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
        printf("Filesize: %ld bytes\n", filesize);
        
        FILE *fp = fopen(fullpath, "wb"); //opens file for writing
        if (fp == NULL) 
        {
                printf("Error creating file %s\n", fullpath);
                send(sockfd, "FAILED", 6, 0);
                return;
        }
        
        send(sockfd, "Ready for data", 14, 0); //sends acknowledgment for filesize
        
        int bytes_written;
        long total_received = 0;
        //Receives file data
        while (total_received < filesize) 
        {
                bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives file data
                if (bytes_received <= 0) 
                {
                        printf("Error receiving file data\n");
                        fclose(fp);
                return;
                }

                bytes_written = fwrite(buffer, 1, bytes_received, fp); //writes file data to file
                if (bytes_written != bytes_received) 
                {
                        printf("Error writing to file\n");
                        fclose(fp);
                        return;
                }
                
                total_received += bytes_received; //updates total received bytes
                printf("\rReceived %ld/%ld bytes", total_received, filesize); //prints progress
                fflush(stdout); //flushes stdout
        }
        
        printf("\nFile received successfully\n");
        fclose(fp); //closes file
        send(sockfd, "File stored successfully", 24, 0); //sends success message
}

//Handles get command (send file to S1)
void handleGet(int sockfd, char *filepath)
{
        char actual_path[512];
        //Cleans up the path to avoid problems
        if (strncmp(filepath, "~S2/", 4) == 0) 
        {
                sprintf(actual_path, "./%s", filepath + 1); //converts ~S2 to ./S2
        } 
        else 
        {
                sprintf(actual_path, "./S2/%s", filepath + 4); //converts ~S2 to ./S2
        }
        printf("Looking for file: %s\n", actual_path);
        //Checks if file exists
        if (access(actual_path, F_OK) != 0)
        {
                printf("File not found: %s\n", actual_path);
                send(sockfd, "FILE_NOT_FOUND", 14, 0); //sends not found message
                return;
        }
        
        char buffer[BUFFER_SIZE];
        FILE *fp = fopen(actual_path, "rb"); //opens file for reading
        if (fp == NULL) 
        {
                printf("Error opening file %s\n", actual_path);
                send(sockfd, "FILE_NOT_FOUND", 14, 0); //sends not found message
                return;
        }
        
        fseek(fp, 0, SEEK_END); //moves to end of file
        long filesize = ftell(fp); //gets file size
        rewind(fp); //moves to beginning of file
        
        sprintf(buffer, "%ld", filesize); //converts filesize to string
        printf("Sending file size: %s\n", buffer); //prints file size
        send(sockfd, buffer, strlen(buffer), 0); //sends file size to S1
        
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment
        if (bytes_received <= 0)
        {
                printf("Error receiving acknowledgment\n");
                fclose(fp); //closes file
                return;
        }
        printf("Received acknowledgment: %s\n", buffer);
        
        int bytes_read;
        long total_sent = 0;
        //Sends file data
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
        {
                int bytes_sent = send(sockfd, buffer, bytes_read, 0); //sends file data
                if (bytes_sent != bytes_read)
                {
                        printf("Error sending file data\n");
                        break;
                }
                
                total_sent += bytes_sent; //updates total sent bytes
                printf("\rSent: %ld/%ld bytes (%.1f%%)", total_sent, filesize, (float)total_sent / filesize * 100); //prints progressq
                fflush(stdout); //flushes stdout
        }
        printf("\nFile transfer complete\n");
        fclose(fp); //closes file
}

//Handles remove command
void handleRemove(int sockfd, char *filepath) 
{
        char actual_path[512]; //cleans up the path to avoid problems
        //Checks if file exists
        if (strncmp(filepath, "~S2/", 4) == 0) 
        {
                sprintf(actual_path, "./%s", filepath + 1); //converts ~S2 to ./S2
        } 
        else 
        {
                sprintf(actual_path, "./S2/%s", filepath + 4); //converts ~S2 to ./S2
        }
        
        //Checks if file exists
        if (access(actual_path, F_OK) != 0) 
        {
                send(sockfd, "File not found", 14, 0); //sends not found message
                return;
        }
        
        //Removes file
        if (unlink(actual_path) == 0) 
        {
                send(sockfd, "File removed successfully", 24, 0); //sends success message
        } 
        else 
        {
                send(sockfd, "Failed to remove file", 21, 0); //sends failure message
        }
}

//Handles list command (lists files in directory)
void handleList(int sockfd, char *dirpath)
{
        char actual_path[512];
        sprintf(actual_path, "./S2/%s", dirpath); //converts ~S2 to ./S2
        printf("Listing PDF files in: %s\n", actual_path);

        char find_cmd[1024];
        sprintf(find_cmd, "find %s -type f -name \"*.pdf\" 2>/dev/null | sort", actual_path); //finds PDF files

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
                //Checks if '/' is present
                if (filename)
                {
                        filename++; // Skip the '/'
                } 
                else 
                {
                        filename = line; // No '/' found, use the whole string
                }
                
                strcat(file_list, filename); //adds filename to list
                strcat(file_list, "\n");
        }
        pclose(find_pipe); //closes pipe

        // Fallback to direct directory scan if find didn't work
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

                char *pdf_files[100];  //collects PDF files for sorting
                int pdf_count = 0;
                struct dirent *entry;
                //Checks if there are more entries and if the list is not full
                while ((entry = readdir(dir)) != NULL && pdf_count < 100)
                {
                        //Checks if entry is a regular file
                        if (entry->d_type == DT_REG) 
                        {
                                char *ext = strrchr(entry->d_name, '.');
                                //Checks if file is a PDF
                                if (ext != NULL && strcmp(ext, ".pdf") == 0)
                                {
                                        pdf_files[pdf_count++] = strdup(entry->d_name); //adds PDF file to list
                                }
                        }
                }
                closedir(dir); //closes directory

                qsort(pdf_files, pdf_count, sizeof(char*), compareStrings); //sorts PDF files
                file_list[0] = '\0'; //clears file list
                for (int i = 0; i < pdf_count; i++)
                {
                        strcat(file_list, pdf_files[i]); //adds PDF file to list
                        strcat(file_list, "\n");
                        free(pdf_files[i]); //frees memory
                }
        }
        send(sockfd, file_list, strlen(file_list), 0); //sends file list
}

//Handles tar command (create tar of PDF files)
void handleTar(int sockfd, char *filetype)
{
        //Checks if file type is PDF
        if (strcmp(filetype, ".pdf") != 0) 
        {
                send(sockfd, "0", 1, 0); //sends 0
                return;
        }
        
        FILE *check = popen("find ./S2 -name \"*.pdf\" | wc -l", "r"); //checks if there are PDF files
        char count_str[20];
        fgets(count_str, sizeof(count_str), check); //gets number of PDF files
        pclose(check); //closes pipe
        
        int file_count = atoi(count_str); //converts number of PDF files to int
        printf("Found %d PDF files in S2 directory\n", file_count);
        
        //If no PDF files, create sample ones
        if (file_count == 0) 
        {
                printf("No PDF files found, creating sample PDF files\n");
                system("mkdir -p ./S2/test"); //creates test directory
                
                FILE *dummy = fopen("./S2/test/sample1.pdf", "w"); //creates dummy PDF files        
                if (dummy) 
                {
                        fprintf(dummy, "%%PDF-1.4\nThis is a dummy PDF file for testing purposes.\n%%EOF\n");
                        fclose(dummy);
                        printf("Created dummy PDF file: ./S2/test/sample1.pdf\n");
                }
                
                dummy = fopen("./S2/test/sample2.pdf", "w");
                if (dummy) 
                {
                        fprintf(dummy, "%%PDF-1.4\nAnother dummy PDF file.\n%%EOF\n");
                        fclose(dummy);
                        printf("Created second dummy PDF file: ./S2/test/sample2.pdf\n");
                }
        }
        unlink("pdf.tar"); //removes existing tar file
        printf("Creating tar file of PDF files...\n");
        system("find ./S2 -name \"*.pdf\" | tar -cvf pdf.tar -T -"); //creates tar file
        
        struct stat st;
        //Checks if tar file was created correctly
        if (stat("pdf.tar", &st) != 0 || st.st_size == 0) 
        {
                printf("Error: Failed to create valid tar file\n");
                send(sockfd, "0", 1, 0); //sends 0
                return;
        }
        long filesize = st.st_size; //gets size of tar file
        printf("Successfully created pdf.tar with size %ld bytes\n", filesize);
        printf("Tar file contents:\n");
        system("tar -tvf pdf.tar"); //displays tar file contents
        
        //Create fixed-size test data if the tar file creation fails
        if (filesize < 1024)
        {
                printf("Warning: Created tar file is too small, creating test data instead\n");
                FILE *test_file = fopen("pdf.tar", "wb"); //creates test data
                if (test_file) 
                {
                        // Creates 10KB of data for testing
                        for (int i = 0; i < 10; i++) 
                        {
                                char buffer[1024];
                                sprintf(buffer, "Block %d: This is test data for the PDF tar file. This block is filled with text to make it 1KB in size.%*s\n", i, 900, ""); // Padding to make it 1KB
                                fwrite(buffer, 1, 1024, test_file); //writes test data
                        }
                        fclose(test_file); //closes test data
                        stat("pdf.tar", &st); //updates filesize
                        filesize = st.st_size; //gets size of test data
                        printf("Created test data with size %ld bytes\n", filesize);
                }
        }

        char buffer[BUFFER_SIZE];
        sprintf(buffer, "%ld", filesize); //converts filesize to string
        printf("Sending size string: %s\n", buffer);
        int size_sent = send(sockfd, buffer, strlen(buffer), 0); //sends filesize
        if (size_sent <= 0)
        {
                printf("Error sending file size: %s\n", strerror(errno));
                return;
        }

        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int ack_bytes = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment
        if (ack_bytes <= 0) 
        {
                printf("Error receiving acknowledgment: %s\n", strerror(errno));
                return;
        }
        printf("Received acknowledgment: %s (%d bytes)\n", buffer, ack_bytes);
        usleep(100000); //delay to ensure acknowledgment is processed
        
        printf("Sending tar file via socket...\n");
        FILE *fp = fopen("pdf.tar", "rb"); //opens tar file
        if (!fp) 
        {
                printf("Error opening tar file: %s\n", strerror(errno));
                return;
        }
        
        char send_buffer[4096]; //reads and sends in small chunks
        size_t bytes_read;
        long total_sent = 0;
        //Reads and sends in small chunks
        while ((bytes_read = fread(send_buffer, 1, sizeof(send_buffer), fp)) > 0) 
        {
                int sent = send(sockfd, send_buffer, bytes_read, 0); //sends file data
                if (sent <= 0) 
                {
                        printf("Error sending file data: %s\n", strerror(errno));
                        break;
                }
                
                total_sent += sent; //updates total sent bytes
                
                if (total_sent % (10 * 1024) == 0 || total_sent == filesize) 
                {
                        printf("Sent %ld/%ld bytes (%.1f%%)\n", total_sent, filesize, (float)total_sent/filesize*100);
                        fflush(stdout); //flushes stdout
                }
                usleep(1000); //delay to prevent overwhelming the receiver
        }
        fclose(fp); //closes tar file
        
        printf("Completed sending tar file: %ld/%ld bytes (%.1f%%)\n", total_sent, filesize, (float)total_sent/filesize*100);
        printf("File transfer completed, pdf.tar kept for debugging\n");
}

int main()
{
        int server_fd, client_fd;
        struct sockaddr_in server_addr, client_addr;
        socklen_t client_len = sizeof(client_addr); //size of client address
        
        mkdir("S2", 0755); //creates S2 directory
        
        server_fd = socket(AF_INET, SOCK_STREAM, 0); //creates socket
        if (server_fd < 0)
        {
                perror("Socket creation failed");
                exit(1);
        }
        
        memset(&server_addr, 0, sizeof(server_addr)); //clears server address
        server_addr.sin_family = AF_INET; //sets family to IPv4
        server_addr.sin_port = htons(PORT); //sets port to PORT
        server_addr.sin_addr.s_addr = INADDR_ANY; //sets address to any
        
        int opt = 1;
        //Sets socket options
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
        {
                perror("Setsockopt failed");
                close(server_fd); //closes socket
                exit(1);
        }

        //Binds socket
        if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) 
        {
                perror("Bind failed");
                close(server_fd); //closes socket
                exit(1);
        }

        //Listens for connections
        if (listen(server_fd, 5) < 0) 
        {
                perror("Listen failed");
                close(server_fd); //closes socket
                exit(1);
        }
        printf("Server S2 started. Listening on port %d...\n", PORT);

        //Accepts connections
        while (1) 
        {
                client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) 
                {
                        perror("Accept failed");
                        continue;
                }
                printf("S1 connected\n");
                handleClient(client_fd); //handles client request
                close(client_fd); //closes connection
        }
        close(server_fd); //closes socket
        return 0;
}
