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
#include <arpa/inet.h>


#define CHUNK_SIZE 1024
#define BUFFER_SIZE 1024
static volatile int keepRunning = 1;
static volatile int sockfd;
const char *filename = "/var/tmp/aesdsocketdata";


void intHandler(int dummy) {
    keepRunning = 0;
    syslog(LOG_INFO, "Caught signal, exiting");
    fprintf(stderr, "exit signal received");
    remove(filename);
    shutdown(sockfd, SHUT_RDWR);
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

    int fd, run;
    FILE *file;
    char buffer[BUFFER_SIZE] = { 0 };

    // server code (parent)
    struct addrinfo hints;
    struct addrinfo *servinfo;  // will point to the results
    socklen_t addrlen = sizeof(struct sockaddr);
    struct sockaddr_storage client_addr;  // To store the client's address
    char client_ip[INET_ADDRSTRLEN];      // Buffer for storing client IP address

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;    
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     

    int status = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        fprintf(stderr, "error in socket");
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &run, sizeof(run)) < 0) {
        syslog(LOG_ERR, "setsockopt() error: %d (%s)\n", errno, strerror(errno));
        fprintf(stderr, "error in socket");
        perror("socket failed");
    }

    if(bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0){
        fprintf(stderr, "error in bind");
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if(listen(sockfd, 5) < 0){
        fprintf(stderr, "error in listen");
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    int run_daemon = 0;
    if (argc > 1) {
        fprintf(stderr, "Entered: %s argument\n", argv[1]);
        if(argv[1][0] == '-' && argv[1][1] == 'd'){
            fprintf(stderr, "Running in daemon mode\n");
            run_daemon = 1;
        }
    }

    // start of loop
    while (keepRunning) { 
        if(run_daemon){
            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "error in fork");
                perror("fork failed");
                exit(EXIT_FAILURE);
            }
            else if (pid != 0) {
                fprintf(stderr, "running in daemon mode");
            }
        }

        fd = accept(sockfd, (struct sockaddr *)&client_addr, &addrlen);
        if(fd < 0){
            fprintf(stderr, "error in accept");
            perror("accept failed");
            exit(EXIT_FAILURE);
        }
        else{
            // Get the client IP address
            struct sockaddr_in *addr_in = (struct sockaddr_in *)&client_addr;
            inet_ntop(AF_INET, &(addr_in->sin_addr), client_ip, INET_ADDRSTRLEN);
            syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);
        }

        file = fopen(filename, "a+");
        if(file == NULL){
            fprintf(stderr, "errno for %s is %d:", filename, errno);
            syslog(LOG_ERR, "mylog: cannot open file: %s", filename);
            exit(EXIT_FAILURE);
        }

        while(1){
            memset(buffer, 0, BUFFER_SIZE);
            int valread = recv(fd, buffer, BUFFER_SIZE - 1, 0);
            if(valread < 0)
            {
                close(fd);
                fprintf(stderr, "error in recv");
                perror("recv failed");
                exit(EXIT_FAILURE);
            }
            else if(valread == 0)
            {
                // client terminated connection
                break;
            }

            fputs(buffer, file);
            fprintf(stderr, "buffer is %s\n", buffer);

            if(valread < BUFFER_SIZE && buffer[valread-1] == '\n')
            {
                file = freopen(filename, "r", file);
                while(fgets(buffer, BUFFER_SIZE, file) != 0)
                {
                    send(fd, buffer, strlen(buffer), 0);
                }
                break;
            }
        }

        close(fd);
        syslog(LOG_INFO, "Closed connection from %s\n", client_ip);
        fclose(file);
    }

    close(sockfd);
    closelog();
    freeaddrinfo(servinfo);

    // Delete the file at the end
    if (remove(filename) == 0) {
        fprintf(stderr, "\nFile deleted successfully.\n");
    } else {
        fprintf(stderr, "\nError deleting the file.\n");
    }

    return 0;
}
