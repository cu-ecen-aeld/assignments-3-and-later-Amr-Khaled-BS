#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "string.h"
#include "sys/socket.h"
#include "sys/types.h"
#include "netinet/in.h"
#include "arpa/inet.h"
#include "errno.h"
#include "error.h"
#include "syslog.h"
#include "signal.h"
#include <pthread.h>
#include <time.h>
#include "queue.h"

#define BUFFER_SIZE 1024
#define OFN "/var/tmp/aesdsocketdata"


struct client_t {
    struct sockaddr_in addr;
    int addr_len;
    int sd;
};


int server;
volatile int run;
pthread_mutex_t text_file_lock = PTHREAD_MUTEX_INITIALIZER;
bool is_running = true;
timer_t timer_id;

//////////////// SLIST
struct slist_element
{
    pthread_t thread_id;
    bool thread_completed;
    int socket_fd;
    int text_fd;

    SLIST_ENTRY(slist_element)
    slist_elements;
};

SLIST_HEAD(slist_head, slist_element);
struct slist_head head;

void close_threads_res()
{
    struct slist_element *thread_in_list;

    while (!SLIST_EMPTY(&head))
    {
        thread_in_list = SLIST_FIRST(&head);

        thread_in_list->thread_completed = true;

        pthread_join(thread_in_list->thread_id, NULL);

        close(thread_in_list->text_fd);
        close(thread_in_list->socket_fd);

        SLIST_REMOVE_HEAD(&head, slist_elements);
        free(thread_in_list);
    }

    pthread_mutex_destroy(&text_file_lock);
}

void timer_handler()
{
    time_t now;
    char timestamp[100];

    time(&now);
    struct tm *time_info = localtime(&now);

    // Format the timestamp according to RFC 2822
    strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", time_info);

    // Write the timestamp to the file
    pthread_mutex_lock(&text_file_lock);
    FILE *file = fopen(FILE_PATH, "a");
    if (file == NULL)
    {
        perror("Failed to open file for timestamp");
        pthread_mutex_unlock(&text_file_lock);
        return;
    }
    fputs(timestamp, file);
    fclose(file);
    pthread_mutex_unlock(&text_file_lock);
}

void sd_handler(int sig)
{
    if(sig == SIGINT || sig == SIGTERM)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
        run = 0;
        shutdown(server, SHUT_RDWR);
    }
}

int main(int argc, char **argv)
{
    struct sockaddr_in sa;
    struct client_t c;
    char buffer[BUFFER_SIZE];
    size_t len;
    int recv_len;
    FILE *file;
    int daemon = 0;
    int opt;

    while((opt = getopt(argc, argv, "d")) != -1)
    {
        switch(opt)
        {
            case 'd':
                daemon = 1;
                break;
        }
    }

    run = 1;
    signal(SIGINT, sd_handler);
    signal(SIGTERM, sd_handler);
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER); 
    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server < 0)
    {
        syslog(LOG_ERR, "[ERROR %d] %s\n", errno, strerror(errno));
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9000);
    if(setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &run, sizeof(run)) < 0)
    {
        goto return_error;
    }
    if(bind(server, (struct sockaddr*)&sa, sizeof(sa)) < 0)
    {
        goto return_error;
    }
        
    struct itimerspec ts = {0};
    struct sigevent se = {0};
    /*
     * Set the sigevent structure to cause the signal to be
     * delivered by creating a new thread.
     */
    se.sigev_notify = SIGEV_THREAD;
    se.sigev_value.sival_ptr = &timer_id;
    se.sigev_notify_function = timer_handler;
    se.sigev_notify_attributes = NULL;

    ts.it_value.tv_sec = 10;
    ts.it_value.tv_nsec = 0;
    ts.it_interval.tv_sec = 10;
    ts.it_interval.tv_nsec = 0;

    if (timer_create(CLOCK_REALTIME, &se, &timer_id) < 0)
        perror("Create timer");

    if (timer_settime(timer_id, 0, &ts, 0) < 0)
        perror("Set timer");
        
    if(listen(server, 5) < 0 )
    {
        goto return_error;
    }

    while(run)
    {
        if(daemon)
        {
            daemon = fork();
            if(daemon == -1)
                goto return_error;
            else if(daemon != 0)
            {
                syslog(LOG_INFO, "running daemonized");
                return 0;
            }
        }
        syslog(LOG_INFO, "waiting for connections on port %hd", ntohs(sa.sin_port));
        memset(&c, 0, sizeof(struct client_t));
        c.sd = accept(server, (struct sockaddr*)&c.addr, &c.addr_len);
        if(c.sd < 0)
        {
            if(errno == EINTR || errno == EINVAL)
            {
                syslog(LOG_INFO, "Shutting down\n");
                break;
            }
            goto return_error;
        }
        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(c.addr.sin_addr));
        
        file = fopen(OFN, "a");
        while(1)
        {
            memset(buffer, 0, BUFFER_SIZE);
            recv_len = recv(c.sd, buffer, BUFFER_SIZE-1, 0);
            if(recv_len < 0)
            {
                close(c.sd);
                goto return_error;
            }
            else if(recv_len == 0)
            {
                //client terminated connection
                break;
            }
            fputs(buffer, file);
            if(recv_len < BUFFER_SIZE && buffer[recv_len-1] == '\n')
            {
                file = freopen(OFN, "r", file);
                while(fgets(buffer, BUFFER_SIZE, file) != 0)
                {
                    send(c.sd, buffer, strlen(buffer), 0);
                }
                break;
            }
        }
        
        close(c.sd);
        syslog(LOG_INFO, "Closed connection from %s\n", inet_ntoa(c.addr.sin_addr));
        fclose(file);
    }

    close(server);
    remove(OFN);
    closelog();
    return 0;


return_error:
    syslog(LOG_ERR, "[ERROR %d] %s\n", errno, strerror(errno));
    closelog();
    close(server);
    if(c.sd != 0)
        close(c.sd);
    return -1;
}