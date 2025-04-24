# Distributed File System

## Overview
This project implements a distributed file system where multiple specialized servers work together to store and manage different types of files. The system consists of one main server (S1) that acts as a central hub and three specialized servers (S2, S3, and S4) that each handle specific file types.

## Architecture

### Components
- **S1 Server**: The central hub that routes requests to appropriate specialized servers based on file extensions. Handles `.c` files locally.
- **S2 Server**: Handles `.pdf` files exclusively.
- **S3 Server**: Handles `.txt` files exclusively.
- **S4 Server**: Handles `.zip` files exclusively.
- **Client**: Provides a command-line interface for interacting with the file system.

### Network Configuration
- S1 Server: Port 8007
- S2 Server: Port 8008 
- S3 Server: Port 8009
- S4 Server: Port 8010

## Features

### File Operations
- **Upload Files**: Upload files to the distributed system.
- **Download Files**: Download files from the system.
- **Remove Files**: Delete files from the system.
- **List Files**: Display all files in a directory across all servers.
- **Download Tar Archives**: Get a tar archive of all files of a specific type.

### Path Structure
The system uses a unified path structure starting with `~S1/` that simplifies interactions with the distributed system. Internally, this is mapped to the appropriate server:
- `~S1/path` for C files on S1
- `~S2/path` for PDF files on S2
- `~S3/path` for TXT files on S3
- `~S4/path` for ZIP files on S4

### Advanced Features
- **Recursive Directory Creation**: Automatically creates necessary directories during file operations.
- **Robust Error Handling**: Comprehensive error detection and recovery mechanisms.
- **Direct File Transfer Mode**: Alternative transfer protocol for large files or interrupted transfers.
- **Progress Tracking**: Real-time progress reporting during file transfers.

## Supported Commands

| Command | Description | Usage |
|---------|-------------|-------|
| uploadf | Upload a file | `uploadf <local_filename> <destination_path>` |
| downlf | Download a file | `downlf <remote_filepath>` |
| removef | Remove a file | `removef <remote_filepath>` |
| dispfnames | List files in a directory | `dispfnames <remote_dirpath>` |
| downltar | Download tar archive of files | `downltar <file_extension>` |
| exit | Exit the client | `exit` |

## Implementation Details

### Protocol
The system implements a custom file transfer protocol with:
- Command/response handshaking
- File size pre-transmission
- Chunked data transfer
- End-of-file markers
- Fallback mechanisms for interrupted transfers

### File Type Routing
Files are automatically routed to the appropriate server based on their extension:
- `.c` files → S1
- `.pdf` files → S2
- `.txt` files → S3
- `.zip` files → S4

## Building and Running

### Prerequisites
- C compiler (gcc or clang)
- Unix-like environment (Linux, macOS)
- Network connectivity between servers

### Compilation
```bash
# Compile the servers
gcc -o S1 S1.c
gcc -o S2 S2.c
gcc -o S3 S3.c
gcc -o S4 S4.c

# Compile the client
gcc -o w25clients w25clients.c
```

### Running

#### Method 1: Background Processes
Start the servers in the following order:
```bash
./S2 &
./S3 &
./S4 &
./S1 &
```

Then run the client:
```bash
./w25clients
```

#### Method 2: Multiple Terminal Windows
1. Open four terminal windows or tabs
2. In the first terminal, run:
   ```bash
   ./S2
   ```
3. In the second terminal, run:
   ```bash
   ./S3
   ```
4. In the third terminal, run:
   ```bash
   ./S4
   ```
5. In the fourth terminal, run:
   ```bash
   ./S1
   ```
6. Open a fifth terminal for the client:
   ```bash
   ./w25clients
   ```

This method allows you to see the debug output from each server separately, making it easier to observe the system's behavior and troubleshoot issues.

## Example Usage

```
# Upload a C file
w25clients$ uploadf example.c ~S1/example.c

# Upload a PDF file
w25clients$ uploadf document.pdf ~S1/documents/document.pdf

# Download a file
w25clients$ downlf ~S1/documents/document.pdf

# List all files in a directory
w25clients$ dispfnames ~S1/documents

# Download a tar archive of all PDF files
w25clients$ downltar .pdf

# Remove a file
w25clients$ removef ~S1/example.c
```

## Project Structure
```
.
├── S1.c                # Central server implementation
├── S2.c                # PDF file server implementation
├── S3.c                # TXT file server implementation
├── S4.c                # ZIP file server implementation
└── w25clients.c        # Client implementation
```

## Technical Implementation
The system uses TCP sockets for all communications and implements a custom application-layer protocol. File transfers are handled in chunks to manage memory efficiently, and the system includes robust error handling and recovery mechanisms.

---

_Author: Yuvashree Senthilmurugan_ 