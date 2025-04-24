#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8007
#define BUFFER_SIZE 1024
#define MAXPATHLEN 256

// Function prototypes
int verifyCommand(char* command, char* arg1, char* arg2);
void sendFile(int sockfd, char* filename);
void getFile(int sockfd, char* filename);
void getTarFile(int sockfd, char* filename, long expected_size);
void cleanupFile(char* filename);

//Verifies if command syntax is correct
int verifyCommand(char* command, char* arg1, char* arg2) 
{
        //Checks if command is uploadf
        if (strcmp(command, "uploadf") == 0) 
        {
                //Checks if file exists in current directory
                if (access(arg1, F_OK) != 0) 
                {
                        printf("Error: File %s does not exist\n", arg1);
                        return 0;
                }
                char* ext = strrchr(arg1, '.'); //checks if file has a valid extension
                if (ext == NULL || (strcmp(ext, ".c") != 0 && strcmp(ext, ".pdf") != 0 && strcmp(ext, ".txt") != 0 && strcmp(ext, ".zip") != 0))
                {
                        printf("Warning: File type %s is not officially supported (.c, .pdf, .txt, .zip recommended)\n", 
                        ext ? ext : "unknown");
                }
                //Checks if destination path starts with ~S1/
                if (strncmp(arg2, "~S1/", 4) != 0) 
                {
                        printf("Error: Destination path must start with ~S1/\n");
                        return 0;
                }
                return 1;
        }
        //Checks if command is downlf
        else if (strcmp(command, "downlf") == 0)
        {
                //Checks if file path starts with ~S1/
                if (strncmp(arg1, "~S1/", 4) != 0)
                {
                        printf("Error: File path must start with ~S1/\n");
                        return 0;
                }                
                return 1;
        } 
        //Checks if command is removef
        else if (strcmp(command, "removef") == 0) 
        {
                //Checks if file path starts with ~S1/
                if (strncmp(arg1, "~S1/", 4) != 0)
                {
                        printf("Error: File path must start with ~S1/\n");
                        return 0;
                }
                return 1;
        }
        //Checks if command is downltar
        else if (strcmp(command, "downltar") == 0) 
        {
                //Checks if filetype is valid (.c, .pdf, .txt, .zip)
                if (strcmp(arg1, ".c") != 0 && strcmp(arg1, ".pdf") != 0 && strcmp(arg1, ".txt") != 0 && strcmp(arg1, ".zip") != 0) 
                {
                        printf("Error: Only .c, .pdf, .txt, and .zip file types are supported for downltar\n");
                        return 0;
                }
                return 1;
        }
        //Checks if command is dispfnames or dispfanmes
        else if (strcmp(command, "dispfnames") == 0 || strcmp(command, "dispfanmes") == 0) 
        {
                //Checks if dispfanmes is used
                if (strcmp(command, "dispfanmes") == 0) 
                {
                        printf("Note: Using 'dispfnames' instead of 'dispfanmes'\n");
                }
                //Checks if path starts with ~S1/
                if (strncmp(arg1, "~S1/", 4) != 0)
                {
                        printf("Error: Path must start with ~S1/\n");
                        return 0;
                }
                return 1;
        } 
        else
        {
                printf("Error: Unknown command\n");
                return 0;
        }
}

//Sends file to server
void sendFile(int sockfd, char* filename) 
{
        char buffer[BUFFER_SIZE];
        FILE* fp = fopen(filename, "rb"); //opens file in binary mode
        if (fp == NULL) 
        {
                printf("Error opening file %s\n", filename);
                return;
        }
        
        fseek(fp, 0, SEEK_END); //moves pointer to end of file
        long filesize = ftell(fp); //gets size of file
        rewind(fp); //moves pointer to beginning of file
        
        sprintf(buffer, "%ld", filesize); //converts size to string
        send(sockfd, buffer, BUFFER_SIZE, 0); //sends size to server
        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment

        int bytes_read;
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) 
        {
                send(sockfd, buffer, bytes_read, 0); //sends file content to server
        }
        fclose(fp); //closes file
}

// Function to receive file from server
void getFile(int sockfd, char* filename)
{
        char buffer[BUFFER_SIZE];
        struct stat st;
        //Checks if file exists and has content
        if (stat(filename, &st) == 0 && st.st_size > 0)
        {
                remove(filename); //removes file if it exists
        }

        FILE* fp = fopen(filename, "wb"); //opens file in binary mode
        if (fp == NULL)
        {
                printf("Error creating/opening file %s: %s\n", filename, strerror(errno));
                return;
        }

        struct timeval tv;
        tv.tv_sec = 60;  // 60 second timeout (increased from 30)
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv); //
        
        int bytes_received;
        long total_received = 0;
        int data_chunks = 0;
        //Receives data until connection closes or times out
        while (1) 
        {
                memset(buffer, 0, sizeof(buffer)); //clears buffer
                bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0); //receives data
                //Checks if connection closed or timed out
                if (bytes_received <= 0)
                {
                        //Checks if connection closed normally
                        if (bytes_received == 0)
                        {
                                break;
                        }
                        //Checks if timeout occurred
                        else if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                                //Checks if we received absolutely no data
                                if (data_chunks == 0)
                                {
                                        printf("No data received before timeout\n");
                                }
                                //Checks if we received some data
                                else
                                {
                                        break;
                                }
                                break;
                        }
                        //Checks if real error occurred
                        else
                        {
                                break;
                        }
                }
                buffer[bytes_received] = '\0'; //null-terminates buffer
                
                const char* markers[] = {
                    "<!---",                          //comment start
                    "END_OF_FILE_TRANSFER_MARKER",    //main marker
                    "<<END_FILE_DATA>>",              //end tag
                    "\n<!---",                        //newline + comment
                    "\n---",                          //markdown separator
                    "--->"                            //comment end
                }; //array of markers to check for
                int num_markers = sizeof(markers) / sizeof(markers[0]); //gets number of markers
                
                char* first_marker = NULL;
                int marker_pos = bytes_received;  //start with end of buffer
                //Checks for first occurrence of any marker
                for (int i = 0; i < num_markers; i++)
                {
                        char* found = strstr(buffer, markers[i]);
                        //Checks if marker is found and if it is, sets first marker and marker position
                        if (found && (found - buffer) < marker_pos)
                        {
                                first_marker = found; //sets first marker
                                marker_pos = found - buffer; //sets marker position
                        }
                }
                
                int bytes_to_write = bytes_received;
                //Checks if we have valid data
                if (first_marker)
                {
                        bytes_to_write = first_marker - buffer; //sets bytes to write
                }
                
                //Checks if we have valid data
                if (bytes_to_write > 0)
                {
                        int bytes_written = fwrite(buffer, 1, bytes_to_write, fp); //writes data to file
                        //Checks if we wrote all the data
                        if (bytes_written != bytes_to_write)
                        {
                                printf("Error writing to file: %s\n", strerror(errno)); //prints error
                                break;
                        }
                        total_received += bytes_written; //updates total received
                        data_chunks++; //updates data chunks
                }
                
                //Checks if we found the end marker
                if (first_marker)
                {
                        break; //ends loop
                }
        }
        fflush(fp); //flushes file
        fclose(fp); //closes file

        struct stat st_verify;
        //Checks if file was created and has content
        if (stat(filename, &st_verify) == 0)
        {
                printf("File transfer complete: %ld bytes received\n", total_received);
        }
        else
        {
                printf("ERROR: Could not verify file %s: %s\n", filename, strerror(errno));
        }
}

//Utility function to clean up any existing file by removing end markers
void cleanupFile(char* filename) 
{
        //Checks if file exists
        if (access(filename, F_OK) != 0) 
        {
                return; // File doesn't exist, nothing to do
        }
    
        FILE* fp = fopen(filename, "rb"); //opens file in binary mode
        if (!fp) 
        {
                return;
        }
        
        fseek(fp, 0, SEEK_END); //moves pointer to end of file
        long filesize = ftell(fp); //gets size of file
        rewind(fp); //moves pointer to beginning of file
        
        //Checks if file is empty
        if (filesize == 0) 
        {
                fclose(fp); //closes file
                return;
        }
        
        char* content = (char*)malloc(filesize + 1); //allocates memory for content
        if (!content) 
        {
                fclose(fp); //closes file
                return; // Memory allocation failed
        }

        size_t bytes_read = fread(content, 1, filesize, fp); //reads file content
        fclose(fp); //closes file
        if (bytes_read != filesize) 
        {
                free(content); //frees memory
                return;
        }
        content[filesize] = '\0'; //null-terminates content

        //Checks for various marker patterns that could indicate the end of data
        const char* markers[] = {
            "<!---",                          //comment start
            "END_OF_FILE_TRANSFER_MARKER",    //main marker
            "<<END_FILE_DATA>>",              //end tag
            "\n<!---",                        //newline + comment
            "\n---",                          //markdown separator
            "--->"                            //comment end
        }; //array of markers to check for
        int num_markers = sizeof(markers) / sizeof(markers[0]); //gets number of markers

        char* first_marker = NULL;
        long marker_pos = filesize;  // Start with end of file
        //Checks for first occurrence of any marker
        for (int i = 0; i < num_markers; i++) 
        {
                char* found = strstr(content, markers[i]);
                //Checks if marker is found and if it is, sets first marker and marker position
                if (found && (found - content) < marker_pos) 
                {
                        first_marker = found; //sets first marker
                        marker_pos = found - content; //sets marker position
                }
        }
        if (!first_marker)
        {
                free(content); //frees memory
                return;
        }
        
        FILE* temp_fp = fopen("temp_file", "wb"); //opens temp file in binary mode
        if (!temp_fp) 
        {
                free(content); //frees memory
                return;
        }
        
        size_t bytes_to_write = first_marker - content; //sets bytes to write
        size_t bytes_written = fwrite(content, 1, bytes_to_write, temp_fp); //writes data to temp file
        fclose(temp_fp); //closes temp file
        if (bytes_written != bytes_to_write) 
        {
                free(content); //frees memory
                remove("temp_file"); //removes temp file
                return; // Writing failed
        }

        remove(filename); //removes original file
        rename("temp_file", filename); //renames temp file to original file
        free(content); //frees memory
}

//Special receive function for tar files
void getTarFile(int sockfd, char* filename, long expected_size)
{
        printf("Starting tar file reception, expected size: %ld bytes\n", expected_size);
        int error = 0;
        socklen_t len = sizeof(error); //gets length of error
        //Checks if socket has error state
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0)
        {
                //Checks if error is not 0
                if (error != 0) 
                {
                        printf("Socket has error state: %s\n", strerror(error));
                }
        }
        
        int rcvbuf_size = 262144; //sets buffer size to 256KB
        setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)); //sets buffer size
        
        int keepalive = 1; //sets keep-alive to 1
        setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)); //sets keep-alive
        
        //Checks if file exists and removes it
        if (access(filename, F_OK) == 0) 
        {
                //Checks if file can be removed
                if (remove(filename) != 0) 
                {
                        printf("Warning: Failed to remove existing file: %s\n", strerror(errno));
                }
        }
        
        FILE* fp = fopen(filename, "wb"); //opens file in binary mode
        //Checks if file can be opened
        if (fp == NULL) 
        {
                printf("Error creating/opening file %s: %s\n", filename, strerror(errno));
                return;
        }
        
        struct timeval tv;
        tv.tv_sec = 120;  //120 second timeout
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)); //sets timeout
        
        char *recv_buffer = malloc(16384); //16KB buffer
        if (!recv_buffer) 
        {
                printf("Error allocating memory for buffer\n");
                fclose(fp);
                return;
        }
        
        int bytes_received;
        long total_received = 0;
        int chunks_received = 0;
        bool direct_transfer_mode = false; //checks if direct transfer mode is on
        long new_expected_size = expected_size; //sets new expected size to expected size
        
        while (total_received < new_expected_size) 
        {
                memset(recv_buffer, 0, 16384); //clears buffer
                bytes_received = recv(sockfd, recv_buffer, 16384, 0); //receives data
                //Checks if data was received
                if (bytes_received > 0) 
                {
                        //Checks for direct transfer marker
                        if (!direct_transfer_mode && strstr(recv_buffer, "<<DIRECT_FILE_TRANSFER>>") != NULL)
                        {
                                printf("Detected direct file transfer mode\n");
                                direct_transfer_mode = true;
                                fclose(fp); //closes file

                                fp = fopen(filename, "wb"); //opens file in binary mode
                                if (fp == NULL) 
                                {
                                        printf("Error reopening file: %s\n", strerror(errno));
                                        free(recv_buffer);
                                        return;
                                }
                                continue;
                        }

                        //Checks if we are in direct transfer mode and if we just received the size
                        if (direct_transfer_mode && total_received == 0) 
                        {
                                bool is_size = true; //checks if string is a size
                                //Checks if string is a size
                                for (int i = 0; i < bytes_received; i++) 
                                {
                                        //Checks if end of string is reached
                                        if (recv_buffer[i] == 0) 
                                        {
                                                break; //End of string
                                        }
                                        //Checks if character is not a digit
                                        if (!isdigit(recv_buffer[i])) 
                                        {
                                                is_size = false; //sets is_size to false
                                                break;
                                        }
                                }
                                //Checks if string is a size
                                if (is_size) 
                                {
                                        new_expected_size = atol(recv_buffer); //parses new file size
                                        printf("Received new file size: %ld bytes\n", new_expected_size);
                                        total_received = 0; //Reset counter
                                        continue;
                                }
                        }
                        
                        //Checks for end marker
                        if (strstr(recv_buffer, "END_OF_FILE_TRANSFER_MARKER") != NULL)
                        {
                                char *marker = strstr(recv_buffer, "END_OF_FILE_TRANSFER_MARKER"); //checks for end marker
                                //Checks if marker is found and if it is, writes data before the marker
                                if (marker != NULL && marker > recv_buffer)
                                {
                                        size_t bytes_to_write = marker - recv_buffer; //sets bytes to write
                                        fwrite(recv_buffer, 1, bytes_to_write, fp); //writes data to file
                                        total_received += bytes_to_write; //updates total received
                                }
                                break;
                        }
                        
                        size_t bytes_written = fwrite(recv_buffer, 1, bytes_received, fp); //writes data to file
                        //Checks if all data was written
                        if (bytes_written != bytes_received) 
                        {
                                printf("Error: Failed to write all received data to file\n");
                                break;
                        }
                        total_received += bytes_received; //updates total received
                        chunks_received++; //updates chunks received
                }
                //Checks if server closed connection
                else if (bytes_received == 0)
                {
                        printf("\nServer closed connection after %ld/%ld bytes\n", total_received, new_expected_size); //\
                        break;
                }
                //Checks if timeout occurred
                else 
                {
                        //Checks if timeout occurred
                        if (errno == EAGAIN || errno == EWOULDBLOCK) 
                        {
                                //Checks if total received is greater than 0
                                if (total_received > 0) 
                                {
                                        printf("\nTimeout but received %ld bytes. Assuming transfer complete.\n", total_received);
                                }
                                else
                                {
                                        printf("\nTimeout with no data received\n");
                                }
                                break;
                        }
                        else
                        {
                                printf("\nError receiving data: %s\n", strerror(errno));
                                break;
                        }
                }
        }
        free(recv_buffer); //frees memory
        fflush(fp); //flushes file

        int fd = fileno(fp); //gets file descriptor
        //Checks if file descriptor is valid
        if (fd >= 0) 
        {
                fsync(fd); //flushes file
        }
        fclose(fp); //closes file
        printf("File transfer complete: %ld bytes received\n", total_received);

        struct stat st;
        //Checks if file exists
        if (stat(filename, &st) == 0)
        {
                //Checks if file size matches total received
                if ((long)st.st_size != total_received)
                {
                        printf("Warning: File size (%ld) doesn't match bytes received (%ld)\n", (long)st.st_size, total_received);
                }

                //Checks if tar file contains actual data
                if (st.st_size > 0)
                {
                        FILE *check = fopen(filename, "rb"); //opens file in binary mode
                        if (check)
                        {
                                char test_buffer[512];
                                size_t test_bytes = fread(test_buffer, 1, sizeof(test_buffer), check); //reads file into buffer
                                fclose(check); //closes file
                                //Checks if file is empty
                                if (test_bytes == 0)
                                {
                                        printf("Warning: File seems to be empty\n");
                                }
                        }
                }
                else
                {
                        printf("Error: File is empty\n");
                }
        }
        else
        {
                printf("Error: File verification failed: %s\n", strerror(errno));
        }
}

int main()
{
        int sockfd;
        struct sockaddr_in server_addr;
        char buffer[BUFFER_SIZE];
        char command[20], arg1[256], arg2[256];
        
        sockfd = socket(AF_INET, SOCK_STREAM, 0); //creates socket
        if (sockfd < 0) 
        {
                perror("Socket creation failed");
                exit(1);
        }
        
        memset(&server_addr, 0, sizeof(server_addr)); //clears server address
        server_addr.sin_family = AF_INET; //sets family to internet
        server_addr.sin_port = htons(SERVER_PORT); //sets port to server port
        server_addr.sin_addr.s_addr = inet_addr(SERVER_IP); //sets address to server ip
        
        //Connects to server and checks if connection failed
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        {
                perror("Connection failed");
                exit(1);
        }
        printf("Connected to server S1\n");
        
        while (1)
        {
                printf("w25clients$ ");
                fgets(buffer, BUFFER_SIZE, stdin);
                buffer[strcspn(buffer, "\n")] = 0; // Remove newline
                
                int result;
                result = sscanf(buffer, "%s %s %s", command, arg1, arg2); //scans buffer for command, arg1, and arg2
                if (result < 1)
                        continue;
                //Checks if command is uploadf
                if (strcmp(command, "uploadf") == 0)
                {
                        if (result < 3)
                        {
                                printf("Usage: uploadf filename destination_path\n");
                                continue;
                        }
                        //Checks if command is valid
                        if (!verifyCommand(command, arg1, arg2))
                                continue;

                        printf("Uploading file %s to %s...\n", arg1, arg2);
                        send(sockfd, buffer, strlen(buffer), 0); //sends command to server
                        sendFile(sockfd, arg1); //sends file to server

                        memset(buffer, 0, BUFFER_SIZE); //clears buffer
                        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives response from server
                        printf("Server response: %s\n", buffer);
                }
                //Checks if command is downlf
                else if (strcmp(command, "downlf") == 0) 
                {
                        //Checks if command is valid
                        if (result < 2)
                        {
                                printf("Usage: downlf filename\n");
                                continue;
                        }
                        if (!verifyCommand(command, arg1, NULL))
                                continue;

                        printf("Sending download request for %s...\n", arg1);
                        send(sockfd, buffer, strlen(buffer), 0); //sends command to server
                        
                        memset(buffer, 0, BUFFER_SIZE); //clears buffer
                        struct timeval tv;
                        tv.tv_sec = 30;  //30 second timeout
                        tv.tv_usec = 0;
                        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv); //sets timeout
                        
                        printf("Waiting for server response...\n");
                        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives response from server
                        //Checks if response is valid
                        if (bytes_received <= 0) 
                        {
                                printf("Error receiving response from server: %s\n", strerror(errno));
                                continue;
                        }
                        buffer[bytes_received] = '\0'; //sets null terminator

                        //Checks if buffer contains "FILE_NOT_FOUND"
                        if (strstr(buffer, "FILE_NOT_FOUND") != NULL) 
                        {
                                printf("Error: File not found on server\n");
                                continue;
                        }

                        char* filename = strrchr(arg1, '/'); //checks if filename has a slash
                        if (filename == NULL)
                                filename = arg1;
                        else
                                filename++;
                        printf("Downloading %s...\n", filename);
                        remove(filename); //removes file

                        int initial_bytes_written = 0;
                        bool initial_complete = false;
                        if (bytes_received > 0) 
                        {
                                const char* markers[] = {
                                    "<!---",                          //comment start
                                    "END_OF_FILE_TRANSFER_MARKER",    //main marker
                                    "<<END_FILE_DATA>>",              //end tag
                                    "\n<!---",                        //newline + comment
                                    "\n---",                          //markdown separator
                                    "--->"                            //comment end
                                }; //array of markers
                                int num_markers = sizeof(markers) / sizeof(markers[0]); //number of markers
                                
                                char* first_marker = NULL; //first marker
                                int marker_pos = bytes_received; //marker position
                                //Checks for first marker
                                for (int i = 0; i < num_markers; i++)
                                {
                                        char* found = strstr(buffer, markers[i]); //checks for marker
                                        //Checks if marker is found
                                        if (found && (found - buffer) < marker_pos)
                                        {
                                                first_marker = found; //sets first marker
                                                marker_pos = found - buffer; //sets marker position
                                        }
                                }
                                
                                int bytes_to_write = bytes_received;
                                //Checks if marker is found
                                if (first_marker)
                                {
                                        bytes_to_write = first_marker - buffer; //sets bytes to write
                                }
                                //Checks if bytes to write is greater than 0
                                if (bytes_to_write > 0)
                                {
                                        FILE *fp = fopen(filename, "wb"); //opens file in binary mode
                                        //Checks if file is opened
                                        if (fp) 
                                        {
                                                fwrite(buffer, 1, bytes_to_write, fp); //writes data to file
                                                fclose(fp); //closes file
                                                initial_bytes_written = bytes_to_write; //sets initial bytes written
                                                //Checks if marker is found
                                                if (first_marker)
                                                {
                                                        printf("File transfer complete: %d bytes received\n", initial_bytes_written);
                                                        initial_complete = true;
                                                }
                                        }
                                        else
                                        {
                                                printf("Error: Could not create file: %s\n", strerror(errno));
                                                continue;
                                        }
                                }
                        }

                        //Checks if initial download is complete
                        if (!initial_complete)
                        {
                                getFile(sockfd, filename); //receives rest of file
                                cleanupFile(filename); //cleans up any end markers that might have been included
                        }
                }
                //Checks if command is removef
                else if (strcmp(command, "removef") == 0) 
                {
                        //Checks if command is valid
                        if (result < 2) 
                        {
                                printf("Usage: removef filename\n");
                                continue;
                        }
                        //Checks if command is valid
                        if (!verifyCommand(command, arg1, NULL))
                                continue;
                        
                        send(sockfd, buffer, strlen(buffer), 0); //sends command to server
                        
                        memset(buffer, 0, BUFFER_SIZE); //clears buffer
                        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives response from server  
                        if (bytes_received > 0)
                        {
                                buffer[bytes_received] = '\0';  // Ensure null termination
                                printf("%s\n", buffer);
                        }
                        else
                        {
                            printf("Error receiving server response\n");
                        }
                }
                //Checks if command is downltar
                else if (strcmp(command, "downltar") == 0) 
                {
                        //Checks if command is valid
                        if (arg1 == NULL) 
                        {
                                printf("Usage: downltar <file_type>\n");
                                continue;
                        }

                        char tar_filename[256];
                        snprintf(tar_filename, sizeof(tar_filename), "%s.tar", arg1); //creates tar filename
                        //Checks if tar file exists
                        if (access(tar_filename, F_OK) == 0) 
                        {
                                //Checks if tar file can be removed
                                if (remove(tar_filename) != 0)
                                {
                                        printf("Warning: Failed to remove existing tar file: %s\n", strerror(errno));
                                }
                                else
                                {
                                    printf("Removed existing %s file\n", tar_filename);
                                }
                        }

                        int sockfd = socket(AF_INET, SOCK_STREAM, 0); //creates socket  
                        if (sockfd < 0) 
                        {
                                perror("Error creating socket");
                                continue;
                        }

                        int val = 1;
                        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); //sets socket options
                        int keepalive = 1;
                        setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)); //sets keepalive
                        int rcvbuf_size = 262144; // 256KB
                        setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)); //sets receive buffer size

                        struct sockaddr_in server_addr;
                        memset(&server_addr, 0, sizeof(server_addr)); //clears server address
                        server_addr.sin_family = AF_INET; //sets family to internet
                        server_addr.sin_addr.s_addr = inet_addr(SERVER_IP); //sets address to server ip
                        server_addr.sin_port = htons(SERVER_PORT); //sets port to server port

                        //Connects to server and checks if connection failed
                        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) 
                        {
                                perror("Error connecting to server");
                                close(sockfd); //closes socket
                                continue;
                        }

                        char send_buffer[BUFFER_SIZE];
                        snprintf(send_buffer, sizeof(send_buffer), "downltar %s", arg1); //creates send buffer
                        //Checks if command can be sent
                        if (send(sockfd, send_buffer, strlen(send_buffer), 0) < 0) 
                        {
                                perror("Error sending command");
                                close(sockfd); //closes socket
                                continue;
                        }

                        memset(send_buffer, 0, BUFFER_SIZE); //clears send buffer
                        int bytes_received = recv(sockfd, send_buffer, BUFFER_SIZE, 0); //receives file size or server rejects command
                        if (bytes_received <= 0) 
                        {
                                printf("Error receiving file size or server rejected command\n");
                                close(sockfd); //closes socket
                                continue;
                        }

                        //Checks for error message
                        if (strncmp(send_buffer, "ERROR", 5) == 0) 
                        {
                                printf("Server error: %s\n", send_buffer);
                                close(sockfd); //closes socket
                                continue;
                        }

                        long file_size = atol(send_buffer); //converts string to long
                        if (file_size <= 0) 
                        {
                                printf("Invalid file size received: %s\n", send_buffer);
                                close(sockfd); //closes socket
                                continue;
                        }
                        printf("File size to download: %ld bytes\n", file_size);

                        char ack[32];
                        snprintf(ack, sizeof(ack), "ACK %ld", file_size); //creates acknowledgment
                        //Checks if acknowledgment can be sent
                        if (send(sockfd, ack, strlen(ack), 0) < 0)
                        {
                                perror("Error sending acknowledgment");
                                close(sockfd); //closes socket
                                continue;
                        }
                        getTarFile(sockfd, tar_filename, file_size); //receives tar file

                        struct stat st;
                        //Checks if file exists and has content
                        if (stat(tar_filename, &st) == 0) 
                        {
                                //Checks if file has content
                                if (st.st_size > 0) 
                                {
                                        char test_cmd[512];
                                        snprintf(test_cmd, sizeof(test_cmd), "tar -tf %s > /dev/null 2>&1", tar_filename); //checks if file is incomplete or corrupted
                                        int result = system(test_cmd); //runs command
                                        if (result != 0)
                                        {
                                                printf("Warning: File may be incomplete or corrupted\n");
                                        }
                                }
                                else
                                {
                                        printf("Error: Downloaded file is empty\n");
                                }
                        }
                        //Checks if file is not verified
                        else
                        {
                                printf("Error: Failed to verify downloaded file\n");
                        }
                        close(sockfd); //closes socket
                }
                //Checks if command is dispfnames or dispfanmes
                else if (strcmp(command, "dispfnames") == 0 || strcmp(command, "dispfanmes") == 0)
                {
                        //Checks if dispfanmes is used
                        if (strcmp(command, "dispfanmes") == 0)
                        {
                                printf("Note: Using 'dispfnames' instead of 'dispfanmes'\n");
                        }
                        if (result < 2)
                        {
                                printf("Usage: dispfnames pathname\n");
                                continue;
                        }
                        //Checks if command is valid
                        if (!verifyCommand("dispfnames", arg1, NULL))
                            continue;
                            
                        char correct_cmd[BUFFER_SIZE];
                        sprintf(correct_cmd, "dispfnames %s", arg1); //creates correct command
                        send(sockfd, correct_cmd, strlen(correct_cmd), 0); //sends command to server
                        
                        memset(buffer, 0, BUFFER_SIZE); //clears buffer
                        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives response from server
                        //Checks if response is valid
                        if (bytes_received > 0) 
                        {
                                buffer[bytes_received] = '\0';  // Ensure null termination
                                printf("\n%s\n", buffer);
                        }
                        else 
                        {
                                printf("Error receiving directory listing\n");
                        }
                }
                //Checks if command is exit
                else if (strcmp(command, "exit") == 0) 
                {
                        break;
                }
                else
                {
                        printf("Error: Unknown command\n");
                }
        }
        close(sockfd); //closes socket
        return 0;
}
