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
#include <sys/wait.h>
#include <errno.h>

#define PORT 8007
#define BUFFER_SIZE 1024
#define S2_PORT 8008
#define S3_PORT 8009
#define S4_PORT 8010
#define SERVER_IP "127.0.0.1"

//Creates a directory path recursively
void createDirectories(char *path)
{
        char temp[256];
        char *p = NULL;
        size_t len;

        snprintf(temp, sizeof(temp), "%s", path); //copies path to temp
        len = strlen(temp); //length of temp
        
        //Checks if the path ends with a '/'
        if (temp[len - 1] == '/')
                temp[len - 1] = 0; //replaces '/' with null terminator

        //Iterates through the temp path
        for (p = temp + 1; *p; p++) 
        {
            //Checks if the current character is a '/'
            if (*p == '/')
            {
                *p = 0; //replaces '/' with null terminator
                mkdir(temp, 0755); //creates directory
                    *p = '/'; //restores '/'
            }
        }

        mkdir(temp, 0755); //creates last directory 
}

//Sends file to client
void sendFile(int sockfd, char *filepath) 
{
        char buffer[BUFFER_SIZE];

        FILE *fp = fopen(filepath, "rb"); //opens file in binary mode
        //Checks if file was opened successfully
        if (fp == NULL)
        {
                printf("Error opening file %s: %s\n", filepath, strerror(errno));
                send(sockfd, "FILE_NOT_FOUND", 14, 0);
                return;
        }

        printf("File %s opened successfully\n", filepath);

        fseek(fp, 0, SEEK_END); //moves pointer to end of file
        long filesize = ftell(fp); //gets size of file
        rewind(fp); //moves pointer to beginning of file

        //Checks if file is empty
        if (filesize == 0)
        {
                printf("Warning: File is empty\n");
        }

        char header[128];
        sprintf(header, "<<BEGIN_FILESIZE>>%ld<<END_FILESIZE>>\n<<BEGIN_FILE_DATA>>\n", filesize); //creates header with filesize and start marker

        printf("Sending header: %s", header);
        //Sends header
        if (send(sockfd, header, strlen(header), 0) <= 0)
        {
                printf("Failed to send header: %s\n", strerror(errno));
                fclose(fp);
                return;
        }

        usleep(50000); //delay to ensure the header is processed separately from the content

        printf("Sending file content (%ld bytes)...\n", filesize);
        int bytes_read;
        long total_sent = 0;

        //Reads file in chunks and sends
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) 
        {
                int bytes_sent = send(sockfd, buffer, bytes_read, 0); //sends file data
                if (bytes_sent <= 0) 
                {
                        printf("Error sending file data at offset %ld: %s\n", 
                        total_sent, strerror(errno));
                        break;
                }

                total_sent += bytes_sent; //updates total bytes sent
                printf("\rProgress: %ld/%ld bytes (%.1f%%)", total_sent, filesize, (float)total_sent/filesize*100);
                fflush(stdout); //flushes stdout to avoid buffer issues
                usleep(1000); //delay to avoid network congestion
        }

        usleep(50000);  //delay before sending end marker

        const char *end_marker = "\n<!--- END_OF_FILE_TRANSFER_MARKER --->\n<<END_FILE_DATA>>\n"; //end marker with unique signature
        send(sockfd, end_marker, strlen(end_marker), 0); //sends end marker
        
        printf("\nFile transfer complete: %ld bytes sent\n", total_sent);
        fclose(fp); //closes file
}

//Receives file from client
void getFile(int sockfd, char *filepath) 
{
        char buffer[BUFFER_SIZE];
        FILE *fp = fopen(filepath, "wb"); //opens file in binary mode

        //Checks if file was opened successfully
        if (fp == NULL)
        {
            printf("Error creating file %s\n", filepath);
            return;
        }
        
        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives file size
        long filesize = atol(buffer); //converts string to long
        
        send(sockfd, "OK", 3, 0); //sends acknowledgment
        
        int bytes_received;
        long total_received = 0;
        
        //Receives file content
        while (total_received < filesize) 
        {
                bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives file content
                fwrite(buffer, 1, bytes_received, fp); //writes to file
                total_received += bytes_received; //updates total bytes received
        }
        
        fclose(fp); //closes file
}

//Connects to server S2, S3, or S4
int connectToServer(int port)
{
        int sockfd;
        struct sockaddr_in server_addr;
        
        sockfd = socket(AF_INET, SOCK_STREAM, 0); //creates socket
        //Checks if socket was created successfully
        if (sockfd < 0)
        {
                perror("Socket creation failed");
                return -1;
        }
        
        memset(&server_addr, 0, sizeof(server_addr)); //clears server address
        server_addr.sin_family = AF_INET; //sets address family to IPv4
        server_addr.sin_port = htons(port); //converts port number to network byte order
        server_addr.sin_addr.s_addr = inet_addr(SERVER_IP); //sets server IP address
        
        //Connects to server
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) 
        {
                perror("Connection to server failed");
                close(sockfd);
                return -1;
        }
        
        return sockfd; //returns socket file descriptor
}

//Transfers file to server S2, S3, or S4
void transferFile(char *filepath, char *destpath, int server_port) 
{
        int sockfd = connectToServer(server_port); //connects to server

        //Checks if connection was successful
        if (sockfd < 0)
        {
                printf("Failed to connect to server\n");
                return;
        }

        char *filename = strrchr(filepath, '/'); //finds last '/' in filepath
        //Checks if filename is NULL
        if (filename == NULL)
                filename = filepath;
        else
                filename++; //skips the '/' character

        printf("Sending filename: %s\n", filename);

        char command[BUFFER_SIZE];
        sprintf(command, "store %s", destpath); //stores destination path relative to server
        printf("Sending command: %s\n", command);
        send(sockfd, command, strlen(command), 0); //sends command

        char buffer[BUFFER_SIZE]; 
        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives response from server
        printf("Received response after command: %s\n", buffer);

        send(sockfd, filename, strlen(filename), 0); //sends filename

        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment
        printf("Received acknowledgment after filename: %s\n", buffer);

        FILE *fp = fopen(filepath, "rb"); //opens file in binary mode
        //Checks if file was opened successfully
        if (fp == NULL) 
        {
                printf("Error opening file %s\n", filepath);
                close(sockfd);
                return;
        }

        fseek(fp, 0, SEEK_END); //moves pointer to end of file
        long filesize = ftell(fp); //gets file size
        rewind(fp); //moves pointer to beginning of file

        sprintf(buffer, "%ld", filesize); //converts file size to string
        send(sockfd, buffer, BUFFER_SIZE, 0); //sends file size
        printf("Sent filesize: %ld\n", filesize);

        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment
        printf("Received acknowledgment after filesize: %s\n", buffer);

        int bytes_read;
        long total_sent = 0;
        //Sends file content
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
        {
                send(sockfd, buffer, bytes_read, 0); //sends file content
                total_sent += bytes_read; //updates total bytes sent
                printf("\rSending file... %ld/%ld bytes", total_sent, filesize);
                fflush(stdout); //flushes stdout to avoid buffer issues
        }
        printf("\nFile sending complete\n");

        fclose(fp); //closes file
        
        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives response
        printf("Server response: %s\n", buffer);
        
        close(sockfd); //closes socket
}

//Requests file from server S2, S3, or S4
void requestFile(int client_sockfd, char *filepath, int server_port) 
{
        int sockfd = connectToServer(server_port); //connects to server
        //Checks if connection was successful   
        if (sockfd < 0) 
        {
                printf("Failed to connect to server on port %d\n", server_port);
                send(client_sockfd, "FILE_NOT_FOUND", 14, 0);
                return;
        }

        char server_path[BUFFER_SIZE]; 
        //Converts ~S1/path to the appropriate server path
        if (strstr(filepath, "~S1") == filepath)
        {
                if (server_port == S2_PORT)
                {
                        sprintf(server_path, "~S2%s", filepath + 3); //converts ~S1/path to ~S2/path
                }
                else if (server_port == S3_PORT)
                {
                        sprintf(server_path, "~S3%s", filepath + 3); //converts ~S1/path to ~S3/path
                }
                else if (server_port == S4_PORT)
                {
                        sprintf(server_path, "~S4%s", filepath + 3); //converts ~S1/path to ~S4/path
                }
                else
                {
                        strcpy(server_path, filepath); //copies filepath to server_path
                }
        }
        else
        {
                strcpy(server_path, filepath); //copies filepath to server_path
        }

        printf("Requesting file from server: %s\n", server_path);

        char command[BUFFER_SIZE];
        sprintf(command, "get %s", server_path); //sends request command
        //Checks if command was sent successfully
        if (send(sockfd, command, strlen(command), 0) <= 0) 
        {
                printf("Error sending command to secondary server\n");
                send(client_sockfd, "FILE_NOT_FOUND", 14, 0);
                close(sockfd); //closes socket
                return;
        }

        char buffer[BUFFER_SIZE * 2]; //larger buffer for headers
        memset(buffer, 0, sizeof(buffer)); //clears buffer
        int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0); //receives response

        //Checks if response was received successfully
        if (bytes_received <= 0) 
        {
                printf("Error receiving response from secondary server\n");
                send(client_sockfd, "FILE_NOT_FOUND", 14, 0);
                close(sockfd); //closes socket
                return;
        }

        buffer[bytes_received] = '\0'; //null-terminates buffer
        printf("Response from secondary server (first %d bytes): %.60s%s\n", bytes_received, buffer, bytes_received > 60 ? "..." : ""); //prints first 60 bytes of response

        //Checks if file was not found
        if (strstr(buffer, "FILE_NOT_FOUND") != NULL) 
        {
                printf("File not found on secondary server\n");
                send(client_sockfd, "FILE_NOT_FOUND", 14, 0);
                close(sockfd); //closes socket
                return;
        }

        long filesize = 0;
        filesize = atol(buffer); //converts string to long
        //Checks if file size is greater than 0
        if (filesize > 0) 
        {
                printf("Received file size: %ld bytes\n", filesize);
                send(sockfd, "OK", 2, 0); //sends acknowledgment
                char header[128];
                sprintf(header, "<<BEGIN_FILESIZE>>%ld<<END_FILESIZE>>\n<<BEGIN_FILE_DATA>>\n", filesize); //creates header with filesize and start marker

                //Checks if header was sent successfully
                if (send(client_sockfd, header, strlen(header), 0) <= 0) 
                {
                        printf("Error sending header to client\n");
                        close(sockfd); //closes socket
                        return;
                }
            
                printf("Sent standardized header to client: %s\n", header);
                usleep(50000); //delay to ensure header is processed separately
        } 
        else 
        {
                printf("Warning: Could not parse filesize: '%s'\n", buffer);
                //Sends raw data
                if (send(client_sockfd, buffer, bytes_received, 0) <= 0) 
                {
                        printf("Error forwarding initial data to client\n");
                        close(sockfd); //closes socket
                        return;
                }
        }

        long total_relayed = 0;
        //Continues relaying data from server to client
        while (1) 
        {
                memset(buffer, 0, sizeof(buffer)); //clears buffer
                bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0); //receives data from server
                //Checks if data was received successfully
                if (bytes_received <= 0) 
                {
                        if (bytes_received == 0) 
                        {
                                printf("Server closed connection - transfer complete\n");
                        } 
                        else 
                        {
                                printf("Error receiving data from secondary server: %s\n", strerror(errno));
                        }
                        break;
                }

                int bytes_sent = send(client_sockfd, buffer, bytes_received, 0); //sends data to client
                //Checks if data was sent successfully
                if (bytes_sent <= 0) 
                {
                        printf("Error forwarding data to client\n");
                        break;
                }

                total_relayed += bytes_sent; //updates total bytes relayed

                if (filesize > 0) 
                {
                        printf("\rRelayed: %ld/%ld bytes (%.1f%%)", total_relayed, filesize, (float)total_relayed / filesize * 100); //prints progress relative to total file size
                } 
                else 
                {
                        printf("\rRelayed: %ld bytes", total_relayed); //prints total bytes relayed
                }
                fflush(stdout); //flushes stdout to avoid buffer issues
        }

        const char *end_marker = "\n<!--- END_OF_FILE_TRANSFER_MARKER --->\n<<END_FILE_DATA>>\n";
        send(client_sockfd, end_marker, strlen(end_marker), 0); //sends end-of-file marker
        //Checks if file size is greater than 0
        if (filesize > 0) 
        {
                printf("\nFile relay complete: %ld/%ld bytes (%.1f%%)\n", total_relayed, filesize, (float)total_relayed / filesize * 100); //prints progress relative to total file size
        } 
        else 
        {
                printf("\nFile relay complete: %ld bytes transferred\n", total_relayed); //prints total bytes transferred
        }        
        close(sockfd); //closes socket
}

//Remove file from server S2, S3, or S4
void removeFile(int client_sockfd, char *filepath, int server_port) 
{
        int sockfd = connectToServer(server_port); //connects to server
        //Checks if connection was successful
        if (sockfd < 0)
        {
                printf("Failed to connect to server\n");
                send(client_sockfd, "Failed to connect to server", 26, 0);
                return;
        }

        char command[BUFFER_SIZE];
        sprintf(command, "remove %s", filepath); //sends remove command
        send(sockfd, command, strlen(command), 0); //sends command

        char buffer[BUFFER_SIZE];
        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives response

        send(client_sockfd, buffer, strlen(buffer), 0); //sends response to client

        close(sockfd); //closes socket
}

//Gets list of filenames from server S2, S3, or S4
char* getFileNames(char *path, int server_port) 
{
        int sockfd = connectToServer(server_port); //connects to server

        //Checks if connection was successful
        if (sockfd < 0) 
        {
                printf("Failed to connect to server on port %d\n", server_port);
                return strdup("");
        }

        char command[BUFFER_SIZE];
        sprintf(command, "list %s", path); //sends list command
        printf("Sending list command to port %d: %s\n", server_port, command);
        send(sockfd, command, strlen(command), 0); //sends command

        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0);
        printf("Received from port %d (%d bytes): '%s'\n", server_port, bytes_received, buffer);

        close(sockfd); //closes socket

        return strdup(buffer); //returns buffer
}

//Downloads tar file from server S2, S3, or S4
void downloadTar(int client_sockfd, char *filetype, int server_port)
{
        int sockfd = connectToServer(server_port); //connects to server

        //Checks if connection was successful
        if (sockfd < 0) 
        {
                printf("Failed to connect to server\n");
                send(client_sockfd, "0", 1, 0); //sends 0 to client
                return;
        }

        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)); //sets socket options for better reliability

        int buf_size = 131072;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)); //sets buffer size for receiving
        setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)); //sets buffer size for sending

        char local_tar_filename[256];
        //Determines local filename
        if (strcmp(filetype, ".pdf") == 0) 
        {
                strcpy(local_tar_filename, "pdf.tar"); //copies pdf.tar to local_tar_filename
        }
        else if (strcmp(filetype, ".txt") == 0)
        {
                strcpy(local_tar_filename, "text.tar"); //copies text.tar to local_tar_filename
        }
        else if (strcmp(filetype, ".zip") == 0)
        {
                strcpy(local_tar_filename, "zip.tar"); //copies zip.tar to local_tar_filename
        }
        else
        {
                strcpy(local_tar_filename, "unknown.tar"); //copies unknown.tar to local_tar_filename
        }

        unlink(local_tar_filename); //removes any existing file before we start

        char command[BUFFER_SIZE];
        sprintf(command, "tar %s", filetype); //sends tar command
        printf("Sending command to secondary server: %s\n", command);
        send(sockfd, command, strlen(command), 0); //sends command

        struct timeval tv;
        tv.tv_sec = 30; //sets timeout to 30 seconds
        tv.tv_usec = 0; //sets timeout to 30 seconds
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)); //sets timeout to 30 seconds for receiving

        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int bytes = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives file size from server
        //Checks if file size was received successfully
        if (bytes <= 0) 
        {
                printf("Error receiving file size from server: %s\n", 
                bytes == 0 ? "Connection closed" : strerror(errno)); //checks if connection was closed
                close(sockfd); //closes socket
                send(client_sockfd, "0", 1, 0); //sends 0 to client
                return;
        }
        //Checks if no files were found on server
        if (strcmp(buffer, "0") == 0) 
        {
                printf("No files found on server\n");
                close(sockfd); //closes socket
                send(client_sockfd, "0", 1, 0); //sends 0 to client
                return;
        }
        printf("Received file size: %s bytes\n", buffer);
        
        long filesize = atol(buffer); //converts file size to long  
        //Checks if file size is valid
        if (filesize <= 0) 
        {
                printf("Invalid file size: %s\n", buffer);
                close(sockfd); //closes socket
                send(client_sockfd, "0", 1, 0); //sends 0 to client
                return;
        }

        printf("Forwarding file size (%ld bytes) to client\n", filesize);
        send(client_sockfd, buffer, strlen(buffer), 0);

        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        int ack_received = recv(client_sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment from client
        //Checks if acknowledgment was received successfully
        if (ack_received <= 0) 
        {
                printf("Error receiving acknowledgment from client\n");
                close(sockfd); //closes socket
                return;
        }
        printf("Client acknowledgment: %s\n", buffer);

        printf("Forwarding acknowledgment to server\n");
        send(sockfd, "OK", 2, 0); //sends acknowledgment to server

        usleep(200000); //delay to ensure acknowledgment is processed separately

        FILE *local_fp = fopen(local_tar_filename, "wb"); //creates local file to save the tar
        if (!local_fp) 
        {
                printf("Error creating local tar file: %s\n", strerror(errno)); //checks if local file was created successfully
                close(sockfd); //closes socket
                send(client_sockfd, "0", 1, 0); //sends 0 to client
                return;
        }

        printf("Starting file transfer from server to client, expected size: %ld bytes\n", filesize);

        char *recv_buffer = malloc(BUFFER_SIZE * 4); //allocates buffer 
        if (!recv_buffer) 
        {
                printf("Error allocating buffer\n"); //checks if buffer was allocated successfully
                fclose(local_fp); //closes local file
                close(sockfd); //closes socket
                return;
        }

        tv.tv_sec = 60; //sets timeout to 60 seconds
        tv.tv_usec = 0; //sets timeout to 60 seconds
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)); //sets timeout to 60 seconds for receiving

        int bytes_received;
        long total_received = 0;
        int retry_count = 0;

        //Continues receiving data from server until file is complete
        while (total_received < filesize && retry_count < 5) 
        {
                memset(recv_buffer, 0, BUFFER_SIZE * 4); //clears buffer
                bytes_received = recv(sockfd, recv_buffer, BUFFER_SIZE * 4, 0); //receives data from server
                //Checks if data was received successfully
                if (bytes_received > 0) 
                {
                        retry_count = 0; //resets retry counter

                        //Debugs for first chunk
                        if (total_received == 0)
                        {
                                printf("First data chunk received (%d bytes)\n", bytes_received);
                        }

                        size_t written = fwrite(recv_buffer, 1, bytes_received, local_fp); //writes data to local file
                        //Checks if data was written successfully
                        if (written != bytes_received) 
                        {
                                printf("Error writing to file: %s\n", strerror(errno));
                                break;
                        }

                        int sent = send(client_sockfd, recv_buffer, bytes_received, 0); //sends data to client
                        //Checks if data was sent successfully
                        if (sent != bytes_received) 
                        {
                                printf("Warning: Partial forward to client (%d/%d bytes)\n", sent, bytes_received);
                        }
                        total_received += bytes_received; //updates total received bytes

                        //Prints progress
                        if (total_received % (10 * 1024) == 0 || total_received == filesize) 
                        {
                                printf("Transferred %ld/%ld bytes (%.1f%%)\n", total_received, filesize, (float)total_received / filesize * 100); //prints progress
                                fflush(stdout); //flushes stdout to avoid buffer issues
                        }
                }
                //Checks if server closed connection
                else if (bytes_received == 0)
                {
                        printf("Server closed connection at %ld/%ld bytes\n", total_received, filesize);
                        break;
                }
                //Checks if data was received successfully
                else
                {
                        printf("Error receiving data: %s\n", strerror(errno)); //checks if data was received successfully
                        retry_count++; //increments retry counter
                        //Checks if there are too many consecutive errors
                        if (retry_count >= 5)
                        {
                                printf("Too many consecutive errors\n");
                                break;
                        }
                        usleep(100000); //delay to avoid overwhelming server
                }
        }

        fflush(local_fp); //flushes local file
        int fd = fileno(local_fp); //gets file descriptor
        if (fd >= 0) 
        {
                fsync(fd); //flushes file to disk
        }
        fclose(local_fp); //closes local file

        close(sockfd); //closes socket
        free(recv_buffer); //frees buffer

        printf("File transfer complete: %ld/%ld bytes (%.1f%%)\n", total_received, filesize, (float)total_received / filesize * 100); //prints progress
        struct stat st;
        //Checks if file exists
        if (stat(local_tar_filename, &st) == 0) 
        {
                printf("Local file size: %ld bytes\n", (long)st.st_size); //prints local file size

                //Checks if file is incomplete
                if (total_received < filesize) 
                {
                        printf("Incomplete file received. Sending directly from local tar file...\n");

                        //Simply sends the entire local file to the client
                        FILE *file = fopen(local_tar_filename, "rb");
                        //Checks if file was opened successfully
                        if (file) 
                        {
                                const char *separator = "<<DIRECT_FILE_TRANSFER>>";
                                send(client_sockfd, separator, strlen(separator), 0); //sends separator to client
                                usleep(100000); //delay to avoid overwhelming server

                                char size_str[32];
                                sprintf(size_str, "%ld", (long)st.st_size);
                                send(client_sockfd, size_str, strlen(size_str), 0); //sends file size to client
                                usleep(100000); //delay to avoid overwhelming server

                                char buffer[4096];
                                size_t read_bytes;
                                long total_sent = 0;
                                //Sends file content
                                while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
                                {
                                        int sent = send(client_sockfd, buffer, read_bytes, 0); //sends file content to client
                                        //Checks if file was sent successfully
                                        if (sent <= 0)
                                        {
                                                printf("Error sending file: %s\n", strerror(errno));
                                                break;
                                        }

                                        total_sent += sent; //updates total sent bytes
                                        printf("\rSent: %ld/%ld bytes (%.1f%%)", total_sent, (long)st.st_size, (float)total_sent/(long)st.st_size*100);
                                        fflush(stdout); //flushes stdout to avoid buffer issues
                                }

                                printf("\nCompleted direct file send: %ld bytes\n", total_sent); //prints progress
                                fclose(file); //closes file
                        }
                        else
                        {
                                printf("Failed to open local tar file for direct sending\n");
                        }
                }
        }
        else
        {
                printf("Error: Local file not found after transfer\n");
        }

        const char *end_marker = "\n<!--- END_OF_FILE_TRANSFER_MARKER --->\n";
        send(client_sockfd, end_marker, strlen(end_marker), 0); //sends end marker to client
        printf("Sent end marker\n"); //prints progress
}

//Sends file to client without protocol headers (for tar files)
void simpleSendFile(int sockfd, char *filepath) 
{
        char buffer[BUFFER_SIZE];
        FILE *fp = fopen(filepath, "rb"); //opens file
        //Checks if file was opened successfully
        if (fp == NULL) 
        {
                printf("Error opening file %s: %s\n", filepath, strerror(errno));
                send(sockfd, "0", 1, 0);
                return;
        }
        printf("File %s opened successfully\n", filepath); //prints progress

        fseek(fp, 0, SEEK_END); //moves to end of file
        long filesize = ftell(fp); //gets file size
        rewind(fp); //moves to beginning of file

        //Checks if file is empty
        if (filesize == 0) 
        {
                printf("Warning: File is empty\n");
                fclose(fp); //closes file
                send(sockfd, "0", 1, 0); //sends 0 to client
                return;
        }

        sprintf(buffer, "%ld", filesize); //converts file size to string
        send(sockfd, buffer, strlen(buffer), 0); //sends file size to client
        printf("Sent file size: %s\n", buffer); //prints progress

        memset(buffer, 0, BUFFER_SIZE); //clears buffer
        recv(sockfd, buffer, BUFFER_SIZE, 0); //receives acknowledgment from client
        printf("Received acknowledgment: %s\n", buffer); //prints progress

        printf("Sending file content (%ld bytes)...\n", filesize); //prints progress
        int bytes_read;
        long total_sent = 0;

        //Reads file in chunks and sends
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) 
        {
                int bytes_sent = send(sockfd, buffer, bytes_read, 0); //sends file content to client
                //Checks if file was sent successfully
                if (bytes_sent <= 0) 
                {
                        printf("Error sending file data at offset %ld: %s\n", 
                        total_sent, strerror(errno)); //checks if file was sent successfully
                        break;
                }
                total_sent += bytes_sent; //updates total sent bytes
                printf("\rProgress: %ld/%ld bytes (%.1f%%)", total_sent, filesize, (float)total_sent/filesize*100); //prints progress
                fflush(stdout); //flushes stdout to avoid buffer issues
        }
        printf("\nFile transfer complete: %ld bytes sent\n", total_sent); //prints progress
        fclose(fp); //closes file
}

//Compares strings
int compare_strings(const void *a, const void *b) 
{
        return strcmp(*(const char **)a, *(const char **)b); //compares strings
}

//Handles client requests
void prcclient(int sockfd) 
{
        char buffer[BUFFER_SIZE];
        char command[20], arg1[256], arg2[256];

        mkdir("S1", 0755); //creates S1 directory

        while (1)
        {
                memset(buffer, 0, BUFFER_SIZE); //clears buffer
                int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0); //receives command from client
                //Checks if client disconnected
                if (bytes_received <= 0) 
                {
                        printf("Client disconnected\n");
                        break;
                }
                printf("Received command: %s\n", buffer); //prints command
                sscanf(buffer, "%s %s %s", command, arg1, arg2); //parses command
                //Checks if command is uploadf
                if (strcmp(command, "uploadf") == 0)
                {
                        char *filename = strrchr(arg1, '/'); //extracts filename from path
                        if (filename == NULL)
                                filename = arg1;
                        else
                                filename++;
                            
                        char destpath[512]; //destination path
                        sprintf(destpath, "./%s", arg2 + 1); //converts destination path to string
                        createDirectories(destpath); //creates directories

                        char filepath[512]; //filepath
                        sprintf(filepath, "%s/%s", destpath, filename); //converts filepath to string
                        getFile(sockfd, filepath); //receives file from client
                        
                        char *ext = strrchr(filename, '.'); //extracts file extension
                        //Checks if file extension is supported
                        if (ext != NULL)
                        {
                                //Checks if file extension is PDF
                                if (strcmp(ext, ".pdf") == 0)
                                {
                                        char s2path[512];
                                        sprintf(s2path, "~S2%s", arg2 + 3); //converts S2 path to string
                                        printf("Sending to S2 path: %s\n", s2path); //prints progress
                                        transferFile(filepath, s2path, S2_PORT); //transfers file to S2
                                        unlink(filepath); //deletes file from S1
                                        char response[] = "File uploaded successfully";
                                        send(sockfd, response, strlen(response), 0); //sends response to client
                                }
                                //Checks if file extension is TXT
                                else if (strcmp(ext, ".txt") == 0) 
                                {
                                        char s3path[512];
                                        sprintf(s3path, "~S3%s", arg2 + 3); //converts S3 path to string
                                        printf("Sending to S3 path: %s\n", s3path); //prints progress
                                        transferFile(filepath, s3path, S3_PORT); //transfers file to S3
                                        unlink(filepath); //deletes file from S1
                                        char response[] = "File uploaded successfully";
                                        send(sockfd, response, strlen(response), 0); //sends response to client
                                }
                                //Checks if file extension is ZIP
                                else if (strcmp(ext, ".zip") == 0) 
                                {
                                        char s4path[512];
                                        sprintf(s4path, "~S4%s", arg2 + 3); //converts S4 path to string
                                        printf("Sending to S4 path: %s\n", s4path); //prints progress
                                        transferFile(filepath, s4path, S4_PORT); //transfers file to S4        
                                        unlink(filepath); //deletes file from S1
                                        char response[] = "File uploaded successfully";
                                        send(sockfd, response, strlen(response), 0); //sends response to client
                                }
                                else if (strcmp(ext, ".c") == 0) 
                                {
                                        char response[] = "File uploaded successfully";
                                        send(sockfd, response, strlen(response), 0); //sends response to client
                                }
                                else 
                                {
                                        char response[BUFFER_SIZE];
                                        sprintf(response, "File type %s is not supported. Only .c, .pdf, .txt, and .zip files are supported.", ext); //converts response to string
                                        send(sockfd, response, strlen(response), 0); //sends response to client
                                        unlink(filepath); //deletes file from S1
                                }
                        }
                        else
                        {
                                char response[] = "File has no extension. Only .c, .pdf, .txt, and .zip files are supported."; //converts response to string
                                send(sockfd, response, strlen(response), 0); //sends response to client
                                unlink(filepath); //deletes file from S1
                        }
                }
                //if command is downlf
                else if (strcmp(command, "downlf") == 0)
                {
                        char filepath[512];
                        sprintf(filepath, "./%s", arg1 + 1); //converts filepath to string
                        printf("Converted path: %s\n", filepath); //prints progress
                        
                        //Checks if file exists
                        if (access(filepath, F_OK) == 0) 
                        {
                                printf("File exists in S1, sending to client\n"); //prints progress
                                sendFile(sockfd, filepath); //sends file to client
                                printf("File sent to client\n"); //prints progress
                        }
                        else
                        {
                                printf("File not found in S1: %s\n", filepath); //prints progress
                                char *ext = strrchr(arg1, '.'); //extracts file extension
                                //Checks if file extension is supported
                                if (ext != NULL) 
                                {
                                        printf("Checking other servers based on extension: %s\n", ext);
                                        //Checks if file extension is PDF
                                        if (strcmp(ext, ".pdf") == 0)
                                        {
                                                printf("Routing .pdf file request to S2 server\n"); //prints progress
                                                requestFile(sockfd, arg1, S2_PORT); //requests file from S2
                                        }
                                        //Checks if file extension is TXT
                                        else if (strcmp(ext, ".txt") == 0) 
                                        {
                                                printf("Routing .txt file request to S3 server\n"); //prints progress
                                                requestFile(sockfd, arg1, S3_PORT); //requests file from S3
                                        }
                                        //Checks if file extension is ZIP
                                        else if (strcmp(ext, ".zip") == 0) 
                                        {
                                                printf("Routing .zip file request to S4 server\n"); //prints progress
                                                requestFile(sockfd, arg1, S4_PORT); //requests file from S4
                                        }
                                        else 
                                        {
                                                printf("Unsupported file extension: %s\n", ext); //prints progress
                                                send(sockfd, "FILE_NOT_FOUND", 14, 0); //sends response to client
                                        }
                                }
                                else
                                {
                                        printf("File has no extension\n"); //prints progress
                                        send(sockfd, "FILE_NOT_FOUND", 14, 0); //sends response to client
                                }
                        }
                }
                //if command is removef
                else if (strcmp(command, "removef") == 0)
                {
                        char filepath[512];
                        sprintf(filepath, "./%s", arg1 + 1); //converts filepath to string
                        
                        //Checks if file exists
                        if (access(filepath, F_OK) == 0) 
                        {
                                //File exists in S1, remove it
                                if (unlink(filepath) == 0) 
                                {
                                        send(sockfd, "File removed successfully", 24, 0); //sends response to client
                                }
                                else 
                                {
                                        send(sockfd, "Failed to remove file", 21, 0); //sends response to client
                                }
                        }
                        //if file exists in other servers
                        else
                        {
                                char *ext = strrchr(arg1, '.'); //extracts file extension
                                //Checks if file extension is supported 
                                if (ext != NULL) 
                                {
                                        //Checks if file extension is PDF
                                        if (strcmp(ext, ".pdf") == 0) 
                                        {
                                                char s2path[512];
                                                sprintf(s2path, "~S2%s", arg1 + 3); //converts S2 path to string
                                                removeFile(sockfd, s2path, S2_PORT); //removes file from S2
                                        }
                                        //Checks if file extension is TXT
                                        else if (strcmp(ext, ".txt") == 0) 
                                        {
                                                char s3path[512];
                                                sprintf(s3path, "~S3%s", arg1 + 3); //converts S3 path to string
                                                removeFile(sockfd, s3path, S3_PORT); //removes file from S3
                                        }
                                        //Checks if file extension is ZIP
                                        else if (strcmp(ext, ".zip") == 0) 
                                        {
                                                char s4path[512];
                                                sprintf(s4path, "~S4%s", arg1 + 3); //converts S4 path to string
                                                removeFile(sockfd, s4path, S4_PORT); //removes file from S4
                                        }
                                        else 
                                        {
                                                send(sockfd, "File not found", 14, 0); //sends response to client
                                        }
                                }
                                else 
                                {
                                        send(sockfd, "File not found", 14, 0); //sends response to client
                                }
                        }
                }
                //if command is downltar
                else if (strcmp(command, "downltar") == 0)
                {
                        //Checks if file extension is C
                        if (strcmp(arg1, ".c") == 0)
                        {
                                system("find ./S1 -name \"*.c\" | tar -cf cfiles.tar -T -"); //finds all C files in S1 and creates a tar file
                                simpleSendFile(sockfd, "cfiles.tar"); //sends tar file to client
                                printf("cfiles.tar kept for debugging\n"); //prints progress
                        }
                        //Checks if file extension is PDF
                        else if (strcmp(arg1, ".pdf") == 0) 
                        {
                                downloadTar(sockfd, arg1, S2_PORT); //downloads tar file from S2
                        }
                        //Checks if file extension is TXT
                        else if (strcmp(arg1, ".txt") == 0) 
                        {
                                downloadTar(sockfd, arg1, S3_PORT); //downloads tar file from S3
                        }
                        //Checks if file extension is ZIP
                        else if (strcmp(arg1, ".zip") == 0) 
                        {
                                downloadTar(sockfd, arg1, S4_PORT); //downloads tar file from S4
                        }
                }
                //if command is dispfnames  
                else if (strcmp(command, "dispfnames") == 0) 
                {
                        //Converts ~S1 to ./S1
                        char dirpath[512];
                        sprintf(dirpath, "./%s", arg1 + 1);
                        
                        DIR *dir = opendir(dirpath); //opens directory
                        //If directory does not exist
                        if (dir == NULL)
                        {
                                printf("Directory not found: %s\n", dirpath);
                                send(sockfd, "Directory not found", 19, 0); //sends response to client
                                continue;
                        }
                        closedir(dir); //closes directory

                        char other_path[512] = "";
                        //Extracts path for other servers (after ~S1/)
                        if (strlen(arg1) > 4) 
                        {
                                strcpy(other_path, arg1 + 4); //copies path to other_path
                        }
                        //Ensures we have a valid path for recursion, even for root
                        if (strcmp(other_path, "") == 0) 
                        {
                                strcpy(other_path, "/"); //copies "/" to other_path
                        }
                        printf("Path for other servers: '%s'\n", other_path); //prints progress
                        
                        char find_cmd[1024];
                        char find_result[BUFFER_SIZE * 4] = "";
                        sprintf(find_cmd, "find %s -type f -name \"*.c\" 2>/dev/null | sort", dirpath); //finds all C files in S1
                        printf("Finding C files with: %s\n", find_cmd); //prints progress
                        
                        FILE *find_pipe = popen(find_cmd, "r"); //opens find command
                        //Checks if find command is successful
                        if (find_pipe)
                        {
                                char *c_file_array[100];
                                int c_file_count = 0;
                                char line[256];
                                //Reads find command output
                                while (fgets(line, sizeof(line), find_pipe) != NULL && c_file_count < 100)
                                {
                                        line[strcspn(line, "\n")] = 0; //removes newline
                                        
                                        char *filename = strrchr(line, '/'); //extracts filename
                                        //Checks if filename exists
                                        if (filename) 
                                        {
                                                filename++; //skips '/'
                                        }
                                        else
                                        {
                                                filename = line; //uses whole string
                                        }
                                        c_file_array[c_file_count++] = strdup(filename); //adds filename to array
                                }
                                pclose(find_pipe); //closes find command
                                qsort(c_file_array, c_file_count, sizeof(char*), compare_strings); //sorts C files alphabetically
                                
                                char c_files[BUFFER_SIZE] = ""; //sorted C files string
                                //Adds sorted C files to string
                                for (int i = 0; i < c_file_count; i++) 
                                {
                                        strcat(c_files, c_file_array[i]); //adds filename to string
                                        strcat(c_files, "\n"); //adds newline to string
                                        free(c_file_array[i]); //frees allocated memory
                                }

                                char *pdf_files_raw = getFileNames(other_path, S2_PORT); //gets PDF files from S2
                                char *txt_files_raw = getFileNames(other_path, S3_PORT); //gets TXT files from S3
                                char *zip_files_raw = getFileNames(other_path, S4_PORT); //gets ZIP files from S4
                                
                                char *pdf_file_array[100]; //PDF files array
                                int pdf_file_count = 0; //PDF files count
                                char *pdf_token = strtok(pdf_files_raw, "\n"); //PDF files token
                                //Checks if PDF files token is not NULL and PDF files count is less than 100
                                while (pdf_token != NULL && pdf_file_count < 100) 
                                {
                                        pdf_file_array[pdf_file_count++] = strdup(pdf_token); //adds filename to array
                                        pdf_token = strtok(NULL, "\n"); //gets next token
                                }
                                qsort(pdf_file_array, pdf_file_count, sizeof(char*), compare_strings); //sorts PDF files alphabetically
                                
                                char pdf_files[BUFFER_SIZE] = ""; //PDF files string
                                //Adds sorted PDF files to string
                                for (int i = 0; i < pdf_file_count; i++) 
                                {
                                        strcat(pdf_files, pdf_file_array[i]); //adds filename to string
                                        strcat(pdf_files, "\n"); //adds newline to string
                                        free(pdf_file_array[i]); //frees allocated memory
                                }
                                
                                char *txt_file_array[100]; //TXT files array
                                int txt_file_count = 0; //TXT files count
                                char *txt_token = strtok(txt_files_raw, "\n"); //TXT files token
                                //Checks if TXT files token is not NULL and TXT files count is less than 100
                                while (txt_token != NULL && txt_file_count < 100) 
                                {
                                        txt_file_array[txt_file_count++] = strdup(txt_token); //adds filename to array
                                        txt_token = strtok(NULL, "\n"); //gets next token
                                }
                                qsort(txt_file_array, txt_file_count, sizeof(char*), compare_strings); //sorts TXT files alphabetically
                                
                                char txt_files[BUFFER_SIZE] = ""; //TXT files string
                                //Adds sorted TXT files to string
                                for (int i = 0; i < txt_file_count; i++) 
                                {
                                        strcat(txt_files, txt_file_array[i]); //adds filename to string
                                        strcat(txt_files, "\n"); //adds newline to string
                                        free(txt_file_array[i]); //frees allocated memory
                                }
                                
                                char *zip_file_array[100]; //ZIP files array
                                int zip_file_count = 0; //ZIP files count
                                char *zip_token = strtok(zip_files_raw, "\n"); //ZIP files token
                                //Checks if ZIP files token is not NULL and ZIP files count is less than 100
                                while (zip_token != NULL && zip_file_count < 100) 
                                {
                                        zip_file_array[zip_file_count++] = strdup(zip_token); //adds filename to array
                                        zip_token = strtok(NULL, "\n"); //gets next token
                                }
                                qsort(zip_file_array, zip_file_count, sizeof(char*), compare_strings); //sorts ZIP files alphabetically
                                
                                char zip_files[BUFFER_SIZE] = ""; //ZIP files string
                                //Adds sorted ZIP files to string
                                for (int i = 0; i < zip_file_count; i++) 
                                {
                                        strcat(zip_files, zip_file_array[i]); //adds filename to string
                                        strcat(zip_files, "\n"); //adds newline to string
                                        free(zip_file_array[i]); //frees allocated memory
                                }
                                
                                char file_list[BUFFER_SIZE*4] = ""; //file list string
                                sprintf(file_list, "C files:\n%s\nPDF files:\n%s\nTXT files:\n%s\nZIP files:\n%s", c_files, pdf_files, txt_files, zip_files); //combines all file lists
                                send(sockfd, file_list, strlen(file_list), 0); //sends file list to client
                                
                                free(pdf_files_raw); //frees allocated memory
                                free(txt_files_raw); //frees allocated memory
                                free(zip_files_raw); //frees allocated memory
                        }
                        else
                        {
                                char c_files[BUFFER_SIZE] = "";
                                DIR *dir = opendir(dirpath); //opens directory
                                
                                char *c_file_array[100];
                                int c_file_count = 0;
                                
                                struct dirent *entry;
                                //Reads directory entries
                                while ((entry = readdir(dir)) != NULL) 
                                {
                                        if (entry->d_type == DT_REG) //Checks if entry is a regular file
                                        {
                                                char *ext = strrchr(entry->d_name, '.'); //extracts file extension
                                                //Checks if file extension is C
                                                if (ext != NULL && strcmp(ext, ".c") == 0) 
                                                {
                                                        printf("Found C file: %s\n", entry->d_name);
                                                        c_file_array[c_file_count] = strdup(entry->d_name); //adds filename to array
                                                        c_file_count++;
                                            }
                                        }
                                }
                                closedir(dir); //closes directory   
                                qsort(c_file_array, c_file_count, sizeof(char*), compare_strings); //sorts C files alphabetically

                                //Adds sorted C files to string
                                for (int i = 0; i < c_file_count; i++) 
                                {
                                        strcat(c_files, c_file_array[i]); //adds filename to string
                                        strcat(c_files, "\n"); //adds newline to string
                                        free(c_file_array[i]); //frees allocated memory
                                }

                                char *pdf_files_raw = getFileNames(other_path, S2_PORT); //gets PDF files from S2
                                char *txt_files_raw = getFileNames(other_path, S3_PORT); //gets TXT files from S3
                                char *zip_files_raw = getFileNames(other_path, S4_PORT); //gets ZIP files from S4
                                printf("ZIP files from S4: '%s'\n", zip_files_raw);
                                
                                char *pdf_file_array[100]; //PDF files array
                                int pdf_file_count = 0; //PDF files count
                                char *pdf_token = strtok(pdf_files_raw, "\n"); //PDF files token
                                //Checks if PDF files token is not NULL and PDF files count is less than 100
                                while (pdf_token != NULL && pdf_file_count < 100) 
                                {
                                        pdf_file_array[pdf_file_count++] = strdup(pdf_token); //adds filename to array
                                        pdf_token = strtok(NULL, "\n"); //gets next token
                                }
                                qsort(pdf_file_array, pdf_file_count, sizeof(char*), compare_strings); //sorts PDF files alphabetically
                                
                                char pdf_files[BUFFER_SIZE] = ""; //PDF files string
                                //Adds sorted PDF files to string
                                for (int i = 0; i < pdf_file_count; i++) 
                                {
                                        strcat(pdf_files, pdf_file_array[i]); //adds filename to string
                                        strcat(pdf_files, "\n"); //adds newline to string
                                        free(pdf_file_array[i]); //frees allocated memory
                                }
                                
                                char *txt_file_array[100]; //TXT files array
                                int txt_file_count = 0; //TXT files count
                                char *txt_token = strtok(txt_files_raw, "\n"); //TXT files token
                                //Checks if TXT files token is not NULL and TXT files count is less than 100
                                while (txt_token != NULL && txt_file_count < 100)
                                {
                                        txt_file_array[txt_file_count++] = strdup(txt_token); //adds filename to array
                                        txt_token = strtok(NULL, "\n"); //gets next token
                                }
                                qsort(txt_file_array, txt_file_count, sizeof(char*), compare_strings); //sorts TXT files alphabetically
                                
                                char txt_files[BUFFER_SIZE] = ""; //TXT files string
                                //Adds sorted TXT files to string
                                for (int i = 0; i < txt_file_count; i++) 
                                {
                                        strcat(txt_files, txt_file_array[i]); //adds filename to string
                                        strcat(txt_files, "\n"); //adds newline to string
                                        free(txt_file_array[i]); //frees allocated memory
                                }
                                
                                char *zip_file_array[100]; //ZIP files array
                                int zip_file_count = 0; //ZIP files count
                                char *zip_token = strtok(zip_files_raw, "\n"); //ZIP files token
                                while (zip_token != NULL && zip_file_count < 100) 
                                {
                                        zip_file_array[zip_file_count++] = strdup(zip_token); //adds filename to array
                                        zip_token = strtok(NULL, "\n"); //gets next token
                                }
                                qsort(zip_file_array, zip_file_count, sizeof(char*), compare_strings); //sorts ZIP files alphabetically
                                
                                char zip_files[BUFFER_SIZE] = ""; //ZIP files string
                                //Adds sorted ZIP files to string
                                for (int i = 0; i < zip_file_count; i++) 
                                {
                                        strcat(zip_files, zip_file_array[i]); //adds filename to string
                                        strcat(zip_files, "\n"); //adds newline to string
                                        free(zip_file_array[i]); //frees allocated memory
                                }
                                
                                char file_list[BUFFER_SIZE*4] = ""; //file list string
                                sprintf(file_list, "C files:\n%s\nPDF files:\n%s\nTXT files:\n%s\nZIP files:\n%s", c_files, pdf_files, txt_files, zip_files); //combines all file lists
                                send(sockfd, file_list, strlen(file_list), 0); //sends file list to client
                                
                                free(pdf_files_raw); //frees allocated memory
                                free(txt_files_raw); //frees allocated memory
                                free(zip_files_raw); //frees allocated memory
                        }
                }
                else 
                {
                        send(sockfd, "Unknown command", 15, 0); //sends response to client
                }
        }
}

int main()
{
        int server_fd, client_fd;
        struct sockaddr_in server_addr, client_addr;
        socklen_t client_len = sizeof(client_addr); //client length

        server_fd = socket(AF_INET, SOCK_STREAM, 0); //creates socket
        //Checks if socket creation is successful
        if (server_fd < 0) 
        {
                perror("Socket creation failed"); //prints error
                exit(1); //exits program
        }
        
        memset(&server_addr, 0, sizeof(server_addr)); //clears server address
        server_addr.sin_family = AF_INET; //sets server address family
        server_addr.sin_port = htons(PORT); //sets server port
        server_addr.sin_addr.s_addr = INADDR_ANY; //sets server address
        
        int opt = 1; //socket options
        //Checks if socket options are set successfully
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
        {
                perror("Setsockopt failed"); //prints error
                close(server_fd); //closes socket
                exit(1); //exits program
        }
        
        //Binds socket
        if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) 
        {
                perror("Bind failed"); //prints error
                close(server_fd); //closes socket
                exit(1); //exits program
        }
        
        //Listens for connections
        if (listen(server_fd, 5) < 0) 
        {
                perror("Listen failed"); //prints error
                close(server_fd); //closes socket
                exit(1); //exits program
        }
        printf("Server S1 started. Listening on port %d...\n", PORT); //prints progress
        
        //Accepts connections and forks process for each client
        while (1) 
        {
                client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len); //accepts connection
                //Checks if connection is successful
                if (client_fd < 0) 
                {
                        perror("Accept failed"); //prints error
                        continue; //continues loop
                }
            
                printf("Client connected\n"); //prints progress
            
                //Fork process to handle client
                pid_t pid = fork();
                //Checks if fork is successful
                if (pid < 0) 
                {
                        perror("Fork failed"); //prints error
                        close(client_fd); //closes client socket
                }
                //Child process
                else if (pid == 0) 
                {
                        close(server_fd); //closes server socket
                        prcclient(client_fd); //handles client
                        close(client_fd); //closes client socket
                        exit(0); //exits program
                }
                //Parent process
                else 
                {
                        close(client_fd); //closes client socket
                        while (waitpid(-1, NULL, WNOHANG) > 0); //waits for child process to finish
                }
        }
        close(server_fd); //closes server socket
        return 0;
}