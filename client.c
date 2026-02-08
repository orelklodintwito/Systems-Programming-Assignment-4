#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <pthread.h>
/*Macros*/
#define SERVER_IP   "127.0.0.1" //Answer to Section 2: Communication will be carried out over the local loopback address: 127.0.0.1 (localhost).
#define SERVER_PORT 5555 //Since the server’s port number was not specified, I chose a port for the implementation.
#define BUF_SIZE    4096 //Response buffer size (also aligns with the exercise's 4096-byte buffer guideline).
#define NUM_CLIENT_THREADS 5 //Answer to Section 4 – Multi-Client Simulation: The client program will be multithreaded and must create at least 5 concurrent threads simultaneously.

/* Attempts to send the entire message buffer. Returns the number of bytes sent, or -1 if an error occurs.*/
static ssize_t send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf; //p is a pointer to the buffer as a byte array.
    size_t total = 0; //total: how many bytes have been sent so far.

    /*send() is not guaranteed to send everything in a single call, so we use a loop.*/
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0); //Send the remaining bytes.
        if (n < 0) // If send() fails, retry on EINTR (interrupted by a signal); otherwise, return -1.
        {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break; //No progress (rare). Break to avoid an infinite loop.
        total += (size_t)n;
    }
    return (ssize_t)total; //Update the number of bytes sent and return.
}
/*Attempts to receive exactly len bytes.*/
static ssize_t recv_all_exact(int fd, void *buf, size_t len) 
{
    char *p = (char *)buf;
    size_t total = 0;

    while (total < len)  //recv() is not guaranteed to receive everything in a single call, so we use a loop.
    {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n < 0) //If it fails, retry on EINTR (interrupted by a signal); otherwise, return -1.
        {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break; //Peer closed the connection (EOF).
        total += (size_t)n; //Updates how many bytes were received so far.
    }
    return (ssize_t)total; //Updates the number of bytes received and returns it.
}

/*A struct for passing arguments to a thread.*/
typedef struct {
    int index;
    const char *message;
} client_arg_t;

/*The client thread function.*/
static void *client_thread(void *arg) {
    client_arg_t *carg = (client_arg_t *)arg; //Casts `arg` to the current struct to access `index` and `message`.

    int fd = socket(AF_INET, SOCK_STREAM, 0); //Creates an IPv4 TCP socket.
    if (fd < 0) //Checks for socket creation failure.
    {
        perror("socket");
        return NULL;
    }

    struct sockaddr_in addr; //Defines an IPv4 server address variable.
    memset(&addr, 0, sizeof(addr)); //Zero-initializes the struct to avoid garbage values in memory.
    addr.sin_family = AF_INET; //Sets the address family to IPv4.
    addr.sin_port = htons(SERVER_PORT); //Sets the port in the address (converted to network byte order).
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);//Converts "127.0.0.1" to a binary address and stores it in addr.

    /*Attempts to connect to the server using the socket.*/
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); //Prints the system error.
        close(fd); //Closes the socket.
        return NULL; //Terminates the thread.
    }

    size_t msg_len = strlen(carg->message); //Computes the message length to send (excluding the \0).

    /*Sends the entire message (using a loop to handle partial sends).*/
    if (send_all(fd, carg->message, msg_len) < 0) {
        perror("send"); //Prints the system error.
        close(fd); //Closes the socket.
        return NULL; //Terminates the thread.
    }

    char resp[BUF_SIZE];
    size_t need = msg_len; //Assumes server responds with the same length as sent (processed echo).
    if (need >= sizeof(resp)) need = sizeof(resp) - 1;

    ssize_t got = recv_all_exact(fd, resp, need);
    if (got < 0)
     {
        perror("recv"); //Prints the system error.
        close(fd); //Closes the socket.
        return NULL; //Terminates the thread.
    }

    resp[got] = '\0'; //Adds string terminator so resp can be printed safely as a C-string.



    printf("[Client #%d] sent: \"%s\" | got: \"%s\"\n",  carg->index, carg->message, resp); //Prints the thread’s result to the screen.


    close(fd); //Closes the socket.
    return NULL; //Terminates the thread.
}

int main(void) {
    pthread_t tids[NUM_CLIENT_THREADS]; //An array to store each thread’s ID (so we can `join` them later).
    client_arg_t args[NUM_CLIENT_THREADS]; //An array of arguments for each thread (client index + message to send).

    const char *msgs[NUM_CLIENT_THREADS] = {
        "hello server",
        "shnkar systems programming",
        "multi threaded client",
        "echo test 123",
        "good luck!"
    }; //An array of constant strings, one for each thread.

    /*A loop that runs 5 times to create 5 threads.*/
    for (int i = 0; i < NUM_CLIENT_THREADS; i++) {
        args[i].index = i + 1; //Stores a client sequence number (1..5) to identify it in the output.
        args[i].message = msgs[i]; //Assigns each client its corresponding message from the msgs array.
       
        /*Creates a new thread: stores its ID in `tids[i]`, runs `client_thread`, and passes `&args[i]` as the argument.*/
        if (pthread_create(&tids[i], NULL, client_thread, &args[i]) != 0) 
        {
            perror("pthread_create"); //Prints the system error.
        }
    }
/*Iterates over all threads to wait for them all to finish.*/
    for (int i = 0; i < NUM_CLIENT_THREADS; i++) {
        pthread_join(tids[i], NULL); //Waits for thread `i` to finish so the program doesn’t exit before it closes the socket and prints its output.
    }

    return 0;
}
