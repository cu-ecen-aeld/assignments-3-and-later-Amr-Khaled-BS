#include "stdio.h"
#include "stdlib.h"
#include "errno.h"
#include "syslog.h"
#include <sys/types.h>
#include <sys/socket.h>

// port 9000
// logging : syslog
// signal handling : SIGINT or SIGTERM
// /var/tmp/aesdsocketdata
// -d daemon fork modification

int main(int argc, char *argv[]){
    // logs in /var/log/syslog
    openlog(NULL, 0, LOG_USER);

    // socket();
    // bind();
    
    // listen();
    // accept();
    
    // recv();
    // read();

    // write();
    // send();

    if (argc > 1) {
        fprintf(stderr, "Entered: %s argument\n", argv[1]);
        // syslog(LOG_ERR, "invalid number of arguments");
        if(argv[1] == 'd'){
            fprintf(stderr, "Running in daemon mode");
        }
        return 1;
    }

    // Get filename and text from command line arguments
    const char *filename = "/var/tmp/aesdsocketdata";
    const char *string = "data";

    FILE *file = fopen(filename, "w");
    if(file == NULL){
        // perror("perror returned");
        fprintf(stderr, "errno for %s is %d:", filename, errno);
        syslog(LOG_ERR, "mylog: cannot open file: %s", filename);
        return 1;
    }
    else{
        syslog(LOG_DEBUG, "Writing %s to %s", string, filename);
        fprintf(file, "%s", string);
        fclose(file);
    }

    return 0;
}