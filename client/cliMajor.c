// Written by Tyler Cook, Francisco Rodriguez, Kyle Pruett, and Luis Alba
// UNT CSCE 3600.001
// Major Assignment
// May 8th, 2017
// Description: This program is the client side to a server. It can send messages to the server using [message]
//              or send files with [put].
// Compiles with gcc and -lpthread

#define BUFFER_SIZE 1024  // max size of a message buffer

#include <stdio.h>          // printf()
#include <stdlib.h>         // size_t
#include <netdb.h>          // hostent
#include <netinet/in.h>     // sockaddr_in
#include <string.h>         // strcmp()
#include <errno.h>          // EINTR
#include <pthread.h>        // pthreads

// client listen thread function
void* recieve_message (void* socket_info);

// get the size of a file
int file_size(char* file);

// check the status of a socket read() or write() call
void check_status(int value);

// check the status message of a socket read() call
void check_message(char* message);

int main()
{
    int sockfd, portno, sent;                 // socket address info
    int rc;                                   // thread data
    int connected;                            // connection status bool value
    int first_time = 1;                       // bool for first time usage message display
    char command[100];                        // command storage
    char filename[100];                       // filename storage
    char username[100];                       // our client's username
    char buffer[256];                         // buffer for communication with server
    char messageBuffer[BUFFER_SIZE];          // incoming message buffer
    char sendMessage[BUFFER_SIZE];            // outgoing message buffer
    struct sockaddr_in serv_addr;             // socket structure
    struct hostent *server;                   // socket host struct
    FILE* fp;                                 // our file pointer
    pthread_t listen_thread;                  // listen thread

    // assign our port number and server name
    portno = 23459;
    printf("Attempting to connect to cse04.cse.unt.edu on port %d\n", portno);

    // create a socket point
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("ERROR opening socket");
        exit(1);
    }

    // assign and address our server
    server = gethostbyname("cse04.cse.unt.edu");
    if (server == NULL)
    {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    // fill in our server address struct
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    // connect to our server
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting");
        exit(1);
    }
    printf("Success!\n");
    connected = 1;

    // launch our listen thread
    rc = pthread_create(&listen_thread, NULL, recieve_message, (void*) sockfd);
    if (rc < 0)
    {
        perror("Pthread_create error\n");
        exit(1);
    }

    // main body loop
    while (connected)
    {
        // prompt for username and print usage message
        if (first_time)
        {
            // get username
            printf("Please enter a user name: ");
            scanf("%s", username);
            check_status(sent = write(sockfd, username, strlen(username)));

            // print usage message
            printf("Please enter a command to send to the server.\n");
            printf("Supported commands are \"message [message contents]\", \"put [file]\", or \"quit\".\n");
            first_time = 0; // only display this message once
        }

        // get a command
        scanf("%s", command);

        // check if we're quitting
        if (strcmp(command, "quit") == 0)
        {
            printf("Exiting...\n");
            check_status(sent = write(sockfd, "quit", 5));
            close(sockfd);
            exit(0);
        }

        // send a message to everyone on the server
        else if (strcmp(command, "message") == 0)
        {
            // get the rest of our message
            fgets(messageBuffer, 915,  stdin);

            // form our message to server
            bzero(sendMessage, BUFFER_SIZE);
            strcpy(sendMessage, command);
            strcat(sendMessage, " ");
            strcat(sendMessage, messageBuffer);

            // send server our message
            check_status(sent = write(sockfd, sendMessage, strlen(sendMessage)));
        }

        // transfer a file to everyone on the server
        else if (strcmp(command, "put") == 0)
        {
            // get our filename
            scanf("%s", filename);

            // make sure we have the file
            fp = fopen(filename, "rb");
            if (fp == NULL)
            {
                printf("ERROR: Cannot open file\n");
                continue;
            }
            fclose(fp);

            // form our command
            bzero(buffer, 256);
            strcpy(buffer, command);
            strcat(buffer, " ");
            strcat(buffer, filename);

            // send server our request (command + filename)
            // from here, server acknowledges our request and our listen thread handles the rest
            check_status(sent = write(sockfd, buffer, strlen(buffer)));
        }
        else // command not recognized
        {
            printf("ERROR: Command not recognized, please try again.\n");
            scanf("%*[^\n]%*1[\n]"); // clear our input buffer
        }
    }

    // we shouldn't reach here
    return 0;
}

// handles all the recieved messages from a server
// this is our client's pthread thread function
void* recieve_message (void* socket_info)
{
    int sockfd;                                 // our socket file descriptor
    int ret, sent, read_this_time;              // status ints
    int file_length, offset;                    // used in file transfer operations
    uint32_t converted_file_length;             // converted filesize
    char buffer[BUFFER_SIZE];                   // our server communication buffer
    char* message;                              // pointer for extracting message from server
    char command[256];                          // command storage
    char username[256];                         // username storage
    char filename[256];                         // filename storage
    FILE* fp;                                   // our file pointer
    size_t bytes_read;                          // track fread() and fwrite() amounts

    // assign our socket
    sockfd = (int)socket_info;

    for (;;) // run forever
    {
        bzero(buffer, BUFFER_SIZE);
        if ((ret = read(sockfd, buffer, BUFFER_SIZE-1)) > 0) // get a message if there is one
        {
            // first word identifies type of message
            strcpy(command, strtok(buffer, " "));

            // message from other client
            if (strcmp(command, "message") == 0)
            {
                // extract username and message
                strcpy(username, strtok(NULL, " "));
                message = strtok(NULL, ""); // message is the rest of our string

                // print message
                printf("%s: %s", username, message);
            }

            // we are transmitting a file
            else if (strcmp(command, "transfer") == 0)
            {
                // get filename
                strcpy(filename, strtok(NULL, " "));

                // open the file
                fp = fopen(filename, "rb");
                if (fp == NULL)
                {
                    printf("ERROR: Cannot open file\n");
                    continue;
                }

                // find the size we'll be transmitting
                file_length = file_size(filename);
                converted_file_length = htonl(file_length);

                // send server the filesize
                check_status(sent = write(sockfd, &converted_file_length, sizeof(converted_file_length)));

                // read server ready message
                bzero(buffer, BUFFER_SIZE);
                check_status(sent = read(sockfd, buffer, BUFFER_SIZE-1));
                check_message(buffer);

                // notify user upload operation is beginning
                printf("Uploading...\n");

                // send file to server
                // read from our file descriptor to our buffer, and write that data to our socket
                bytes_read = 0;
                while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE-1, fp)) > 0)
                {
                    // make sure we have a successful send before incrementing our position
                    offset = 0;
                    while (((sent = write(sockfd, buffer + offset, bytes_read)) > 0) || (sent == -1 && errno == EINTR))
                    {
                        // if we sent something, increment our positions
                        if (sent > 0)
                        {
                            offset += sent;
                            bytes_read -= sent;
                        }
                    }
                }
                // close our file and notify user operation is completed
                fclose(fp);
                printf("File successfully uploaded!\n");
            }

            // we are receiving a file
            else if (strcmp(command, "receive") == 0)
            {
                // extract filename and user who sent file
                strcpy(filename, strtok(NULL, " "));
                strcpy(username, strtok(NULL, " "));
                printf("User %s sent a file: %s. Downloading...\n", username, filename);

                // read in filesize
                check_status(sent = read(sockfd, &converted_file_length, sizeof(converted_file_length)));
                file_length = ntohl((converted_file_length));

                // open the file
                fp = fopen(filename, "wb");
                if (fp == NULL)
                {
                    printf("Error opening file!\n");
                    exit(1);
                }

                // get our file from the server
                // call read while we still need to get data (EINTR error can be recovered from)
                while ((((read_this_time = read(sockfd, buffer, BUFFER_SIZE-1)) > 0) && (file_length > 0)) || (read_this_time == -1 && errno == EINTR))
                {
                    // write recieved info to our file
                    if ((bytes_read = fwrite(buffer, sizeof(char), read_this_time, fp)) > 0)
                    {
                        // subtract data recieved from data left to write
                        file_length -= read_this_time;
                        if (file_length == 0) // we're done
                        {
                            break;
                        }
                    }
                }

                // close our file and notify user operation is completed
                fclose(fp);
                printf("File successfully downloaded!\n");
            }

            // error message
            else if (strcmp(command, "error") == 0)
            {
                // extract error message
                message = strtok(NULL, "");
                printf("Server Error: %s", message);
            }
        }
    }
}

// get the size of a file
// this size is later passed to a server as a 'read until' marker
int file_size(char* file)
{
    int length; // length of the file we're finding

    // open the file and seek to the end
    FILE* f = fopen(file, "r");
    fseek(f, 0, SEEK_END);

    // assign our variable and return it
    length = (int)ftell(f);
    fclose(f);
    return length;
}

// check the value of a socket read() or write() call
// our socket calls are wrapped in this function
void check_status(int value)
{
    if (value < 0)
    {
        perror("Error reading or writing to server.\n");
        exit(1);
    }
}

// check the status message of a socket read() call
// the keyword 'ready' is sent between client and server to signal the other they are ready to recieve certain data
void check_message(char* message)
{
    if (strcmp(message, "ready") != 0)
    {
        printf("Error: Server or client not ready!\n");
        exit(1);
    }
}

