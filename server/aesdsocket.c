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

#define BUFFER_SIZE 1024
#define OFN "/var/tmp/aesdsocketdata"


struct client_t {
    struct sockaddr_in addr;
    int addr_len;
    int sd;
};


int server;
volatile int run;

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