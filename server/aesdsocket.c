#include "stdio.h"
#include "stdlib.h"
#include "errno.h"
#include "syslog.h"
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define CHUNK_SIZE 1024
static volatile int keepRunning = 1;

void intHandler(int dummy) {
    keepRunning = 0;
    fprintf(stderr, "exit signal received");
}

// port 9000
// logging : syslog
// /var/tmp/aesdsocketdata
// signal handling : SIGINT or SIGTERM
// -d daemon fork modification

int main(int argc, char *argv[]){
    // logs in /var/log/syslog
    openlog(NULL, 0, LOG_USER);
    signal(SIGINT, intHandler);
    signal(SIGTERM, intHandler);

    int fd;
    FILE *file;
    const char *filename = "/var/tmp/aesdsocketdata";
    char buffer[1024] = { 0 };

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        fprintf(stderr, "error in socket");
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // server code (parent)
    struct addrinfo hints;
    struct addrinfo *servinfo;  // will point to the results
    socklen_t addrlen = sizeof(struct sockaddr);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;    
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     

    int status = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }
    
    if( bind(sockfd, servinfo->ai_addr, addrlen) < 0){
        fprintf(stderr, "error in bind");
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    // connect(sockfd, servinfo->ai_addr, sizeof(struct sockaddr));

    if( listen(sockfd, 2) < 0){
        fprintf(stderr, "error in listen");
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    // start of loop
    // while (keepRunning) { 
        fd = accept(sockfd, servinfo->ai_addr, &addrlen);
        if(fd < 0){
            fprintf(stderr, "error in accept");
            perror("accept failed");
            exit(EXIT_FAILURE);
        }
        else{
            syslog(LOG_DEBUG, "Accepted connection from xxx");
        }

    // while (keepRunning) { 
        int valread = read(fd, buffer, 1024 - 1); // subtract 1 for the null terminator at the end
        // int valread = recv(fd, buffer, 1024 - 1, 0); // subtract 1 for the null terminator at the end
        // fprintf(stderr, "read %d chars\n", valread);
        fprintf(stderr, "buffer is %s\n", buffer);
        // send(fd, buffer, strlen(buffer), 0);

        file = fopen(filename, "a+");
        if(file == NULL){
            // perror("perror returned");
            fprintf(stderr, "errno for %s is %d:", filename, errno);
            syslog(LOG_ERR, "mylog: cannot open file: %s", filename);
            return 1;
        }
        else{
            syslog(LOG_DEBUG, "Writing %s to %s", buffer, filename);
            fprintf(file, "%s", buffer);
            fclose(file);
        }
    // }
    // end of loop

    // cleanup
    file = fopen(filename, "r");
    if(file == NULL){
        // perror("perror returned");
        fprintf(stderr, "errno for %s is %d:", filename, errno);
        syslog(LOG_ERR, "mylog: cannot open file: %s", filename);
        return 1;
    }
    else{
        // syslog(LOG_DEBUG, "sending %s to %s", buffer, filename);
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
            // Send the chunk to the server
            if (send(fd, buffer, bytes_read, 0) == -1) {
                printf("Failed to send data.\n");
            }
        }
        fclose(file);
    }

    syslog(LOG_DEBUG, "Closed connection from XXX");
    freeaddrinfo(servinfo);
    close(fd);
    close(sockfd);

    // Delete the file at the end
    if (remove(filename) == 0) {
        fprintf(stderr, "\nFile deleted successfully.\n");
    } else {
        fprintf(stderr, "\nError deleting the file.\n");
    }


    // part daemon
    fprintf(stderr, "Entered: %d argument\n", argc);
    if (argc > 1) {
        fprintf(stderr, "Entered: %s argument\n", argv[1]);
        // syslog(LOG_ERR, "invalid number of arguments");
        if(argv[1][0] == '-' && argv[1][1] == 'd'){
            fprintf(stderr, "Running in daemon mode\n");
            // int status;
            pid_t pid = fork();
            if (pid<0) {
                return 1;
            }
            else if (pid==0) {
                // child process
                // return 1;
            }
        }
    }

    return 0;
}