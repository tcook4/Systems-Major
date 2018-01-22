// Written by Tyler Cook, Francisco Rodriguez, Kyle Pruett, and Luis Alba
// UNT CSCE 3600.001
// Major Assignment
// May 8th, 2017
// Description: This program is the server side to a client. It can pass messages using [message] or files using [put].
// Compiles with gcc and -lpthread

#define BUFFER_SIZE 1024 // max size of our communication buffer
#define MAX_CLIENTS 4 // maximum number of connected clients

int clients_connected = 0; // how many clients are connected

// client information structure held by main
typedef struct client
{
    int fd; // copy of this client's file descriptor
    int number; // this client's number
    int status; // the connection status of this client
} client;

// client info passed to new client thread
typedef struct client_args
{
    int socket; // client's copy of their FD
    int client_number; // which client we are
} client_args;

client client_array[MAX_CLIENTS]; // array of client information

#include <stdio.h>          // printf()
#include <stdlib.h>         // size_t
#include <netdb.h>          // hostent
#include <netinet/in.h>     // sockaddr_in
#include <string.h>         // strcmp()
#include <errno.h>          // EINTR
#include <pthread.h>        // pthreads
#include <unistd.h>         // dup()

// client thread function
void* client_handler(void* args);

// get the size of a file
int file_size(char* file);

// check the status of a socket read() or write() call
void check_status(int value);

// check the status message of a socket read() call
void check_message(char* message);

int main()
{
    int sockfd, newsockfd, portno, clilen, ret;     // status and socket ints
    int opt = 1;                                    // set socket option
    int connected = 1;                              // connection status bool value
    int n = 0;                                      // thread counter
    int i = 0;                                      // iterator
    struct sockaddr_in serv_addr, cli_addr;         // socket address structure
    pthread_t thread_array[MAX_CLIENTS];            // array of threads
    client_args args[MAX_CLIENTS];                  // client thread args

    // initialize our client status array
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        client_array[i].status = 0;
        client_array[i].number = i+1;
        client_array[i].fd = 0;
    }

    // first call to socket()
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("ERROR opening socket");
        exit(1);
    }

    // set sock opt
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0 )
    {
        perror("Set sock opt\n");
        exit(1);
    }

    // initialize socket struct
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 23459;
    printf("Listening on port %d\n", portno);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    // bind the host address
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR on binding");
        exit(1);
    }

    // outer loop listens on our socket while we are not connected to a client
    while (connected) // run forever
    {
        // listen on our socket
        printf("Waiting for connection...\n");
        listen(sockfd,5);
        clilen = sizeof(cli_addr);

        // accept connection from the client
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0)
        {
            perror("ERROR on accept");
            exit(1);
        }

        n = -1; // we haven't saved the client yet
        // save this socket descriptor
        for (i = 0; i < MAX_CLIENTS; i++)
        {
            if (client_array[i].status == 0)
            {
                // save conneciton here
                client_array[i].status = 1;
                client_array[i].fd = dup(newsockfd);
                n = i;
                break;
            }
        }
        // we didn't have room for this client
        if (n == -1)
        {
            perror("Error: All connections in use!\n");
            ret = write(newsockfd, "error ERROR: All connections in use!\n", 38);
            continue;
        }
        else
        {
            printf("Connection accepted!\n");
        }

        // create our args struct
        args[n].client_number = n+1;
        args[n].socket = newsockfd;

        // pass this socket to a new thread handling this client
        if ((ret = pthread_create(&thread_array[n], NULL, client_handler, (void*) &args[n])) < 0)
        {
            perror("Thread creation failed\n");
            exit(1);
        }

        // increment our clients
        clients_connected++;
    }

    return 0;
}


// handles all operations for a specific client
// this is our server's pthread thread function
void* client_handler(void* args)
{
    int newsockfd;                       // our socket file descriptor
    int connected = 1;                   // connection status bool value
    int firstTime = 1;                   // first time bool status
    int read_this_time;                  // data recieved in a read() call
    int my_client_number;                // which client we are responsible for
    int i;                               // iterator
    int offset;                          // used in file transfer
    int sent;                            // status for all our socket messages
    int file_length;                     // file size storages
    uint32_t converted_file_length;      // converted filesize
    char buffer[BUFFER_SIZE];            // our buffer for communication
    char messageBuffer[BUFFER_SIZE];     // secondary buffer
    char* message;                       // pointer for extracted message
    char filename[256];                  // file name storage
    char command[256];                   // command storage
    char username[256];                  // username storage
    FILE* fp;                            // file pointer
    size_t bytes_read;                   // number of elements written in a send() or fread()
    client_args* client_info;            // our args passed from main

    // assign our socket and client number
    client_info = (client_args*) args;
    newsockfd = client_info->socket;
    my_client_number = client_info->client_number;

    // inner loop reads and handles commands from connected client
    while (connected)
    {
        // first message is our username
        if (firstTime)
        {
            bzero(username, 256);
            check_status(sent = read(newsockfd, username, 255));
            firstTime = 0;
        }

        // read in our buffer
        bzero(buffer,BUFFER_SIZE);
        check_status(sent = read(newsockfd, buffer, BUFFER_SIZE-1));

        // check if we're quitting
        if(strcmp(buffer, "quit") == 0)
        {
            // create our message to send to the other threads
            bzero(messageBuffer, BUFFER_SIZE);
            strcpy(messageBuffer, "message");
            strcat(messageBuffer, " ");
            strcat(messageBuffer, username);
            strcat(messageBuffer, " ");
            strcat(messageBuffer, "has left the chat.\n");

            // send this message to our other clients
            for (i = 0; i < MAX_CLIENTS; i++)
            {
                // skip ourselves or a disconnected client
                if ((i == my_client_number-1) || (client_array[i].status == 0))
                {
                    continue;
                }
                else // send quit message
                {
                    check_status(sent = write(client_array[i].fd, messageBuffer, strlen(messageBuffer)));
                }
            }

            // cleanup and close
            connected = 0;
            close(newsockfd);
            printf("Client disconnected.\n");
            clients_connected--;
            client_array[my_client_number-1].status = 0;
            client_array[my_client_number-1].fd = 0;
            break;
        }

        // we're not quitting
        else
        {
            // strip our command
            strcpy(command, strtok(buffer, " "));
        }

        // if we're receiving a message
        if (strcmp(command, "message") == 0)
        {
            // extract the rest of our message
            message = strtok(NULL, "");

            // create our message to send to the other threads
            bzero(messageBuffer, BUFFER_SIZE);
            strcpy(messageBuffer, "message");
            strcat(messageBuffer, " ");
            strcat(messageBuffer, username);
            strcat(messageBuffer, " ");
            strcat(messageBuffer, message);

            // send this message to our other clients
            for (i = 0; i < MAX_CLIENTS; i++)
            {
                // notify user if no one else if available to talk to
                if (clients_connected < 2)
                {
                    check_status(sent = write(newsockfd, "error No other clients connected!\n", 38));
                    break;
                }

                // skip ourselves or a disconnected client
                if ((i == my_client_number-1) || (client_array[i].status == 0))
                {
                    continue;
                }
                else
                {
                    // send message
                    check_status(sent = write(client_array[i].fd, messageBuffer, strlen(messageBuffer)));
                }
            }
        }

        // we are receiving a file from the client
        else if (strcmp(command, "put") == 0)
        {
            // get filename
            strcpy(filename, strtok(NULL, " "));

            // open the file
            fp = fopen(filename, "wb");
            if (fp == NULL)
            {
                printf("Error opening filename!\n");
                check_status(sent = write(newsockfd, "File open failed!", 17));
                exit(1);
            }

            // create transfer command
            bzero(buffer, BUFFER_SIZE);
            strcpy(buffer, "transfer");
            strcat(buffer, " ");
            strcat(buffer, filename);

            // send transfer command and wait for filesize
            // we're now talking to the client's listen thread
            check_status(sent = write(newsockfd, buffer, strlen(buffer)));

            // read in filesize
            check_status(sent = read(newsockfd, &converted_file_length, sizeof(converted_file_length)));
            file_length = ntohl((converted_file_length));

            // respond with ready
            check_status(sent = write(newsockfd, "ready", 5));

            // recieve file
            // call read while there's still data to be read (EINTR can be recovered from)
            while ((((read_this_time = read(newsockfd, buffer, BUFFER_SIZE-1)) > 0) && (file_length > 0)) || (sent == -1 && errno == EINTR))
            {
                // write recieved data into our file pointer
                if ((bytes_read = fwrite(buffer, sizeof(char), read_this_time, fp)) > 0)
                {
                    // decrement our length to still be read
                    file_length -= read_this_time;
                    if (file_length == 0) // we're done
                    {
                        break;
                    }
                }
            }

            // close file
            fclose(fp);

            // send this file to the rest of our connected clients
            for (i = 0; i < MAX_CLIENTS; i++)
            {
                // create our message to send to the other threads
                bzero(messageBuffer, BUFFER_SIZE);
                strcpy(messageBuffer, "receive");
                strcat(messageBuffer, " ");
                strcat(messageBuffer, filename);
                strcat(messageBuffer, " ");
                strcat(messageBuffer, username);

                // skip our client or a disconnected FD
                if ((i == my_client_number-1) || (client_array[i].status == 0))
                {
                    continue;
                }

                // open the file
                fp = fopen(filename, "rb");
                if (fp == NULL)
                {
                    check_status(sent = write(newsockfd, "error Server Error: Requested file not found.\n", 50));
                    printf("ERROR: Cannot open file\n");
                    continue;
                }

                // send receive message
                check_status(sent = write(client_array[i].fd, messageBuffer, strlen(messageBuffer)));

                // find the size we'll be transmitting
                file_length = file_size(filename);
                converted_file_length = htonl(file_length);

                // send file size to client
                check_status(sent = write(client_array[i].fd, &converted_file_length, sizeof(converted_file_length)));

                // send file to client
                bytes_read = 0;
                while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE-1, fp)) > 0)
                {
                    offset = 0;
                    while ((sent = write(client_array[i].fd, buffer + offset, bytes_read)) > 0 || (sent == -1 && errno == EINTR))
                    {
                        // make sure we have a successful send before incrementing our position
                        if (sent > 0)
                        {
                            offset += sent;
                            bytes_read -= sent;
                        }
                    }
                }
                printf("File sent successfully.\n");
                fclose(fp);
            }
        }
    }
}


// get the size of a file
// this size is later passed to a client as a 'read until' marker
int file_size(char* file)
{
    int length; // size of the file

    // open the file and seek to the end
    FILE* f = fopen(file, "r");
    fseek(f, 0, SEEK_END);

    // assign our length and return it
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
        perror("Error reading or writing to client.\n");
    }
}

// check the status message of a socket read() call
// the keyword 'ready' is sent between client and server to signal the other they are ready to recieve certain data
void check_message(char* message)
{
    if (strcmp(message, "ready") != 0)
    {
        printf("Error: Server or client not ready!\n");
    }
}
