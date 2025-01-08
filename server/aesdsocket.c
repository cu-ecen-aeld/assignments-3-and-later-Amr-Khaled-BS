#include "aesdsocket.h"

/****************   Macros     ***************/ 
#define USE_AESD_CHAR_DEVICE
#ifdef USE_AESD_CHAR_DEVICE
	#define DATA_FILE "/dev/aesdchar"
#else
	#define DATA_FILE "/var/tmp/aesdsocketdata"
#endif
const char *ioctl_str = "AESDCHAR_IOCSEEKTO:";

/****************   Global Variables     ***************/ 
sig_atomic_t fatal_error_in_progress = 0;

// Daemon application
bool daemon_mode = false;
int dataFileDescriptor;

// Server & Client Socket fd
int sock_fd;
int clientSocketFd;
// linked list head init
SLIST_HEAD(head_s, node) head;
node_t * node = NULL;
pthread_mutex_t lock;
#ifndef USE_AESD_CHAR_DEVICE
ThreadTimestampData_t TS_data;
#endif

char s[INET6_ADDRSTRLEN];

void main_socket_application();
int open_socket();
int run_daemon();
int accept_and_log_client();
void cleanup_on_exit();
void *recv_send_thread(void *thread_param);
int setup_time_logging(void);
void *log_timestamps(void *timestamp_param);
void* client_data_handler(void *thread_param);

// Initialize all elements to false
status_flags s_flags = {false, false, false, false, false, false, false};
struct addrinfo *result;

void free_and_nullify_result() {
    if (result != NULL) {
        freeaddrinfo(result);
        result = NULL;
    }
}

void handle_termination(int sig)
{
    // Prevent recursive invocation of the handler
    if (fatal_error_in_progress) {
        raise(sig);
        return;
    }
    fatal_error_in_progress = 1;

    // Log that the program is preparing to terminate due to a specific signal
    syslog(LOG_INFO, "Signal %d received, initiating graceful shutdown.", sig);

    // Attempt to close the socket and log an error if unsuccessful
    if (shutdown(sock_fd, SHUT_RDWR) == -1) {
        syslog(LOG_ERR, "Unable to properly close socket.");
    }

    s_flags.signal_caught = true;
    cleanup_on_exit();
    
    // Reset the signal handling to default and re-raise the signal for standard termination
    signal(sig, SIG_DFL);
    raise(sig);
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void cleanup_on_exit(void)
{
    int ret_status;
    syslog(LOG_INFO, "Initiating clean-up procedures.");

    // Close data file
    ret_status = close(dataFileDescriptor);
    if(ret_status == -1)
    {
        syslog(LOG_ERR, "Failed to close data file.");
        s_flags.command_status_success = false;
    }

    // Delete data file
#ifndef USE_AESD_CHAR_DEVICE
    ret_status = unlink(DATA_FILE);
    if(ret_status == -1)
    {
        syslog(LOG_ERR, "Failed to delete data file.");
    }
#endif

    // Free elements from the queue if any (assuming head is defined elsewhere)
    while (!SLIST_EMPTY(&head))
    {
        node = SLIST_FIRST(&head);
        SLIST_REMOVE(&head, node, node, nodes);
        free(node);
        node = NULL;
    }

    if(s_flags.signal_caught != true){
#ifndef USE_AESD_CHAR_DEVICE
    // Join timestamp thread
    pthread_join(TS_data.threadId, NULL);
#endif
    // Destroy mutex lock (assuming lock is defined elsewhere)
    pthread_mutex_destroy(&lock);

    }

    // Close socket
    if (s_flags.socket_open)
    {
        close(sock_fd);
        s_flags.socket_open = false;
    }

    // Close client descriptor
    if(s_flags.client_fd_open)
    {
        close(clientSocketFd);
        s_flags.client_fd_open = false;
    }

    // Free and nullify addrinfo result
    free_and_nullify_result();

    // Close syslog
    syslog(LOG_INFO, "Application is shutting down.");
    if(s_flags.log_open)
    {
        closelog();
        s_flags.log_open = false;
    }
}

int main(int argc, char *argv[])
{
    int opt;
    int ret;

    // Initialize the mutex for threads
    ret = pthread_mutex_init(&lock, NULL);
    if(ret != 0)
    {
        syslog(LOG_ERR, "mutex init failed");
        return -1;
    }

    // Open syslog
    openlog(NULL, 0, LOG_USER);

    while((opt = getopt(argc, argv, "d")) != -1)
    {
        if(opt == 'd')
        {
            s_flags.daemon_mode = true;
        }
    }

    main_socket_application();

    return (s_flags.command_status_success) ? 0 : -1;
}

void main_socket_application()
{
    int ret_status;
    struct addrinfo hints;
    int yes = 1;  // for setsockopt()

    // signal handler for SIGINT and SIGTERM
    signal(SIGINT, handle_termination);
    signal(SIGTERM, handle_termination);
    
    // Initialize syslog
    syslog(LOG_INFO,"AESD Socket application started");
    if(s_flags.daemon_mode)
    {
        syslog(LOG_INFO,"Started as a daemon");
    }
    
    // Initialize hints struct
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;	        /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;    /* stream socket */
    hints.ai_flags = AI_PASSIVE;        /* For local IP address */
    hints.ai_protocol = 0;              /* Any protocol */
    
    ret_status = getaddrinfo(NULL, "9000", &hints, &result);
    if (ret_status != SUCCESS)
    {
        syslog(LOG_ERR, "Failure in getaddrinfo()");
        cleanup_on_exit();
        return;
    }
    
    if (result == NULL)
    {
        syslog(LOG_ERR, "Memory allocation failed in getaddrinfo()");
        cleanup_on_exit();
        return;
    }
    
    sock_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock_fd == ERROR)
    {
        syslog(LOG_ERR, "Failed to create socket");
        cleanup_on_exit();
        return;
    }
    
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == ERROR)
    {
        syslog(LOG_ERR, "Failed to set socket options");
        cleanup_on_exit();
        return;
    }
    
    ret_status = bind(sock_fd, result->ai_addr, sizeof(struct sockaddr));
    if (ret_status == ERROR)
    {
        syslog(LOG_ERR, "Binding socket operation unsuccessful");
        cleanup_on_exit();
        return;
    }
    
    free_and_nullify_result();
    if(s_flags.daemon_mode == 1)
    {
        ret_status = run_daemon();
        if(ret_status == ERROR)
        {
            syslog(LOG_ERR, "Failed to start as daemon");
            cleanup_on_exit();
            return;
        }
    }
#ifndef USE_AESD_CHAR_DEVICE
    // Set up timestamp
    ret_status = setup_time_logging();
    if(ret_status == ERROR)
    {
        syslog(LOG_ERR, "Failed to setup timestamp");
        cleanup_on_exit();
        return;
    }
#endif 

    ret_status = listen(sock_fd, BACKLOG_CONNECTIONS);
    if(ret_status == ERROR)
    {
        syslog(LOG_ERR, "Failed to listen on socket");
        cleanup_on_exit();
        return;
    }
    
    ret_status = accept_and_log_client(s);
    if(ret_status == ERROR)
    {
        syslog(LOG_ERR, "Failed to start communication");
        cleanup_on_exit();
        return;
    }

    cleanup_on_exit();
}

int run_daemon(void)
{
    int fd; // File descriptor for redirecting stdin, stdout, stderr
    pid_t forked_pid = fork(); // Fork the current process

    if (forked_pid < 0)
    {
        ERROR_LOG("Failed to fork process\n");
        syslog(LOG_ERR, "Failed to fork process");
        return ERROR;
    }

    if (forked_pid > 0)
    {
        syslog(LOG_INFO, "Termination of parent process completed");
        exit(0); // Exit the parent process
    }

    umask(0);
    // Create a new session ID
    if (setsid() < 0)
    {
        syslog(LOG_ERR, "setsid failed");
        return -1;
    }

    // Change the current working directory to root
    if (chdir("/") == -1)
    {
        syslog(LOG_ERR, "chdir failed");
        return -1;
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect stdin, stdout, stderr to /dev/null
    fd = open("/dev/null", O_RDWR);
    if (fd == -1)
    {
        syslog(LOG_ERR, "/dev/null open failed");
        return -1;
    }

    if (dup2(fd, STDIN_FILENO) == -1)
    {
        syslog(LOG_ERR, "stdin redirect failed");
        return -1;
    }

    if (dup2(fd, STDOUT_FILENO) == -1)
    {
        syslog(LOG_ERR, "stdout redirect failed");
        return -1;
    }

    if (dup2(fd, STDERR_FILENO) == -1)
    {
        syslog(LOG_ERR, "stderr redirect failed");
        return -1;
    }

    close(fd);
    return 0;
}

int accept_and_log_client(char *ip_address)
{
    // Variables for pthreads
    void * threadRetVal = NULL;
    
    // Variables for accept() command
    struct sockaddr_storage clientInfo;
    socklen_t clientSize = sizeof(struct sockaddr_storage);
    
    // Create new node in linked list
    node_t *freshNode;

    while (!fatal_error_in_progress)
    {
        clientSocketFd = accept(sock_fd, (struct sockaddr *)&clientInfo, &clientSize);
        if (clientSocketFd == ERROR)
        {
            if (fatal_error_in_progress == 0)
            {
                syslog(LOG_ERR, "Failed to accept client connection");
                return ERROR;
            }
            else
            {
                return SUCCESS;
            }
        }

        // Allocate memory for new node
        freshNode = malloc(sizeof(node_t));
        if (freshNode == NULL)
        {
            syslog(LOG_ERR, "Failed to allocate memory for new client node");
            return ERROR;
        }

        // Populate node data
        freshNode->thread_data.pMutex = &lock;
        freshNode->thread_data.isThreadComplete = false;
        freshNode->thread_data.clientSocketFd = clientSocketFd;
        freshNode->thread_data.pClientAddr = (struct sockaddr_storage *)&clientInfo;

        // Create a new thread for the connection
        if (pthread_create(&(freshNode->thread_data.threadId), NULL, 
                           client_data_handler, &(freshNode->thread_data)) == ERROR)
        {
            syslog(LOG_ERR, "Thread creation for new client failed");
            free(freshNode);
            return ERROR;
        }

        // Insert node into list
        SLIST_INSERT_HEAD(&head, freshNode, nodes);
        freshNode = NULL;

        // Check for thread completion and join them
        SLIST_FOREACH(freshNode, &head, nodes)
        {
            if (freshNode->thread_data.isThreadComplete)
            {
                if (pthread_join(freshNode->thread_data.threadId, &threadRetVal) == ERROR)
                {
                    syslog(LOG_ERR, "Failed to join completed thread");
                    return ERROR;
                }
                if (threadRetVal == NULL)
                {
                    return ERROR;
                }
                syslog(LOG_INFO, "Successfully joined thread %ld", freshNode->thread_data.threadId);
            }
        }
    }
    return SUCCESS;
}

void* client_data_handler(void *thread_param)
{
    int result;
    int ioctl_check;
    // variables for receiving data
    ssize_t bytes_received = 0;
    char receive_buffer[BUF_LEN];

    // variables for sending data
    ssize_t bytes_sent = 0;
    char send_buffer[BUF_LEN];
    ssize_t bytes_from_file = 1;

    memset(receive_buffer, 0, BUF_LEN);
    memset(send_buffer, 0, BUF_LEN);

    ClientThreadData_t *thread_data_ptr = (ClientThreadData_t*)thread_param;
    inet_ntop(thread_data_ptr->pClientAddr->ss_family,
              get_in_addr((struct sockaddr *)&(thread_data_ptr->pClientAddr)),
              s, sizeof s);
    
    syslog(LOG_INFO, "New connection established: %s", s);
    syslog(LOG_INFO, "Thread %ld initialized", thread_data_ptr->threadId);

    // Initialize the condition variable
    void *newline_found = NULL;

    // Execute loop as long as the condition variable is NULL
    while (newline_found == NULL)
    {
        bytes_received = recv(thread_data_ptr->clientSocketFd, receive_buffer, BUF_LEN, 0);
        if (bytes_received == ERROR)
        {
            syslog(LOG_ERR, "Data reception unsuccessful");
            return NULL;
        }

        // Check if the received string starts with "AESDCHAR_IOCSEEKTO:"
        ioctl_check = strncmp(receive_buffer, ioctl_str, strlen(ioctl_str));
        
        if (ioctl_check == 0)
        {
            struct aesd_seekto aesd_seekto_data;
            sscanf(receive_buffer, "AESDCHAR_IOCSEEKTO:%d,%d", &aesd_seekto_data.write_cmd, &aesd_seekto_data.write_cmd_offset); 
            
            dataFileDescriptor = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);
            if(dataFileDescriptor == ERROR)
            {
                syslog(LOG_ERR,"Data file open failed");
                DEBUG_LOG("Application Failure\n");
                DEBUG_LOG("Check logs\n");
                return NULL;
            }
        
            if(ioctl(dataFileDescriptor, AESDCHAR_IOCSEEKTO, &aesd_seekto_data) != 0)
            {
                perror("ioctl failed");
                syslog(LOG_ERR,"ioctl failed");
            }
        }
        else
        {
            // Open the file in write mode
            dataFileDescriptor = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);
            if (ERROR == dataFileDescriptor)
            {
                perror("File open");
                syslog(LOG_ERR, "File Open");
            }

            // Lock mutex to protect file
            result = pthread_mutex_lock(thread_data_ptr->pMutex);
            if (result == ERROR)
            {
                syslog(LOG_ERR, "Failed to acquire mutex");
                return NULL;
            }

            // Write received data to the file
            result = write(dataFileDescriptor, receive_buffer, bytes_received);
            if (result == ERROR)
            {
                syslog(LOG_ERR, "Unsuccessful file write operation");
                return NULL;
            }

            // Unlock mutex
            result = pthread_mutex_unlock(thread_data_ptr->pMutex);
            if (result == ERROR)
            {
                syslog(LOG_ERR, "Failed to release mutex");
                return NULL;
            }

            // Close the file descriptor after writing
            close(dataFileDescriptor);
        }

        // Update the condition variable
        newline_found = memchr(receive_buffer, '\n', bytes_received);
    }

    if(ioctl_check != 0)
    {
        // Open the file in read-only mode
        dataFileDescriptor = open(DATA_FILE, O_RDONLY, 0444);
        if (ERROR == dataFileDescriptor)
        {
            syslog(LOG_ERR,"Data file open failed");
            DEBUG_LOG("Application Failure\n");
            DEBUG_LOG("Check logs\n");
            return NULL;
        }
    }

#ifndef USE_AESD_CHAR_DEVICE
    // Seek to the beginning of the file (if not using AESD_CHAR_DEVICE)
    off_t seek_result = lseek(dataFileDescriptor, 0, SEEK_SET);
    if (seek_result == ERROR)
    {
        syslog(LOG_ERR, "Failed to seek in the file");
        return NULL;
    }
#endif

    // Loop as long as bytes_from_file is greater than 0
    while (bytes_from_file > 0)
    {
        // Lock mutex to protect file
        result = pthread_mutex_lock(thread_data_ptr->pMutex);
        if (result == ERROR)
        {
            syslog(LOG_ERR, "Failed to acquire mutex");
            return NULL;
        }

        // Read data from the file
        bytes_from_file = read(dataFileDescriptor, send_buffer, BUF_LEN);
        if (bytes_from_file == ERROR)
        {
            syslog(LOG_ERR, "Failed to read file");
            return NULL;
        }

        // Unlock mutex
        result = pthread_mutex_unlock(thread_data_ptr->pMutex);
        if (result == ERROR)
        {
            syslog(LOG_ERR, "Failed to release mutex");
            return NULL;
        }

        // Send the read data back to the client
        bytes_sent = send(thread_data_ptr->clientSocketFd, send_buffer, bytes_from_file, 0);
        if (bytes_sent == ERROR)
        {  
            syslog(LOG_ERR, "Data transmission unsuccessful");
            return NULL;
        }
    }

    // Close the client socket and log the termination of the connection
    close(thread_data_ptr->clientSocketFd);
    syslog(LOG_INFO, "Terminated connection: %s", s);

    // Set the thread completion status to true
    thread_data_ptr->isThreadComplete = true;

    // Close the data file descriptor
    close(dataFileDescriptor);
    return thread_param;
}

#ifndef USE_AESD_CHAR_DEVICE

int setup_time_logging(void)
{
    // Initialize TS_data with mutex and time interval
    TS_data.pMutex = &lock;
    TS_data.timeIntervalSecs = 10;

    // Create and start the timestamp logging thread
    if(pthread_create(&(TS_data.threadId), NULL, 
                      log_timestamps, &(TS_data)) == ERROR)
    {
        syslog(LOG_ERR, "Failed to create the Timestamp Logging thread");
        return -1;
    }
    return 0;
}

void *log_timestamps(void *arg)
{
    // Cast thread parameter to appropriate type
    ThreadTimestampData_t *param_data = (ThreadTimestampData_t *)arg;

    // Local variables
    time_t curr_time;
    struct tm *local_time_info;
    char formatted_timestamp[TIMESTAMP_STRING_LENGTH];
    struct timespec time_spec;

    // Log that the thread has started
    syslog(LOG_INFO, "Time logging thread activated.");

    // Infinite loop to log timestamps
    while (1)
    {
        // Get current time in monotonic mode
        if (clock_gettime(CLOCK_MONOTONIC, &time_spec))
        {
            syslog(LOG_ERR, "Failed to get current time.");
            return NULL;
        }

        // Increment time by the interval
        time_spec.tv_sec += param_data->timeIntervalSecs;

        // Sleep for the time interval
        if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time_spec, NULL))
        {
            syslog(LOG_ERR, "Sleep operation unsuccessful.");
            return NULL;
        }

        // Get and format current time
        time(&curr_time);
        local_time_info = localtime(&curr_time);
        int length_of_timestamp = strftime(formatted_timestamp, sizeof(formatted_timestamp), "timestamp: %Y, %b %d, %H:%M:%S\n", local_time_info);

        // Lock mutex
        if (pthread_mutex_lock(param_data->pMutex) == ERROR)
        {
            syslog(LOG_ERR, "Failed to lock mutex.");
            return NULL;
        }

        // Write the timestamp to file
        if (write(dataFileDescriptor, formatted_timestamp, length_of_timestamp) == ERROR)
        {
            syslog(LOG_ERR, "Failed to write timestamp to file.");
            return NULL;
        }

        // Unlock mutex
        if (pthread_mutex_unlock(param_data->pMutex) == ERROR)
        {
            syslog(LOG_ERR, "Failed to unlock mutex.");
            return NULL;
        }
    }
    return NULL; // Return NULL for good measure, though we never actually get here
}
#endif