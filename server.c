#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <pthread.h>

#define SERVER_IP   "127.0.0.1" //Answer to Section 2: Communication will be carried out over the local loopback address: 127.0.0.1 (localhost).
#define SERVER_PORT 5555 //Since the server’s port number was not specified, I chose a port for the implementation.
#define BUF_SIZE    4096 //Resilience and Efficiency (Operations Partial): Answer to Section 3 -The server must maintain an internal buffer with a size of 4096 bytes.

static int g_connected_clients = 0; //Global counter of currently connected clients.
static pthread_mutex_t g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;  //Mutex protecting the critical section.

/*print a system error (perror) and terminate the program immediately on a critical failure.*/
static void die(const char *msg) {
    perror(msg); //Prints the system error.
    exit(EXIT_FAILURE); //Terminates the program immediately.
}
/*Send all len bytes through the socket, even if send() transmits only part of the data each time.*/
static ssize_t send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf; //Pointer to outgoing buffer.
    size_t total = 0; //Tracks bytes already sent.

    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0);// Attempt to send remaining bytes.
        if (n < 0) {
            if (errno == EINTR) continue;// Retry if interrupted by a signal.
            return -1; // Return error on failure.
        }
        if (n == 0) break;  // Prevent a potential infinite loop.
        total += (size_t)n;  // Update number of bytes sent.
    }
    return (ssize_t)total; // Return total bytes sent.
}
/*Receives one chunk of data (up to cap bytes) from the client, retrying if recv() is interrupted (EINTR).*/
static ssize_t recv_some(int fd, char *buf, size_t cap) {
    while (1) {
        ssize_t n = recv(fd, buf, cap, 0);  // Attempt to receive up to cap bytes.
        if (n < 0) {
            if (errno == EINTR) continue;  // Retry if interrupted by a signal
            return -1;  // Return error on failure.
        }
        return n;// Return bytes received (0 means connection closed).
    }
}
/*convert lowercase letters to uppercase in the buffer (in-place) for n bytes.*/
static void to_uppercase(char *buf, ssize_t n) {
    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];  // Read current byte safely as unsigned char.
        if (islower(c)) buf[i] = (char)toupper(c);  // Check if it is a lowercase letter &  Convert to uppercase in place.
    }
}
/*A struct for passing arguments to a thread.*/
typedef struct {
    int client_fd;
} thread_arg_t;
/*Safely increment the connected-clients counter using a mutex and print how many are currently connected.*/
static void inc_clients(void) {
    pthread_mutex_lock(&g_clients_mutex); // Lock mutex to update shared counter safely.
    g_connected_clients++;// Increment number of connected clients.
    int now = g_connected_clients;  // Capture current count for printing.
    pthread_mutex_unlock(&g_clients_mutex);  // Unlock mutex.
    fprintf(stderr, "Client connected. Now: %d\n", now);  // Print current number of clients.
}
/*Safely decrement the connected-clients counter using a mutex and print how many remain connected.*/
static void dec_clients(void) {
    pthread_mutex_lock(&g_clients_mutex); // Lock mutex to update shared counter safely.
    g_connected_clients--;// Decrement number of connected clients.
    int now = g_connected_clients; // Capture current count for printing.
    pthread_mutex_unlock(&g_clients_mutex); // Unlock mutex.
    fprintf(stderr, "Client disconnected. Now: %d\n", now); // Print current number of clients.
}
/*A thread that handles a single client: receives data, converts it to uppercase, sends it back, and repeats until the client disconnects.*/
static void *client_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg; // Cast the generic argument to the thread struct.
    int cfd = targ->client_fd; // Extract the client socket file descriptor.
    free(targ); // Free the dynamically allocated argument.

    inc_clients(); // Safely increment connected-clients counter.

    char buf[BUF_SIZE]; // Buffer for receiving client data.

    while (1) {
    ssize_t n = recv_some(cfd, buf, sizeof(buf)); // Receive a chunk from the client.

    if (n < 0) {
        perror("recv");   // Print error only on failure.
        break;
    }

    if (n == 0) break;     // Client disconnected.

    to_uppercase(buf, n);  // Convert received data to uppercase.

    if (send_all(cfd, buf, (size_t)n) < 0) {
        perror("send");    // Print error on send failure.
        break;
    }
}


    close(cfd);   // Close the client socket.
    dec_clients();  // Safely decrement connected-clients counter.
    return NULL; // Terminate the thread.
}
/*Open a server socket, perform bind and listen, then in a loop accept new clients and create a thread for each one.*/
int main(void) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);  // Create an IPv4 TCP server socket.
    if (sfd < 0) die("socket");   // Abort on socket creation failure.

    int opt = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)    // Enable address reuse or abort on error.
    {
        die("setsockopt");
    }

    struct sockaddr_in addr; 
    memset(&addr, 0, sizeof(addr)); // Zero-initialize the address struct.
    addr.sin_family = AF_INET; // Specify IPv4 address family.
    addr.sin_port = htons(SERVER_PORT);  // Set port (network byte order).
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);  // Set server IP address.

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) die("bind"); // Bind socket to address.
    if (listen(sfd, 16) < 0) die("listen");// Start listening with backlog size 16.


    fprintf(stderr, "Server listening on %s:%d\n", SERVER_IP, SERVER_PORT); // Log server status.

    while (1) {
        struct sockaddr_in caddr;// Structure to hold the client’s IPv4 address.
        socklen_t clen = sizeof(caddr);  // Length of the address structure for accept().
        int cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);// Accept a new client connection.
        if (cfd < 0) {
            if (errno == EINTR) continue; // Retry if interrupted by a signal.
            die("accept");   // Abort on critical accept error.
        }

        thread_arg_t *targ = (thread_arg_t *)malloc(sizeof(thread_arg_t));// Allocate thread argument struct.
        if (!targ) {
            close(cfd);// Close client socket on allocation failure.
            continue;
        }
        targ->client_fd = cfd;  // Store client socket descriptor.

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, targ) != 0) {
            perror("pthread_create"); // Report thread creation error.
            close(cfd);// Close the client socket.
            free(targ);  // Free the allocated thread argument struct.
            continue;
        }
        pthread_detach(tid);  // Detach thread for automatic cleanup.
    }

    close(sfd); // Close server socket (unreachable here).
    return 0;  // Normal program exit.
}
