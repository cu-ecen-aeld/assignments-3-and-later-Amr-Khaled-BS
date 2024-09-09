/*
$1 output file path + name
$2 string to be written in file

Exits with value 1 error and print statements if any of the arguments above were not specified

Creates a new file with name and path writefile with content writestr, overwriting any existing file and creating the path if it doesn’t exist. 
Exits with value 1 and error print statement if the file could not be created.

ex. writer.sh /tmp/aesd/assignment1/sample.txt ios
*/
#include "stdio.h"
#include "stdlib.h"
#include "errno.h"
#include "syslog.h"


int main(int argc, char *argv[]){
    // logs in /var/log/syslog
    openlog(NULL, 0, LOG_USER);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <filename> <text>\n", argv[0]);
        syslog(LOG_ERR, "invalid number of arguments");
        return 1;
    }

    // Get filename and text from command line arguments
    const char *filename = argv[1];
    const char *string = argv[2];
    // const char *filename = "file1.txt";
    // const char *string = "my name is amr\n";

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
        // fwrite(file, sizeof(string), sizeof(char), string);
        fclose(file);
    }

    return 0;
}