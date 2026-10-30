#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <pthread.h>
#include <time.h>
#include <sys/queue.h>


#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

static volatile sig_atomic_t stop_requested = 0;
/* Protect access to the shared socket data file. */
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Information about each connected client thread. */
struct client_thread {
    pthread_t thread_id;
    int client_fd;
    int completed;
    char client_ip[INET_ADDRSTRLEN];
    SLIST_ENTRY(client_thread) entries;
};

/* Singly linked list of client threads. */
SLIST_HEAD(client_thread_list, client_thread);

static struct client_thread_list client_threads =
    SLIST_HEAD_INITIALIZER(client_threads);


static void signal_handler(int signo)
{
    (void)signo;
    stop_requested = 1;
}

/* Send all requested bytes, handling partial sends. */
static int send_all(int fd, const char *buffer, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t result = send(fd, buffer + sent, length - sent, 0);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (result == 0) {
            return -1;
        }

        sent += (size_t)result;
    }

    return 0;
}

/* Stream the file to the client without loading it all into RAM. */
static int send_file(int client_fd)
{
    FILE *file = fopen(DATA_FILE, "rb");

    if (file == NULL) {
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
        return -1;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    int result = 0;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send_all(client_fd, buffer, bytes_read) != 0) {
            syslog(LOG_ERR, "Failed to send file: %s", strerror(errno));
            result = -1;
            break;
        }
    }

    if (ferror(file)) {
        syslog(LOG_ERR, "Failed to read data file");
        result = -1;
    }

    fclose(file);
    return result;
}

/* Append one complete newline-terminated packet. */
static int append_packet(const char *packet, size_t length)
{
    FILE *file = fopen(DATA_FILE, "ab");

    if (file == NULL) {
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
        return -1;
    }

    int result = 0;

    if (fwrite(packet, 1, length, file) != length) {
        syslog(LOG_ERR, "Failed to write data file");
        result = -1;
    }

    if (fclose(file) != 0) {
        syslog(LOG_ERR, "Failed to close data file: %s", strerror(errno));
        result = -1;
    }

    return result;
}

/* Receive bytes and process each complete newline-terminated packet. */
static void handle_client(int client_fd)
{
    char buffer[BUFFER_SIZE];
    char *packet = NULL;
    size_t packet_length = 0;
    size_t packet_capacity = 0;

    while (!stop_requested) {
        ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);

        if (received == 0) {
            break;
        }

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            syslog(LOG_ERR, "Receive failed: %s", strerror(errno));
            break;
        }

        for (ssize_t i = 0; i < received; i++) {
            if (packet_length + 1 > packet_capacity) {
                size_t new_capacity =
                    packet_capacity == 0 ? BUFFER_SIZE : packet_capacity * 2;

                char *new_packet = realloc(packet, new_capacity);

                if (new_packet == NULL) {
                    syslog(LOG_ERR, "Packet allocation failed");
                    free(packet);
                    return;
                }

                packet = new_packet;
                packet_capacity = new_capacity;
            }

            packet[packet_length++] = buffer[i];

	if (buffer[i] == '\n') {
 	   pthread_mutex_lock(&file_mutex);

    	   int result = append_packet(packet, packet_length);

           if (result == 0) {
               result = send_file(client_fd);
           }

           pthread_mutex_unlock(&file_mutex);

           if (result != 0) {
               free(packet);
               return;
           }

           packet_length = 0;
          }


        }
    }

    free(packet);
}

/* Thread function for handling one client connection. */
static void *client_thread_func(void *arg)
{
    struct client_thread *client = (struct client_thread *)arg;

    syslog(LOG_INFO, "Client thread started for %s",
           client->client_ip);

    /* Process data received from this client. */
    handle_client(client->client_fd);

    /* Close the socket safely and mark it as unavailable. */
    pthread_mutex_lock(&file_mutex);
    close(client->client_fd);
    client->client_fd = -1;
    pthread_mutex_unlock(&file_mutex);
    
    syslog(LOG_INFO, "Client thread finished for %s",
    client->client_ip);

    /* Mark this thread as completed safely. */
    pthread_mutex_lock(&file_mutex);
    client->completed = 1;
    pthread_mutex_unlock(&file_mutex);
    
    
    return NULL;
}

/* Join completed client threads and release their memory. */
static void cleanup_completed_threads(void)
{
    struct client_thread *client = SLIST_FIRST(&client_threads);

    while (client != NULL) {
        struct client_thread *next = SLIST_NEXT(client, entries);

        pthread_mutex_lock(&file_mutex);
        int completed = client->completed;
        pthread_mutex_unlock(&file_mutex);

        if (completed) {
            pthread_join(client->thread_id, NULL);

            SLIST_REMOVE(&client_threads, client,
                         client_thread, entries);

            free(client);
        }

        client = next;
    }
}


/* Stop all client connections and join their threads. */
static void shutdown_client_threads(void)
{
    struct client_thread *client;

    /* Unblock any client threads waiting in recv(). */
    SLIST_FOREACH(client, &client_threads, entries) {
    pthread_mutex_lock(&file_mutex);

    if (client->client_fd >= 0) {
    shutdown(client->client_fd, SHUT_RDWR);
    }

    pthread_mutex_unlock(&file_mutex);

    }

    /* Wait for every thread and free its resources. */
    while (!SLIST_EMPTY(&client_threads)) {
        client = SLIST_FIRST(&client_threads);

        pthread_join(client->thread_id, NULL);

        SLIST_REMOVE_HEAD(&client_threads, entries);

        free(client);
    }
}

/* Append a timestamp to the data file every 10 seconds. */
static void *timestamp_thread_func(void *arg)
{
    (void)arg;

    while (!stop_requested) {

        /* Sleep for 10 seconds, checking for shutdown. */
        for (int i = 0; i < 10 && !stop_requested; i++) {
            sleep(1);
        }

        if (stop_requested) {
            break;
        }

        time_t now = time(NULL);
        struct tm time_info;
        char timestamp[128];

        if (localtime_r(&now, &time_info) == NULL) {
            syslog(LOG_ERR, "Failed to obtain local time");
            continue;
        }

        size_t length = strftime(
            timestamp,
            sizeof(timestamp),
            "timestamp:%a, %d %b %Y %T %z\n",
            &time_info
        );

        if (length == 0) {
            syslog(LOG_ERR, "Failed to format timestamp");
            continue;
        }

        /* Protect the shared file from concurrent writes. */
        pthread_mutex_lock(&file_mutex);

        if (append_packet(timestamp, length) != 0) {
            syslog(LOG_ERR, "Failed to append timestamp");
        }

        pthread_mutex_unlock(&file_mutex);
    }

    return NULL;
}


int main(int argc, char *argv[])
{
    int server_fd;
    struct sockaddr_in server_addr;
    int daemon_mode = 0;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        return -1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        syslog(LOG_ERR, "Failed to install signal handlers");
        closelog();
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    int opt = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    if (listen(server_fd, 5) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

        /* Run as a daemon if -d was specified.
     * The socket has already been bound to port 9000.
     */
    if (daemon_mode) {
        pid_t pid = fork();

        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
            close(server_fd);
            closelog();
            return -1;
        }

        if (pid > 0) {
            /* Parent exits; child continues as the server. */
            close(server_fd);
            closelog();
            return 0;
        }

        /* Create a new session for the daemon. */
        if (setsid() == -1) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            close(server_fd);
            closelog();
            return -1;
        }

        /* Disconnect the daemon from the terminal. */
        int devnull = open("/dev/null", O_RDWR);

        if (devnull == -1) {
            syslog(LOG_ERR, "Failed to open /dev/null: %s",
                   strerror(errno));
            close(server_fd);
            closelog();
            return -1;
        }

        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);

        if (devnull > STDERR_FILENO) {
            close(devnull);
        }
    }	
    
    /* Start the timestamp thread after daemonization. */
    pthread_t timestamp_thread;

    int timer_error = pthread_create(
    	&timestamp_thread,
    	NULL,
    	timestamp_thread_func,
    	NULL
    );

    if (timer_error != 0) {
    	syslog(LOG_ERR, "Failed to create timestamp thread: %s",
           strerror(timer_error));
    	close(server_fd);
    	closelog();
    	return -1;
    }


    printf("AESD socket server listening on port %d\n", PORT);
    fflush(stdout);

	while (!stop_requested) {
    cleanup_completed_threads();

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(server_fd, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int ready = select(server_fd + 1, &read_fds,
                       NULL, NULL, &timeout);

    if (stop_requested) {
        break;
    }

    if (ready == 0) {
        continue;
    }

    if (ready < 0) {
        if (errno == EINTR) {
            continue;
        }

        syslog(LOG_ERR, "select failed: %s", strerror(errno));
        break;
    }

    struct sockaddr_in client_addr;

        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }

            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];

        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, sizeof(client_ip)) == NULL) {
            syslog(LOG_ERR, "Failed to convert client IP");
            close(client_fd);
            continue;
        }


	syslog(LOG_INFO, "Accepted connection from %s", client_ip);

/* Allocate information for this client thread. */
struct client_thread *client = calloc(1, sizeof(*client));

if (client == NULL) {
    syslog(LOG_ERR, "Failed to allocate client thread");
    close(client_fd);
    continue;
}

client->client_fd = client_fd;

strncpy(client->client_ip, client_ip,
        sizeof(client->client_ip) - 1);

/* Create a separate thread for this client. */
int error = pthread_create(
    &client->thread_id,
    NULL,
    client_thread_func,
    client
);

if (error != 0) {
    syslog(LOG_ERR, "pthread_create failed: %s",
           strerror(error));
    close(client_fd);
    free(client);
    continue;
}

/* Add the client thread to our linked list. */
SLIST_INSERT_HEAD(&client_threads, client, entries);
    }

    syslog(LOG_INFO, "Caught signal, exiting");

    close(server_fd);
    
    /* Wait for all client threads before deleting the data file. */
    shutdown_client_threads();

    if (unlink(DATA_FILE) == -1 && errno != ENOENT) {
        syslog(LOG_ERR, "Failed to delete data file: %s",
               strerror(errno));
    }

    closelog();

    return 0;
}
