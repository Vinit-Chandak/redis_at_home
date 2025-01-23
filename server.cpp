/*
* STEPS TO START A SERVER
    1. Create a socket
    2. Set it's options
    3. Bind it to a address(this is a tuple of port and IP address)
    4. Start listening
    5. Accept a connection, do something with it and close the connection.
*/


#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>

// handles communication to the client with the connected socket
// static limits the visibility of the function to the file
// in which it is defined
static void do_something(int connfd) {
    char rbuf[64] = {};

    // ssize_t - signed type to store the number of bytes read
    // reads upto sizeof(rbuf) - 1 bytes to leave room for a null terminator
    // read() returns the number of bytes read
    // if (n == 0), it means that the client closed the connection
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        perror("read() error");
        return;    }
    // add a null termination
    rbuf[n] = '\0';

    printf("The client says: %s\n", rbuf);
    ssize_t rv = 0;
    if (strcmp(rbuf, "hello") == 0 || strcmp(rbuf, "Hello") == 0) {
        char wbuf[] = "world";
        rv = write(connfd, wbuf, sizeof(wbuf));
    }
    else {
        char wbuf[] = "what are you talking about?";
        rv = write(connfd, wbuf, sizeof(wbuf));
    }
    if (rv < 0) {
        perror("write() error");
    }
    return;
}

int main() {

    // create a socket and assign it a file descriptor
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    // check if the socket was created successfully
    if (fd < 0) {

        // print an error message and exit the program
        // perror prints the error message to stderr,
        // given string followed by a colon and then
        // the error message corresponding to the errorno
        // variable, which is a global variable that stores the
        // error code of the last system call that failed
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int val = 1;

    // Used to set options for a socket
    // SO_REUSEADDR, allows the socket to bind to a recently closed
    // port while is is still in TIME_WAIT state, the port goes
    // into this state to ensure that all the packets are taken
    // care of and any late packets are discarded
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // structure used to represent IPv4 socket address
    // has 3 fields
    // sin_family: address family, IPv4 or IPv6
    // sin_port: the port number to bind the socket to
    // sin_addr.s_addr: the IP to bind the socket to
    struct sockaddr_in server_addr = {};

    server_addr.sin_family = AF_INET;

    // we need the port number in network byte order(always big endian)
    // so ntohs converts it from host to network byte order
    server_addr.sin_port = ntohs(1234);

    // binds the socket to wildcard address 0.0.0.0
    // tells the OS to bind the socket to all the available
    // interfaces on the machine, this allows the server to accept
    // connections from any interface
    server_addr.sin_addr.s_addr = ntohl(0);

    int rv = bind(fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));

    // if binding fails for some reason
    if (rv < 0) {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    // 5 is the length of the waiting queue
    rv = listen(fd, 5);
    if (rv < 0) {
        perror("Can't listen");
        exit(EXIT_FAILURE);
    }

    while (1) {
        // accept a connection
        struct sockaddr client_addr = {};
        socklen_t socklen = sizeof(client_addr);

        // fd for the connection
        int32_t connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);

        if (connfd < 0) {
            perror("Can't eastablish the connection");
            exit(EXIT_FAILURE);
        }

        do_something(connfd);
        close(connfd);
    }

}
