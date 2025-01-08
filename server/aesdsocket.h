#ifndef AESDSOCKET_H
#define AESDSOCKET_H

/****************   Includes    ***************/ 
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>
#include "../aesd-char-driver/aesd_ioctl.h"

/****************   Macros     ***************/ 
#define DEBUG_LOG(msg,...) printf("INFO: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("ERROR: " msg "\n" , ##__VA_ARGS__)

#define SUCCESS 		(0)
#define ERROR 		(-1)

#define BACKLOG_CONNECTIONS	(10)
#define BUF_LEN		(1024)
#define TIMESTAMP_STRING_LENGTH     100


typedef struct
{
    bool file_open;
    bool log_open;
    bool socket_open;
    bool client_fd_open;
    bool signal_caught;
    bool daemon_mode;
    bool command_status_success;
} status_flags;

typedef struct
{
    pthread_t threadId;                     /**< Thread identifier */
    pthread_mutex_t *pMutex;                /**< Pointer to mutex for synchronization */
    bool isThreadComplete;                  /**< Flag to indicate if the thread has completed its task */
    int clientSocketFd;                     /**< File descriptor for the client socket */
    struct sockaddr_storage *pClientAddr;   /**< Pointer to client address information */
} ClientThreadData_t;

typedef struct node
{
    ClientThreadData_t thread_data;

    SLIST_ENTRY(node) nodes;
}node_t;

typedef struct
{
    pthread_t threadId;          /**< Thread identifier */
    pthread_mutex_t *pMutex;     /**< Pointer to mutex for synchronization */
    int timeIntervalSecs;        /**< Time interval for timestamps in seconds */
} ThreadTimestampData_t;


#endif // AESDSOCKET_H