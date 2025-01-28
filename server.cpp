#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>

// max request size
const size_t MAX_MSG_SIZE = 4096;

static int32_t read_all(int connfd, char *buf, size_t n) {
    while (n > 0) {
        int rv = read(connfd, buf, n);
        if (rv < 0) {
            perror("read() error");
            return -1;
        }
        else if (rv == 0) {
            printf("EOF reached.");
            return -1;
        }
        else {
            // macro that checks if the conditions is true or false
            // if false, throws an error and terminates the program
            assert((size_t)rv <= n);
            n -= (size_t)rv;
            buf += rv;
        }
    }
    return 0;
}

static int32_t write_all(int connfd, char *buf, size_t n) {
    while (n > 0) {
        int rv = write(connfd, buf, n);
        if (rv < 0) {
            perror("write() error");
            return -1;
        }
        else {
            assert((size_t)rv <= n);
            n -= (size_t)rv;
            buf += rv;
        }
    }
    return 0;
}

static bool isLittleEndian() {
    uint32_t test = 1;
    return (*((unsigned char *)&test) == 1);
}

static int32_t one_request(int connfd) {
    // 4 bytes length header
    char rbuf[4 + MAX_MSG_SIZE + 1] = {};

    int32_t rv = read_all(connfd, rbuf, 4);
    if (rv) {
        perror("Exiting due to read() error.");
        exit(EXIT_FAILURE);
    }

    // we assume that the bytes are transmitted in network order format
    // i.e big endian, so we check if the system is little endian and
    // store accordingly
    int32_t length = 0;
    if (isLittleEndian()) {
        length = (rbuf[3] << 24 | rbuf[2] << 16 | rbuf[1] << 8 | rbuf[0]);
    }
    else {
        memcpy(&length, rbuf, 4);
    }

    rv = read_all(connfd, &rbuf[4], length);
    if (rv) {
        perror("Exiting due to read() error.");
        exit(EXIT_FAILURE);
    }
    rbuf[4 + length] = '\0';
    printf("The server says: %sn", &rbuf[4]);

    char response[] = "Hi Server!";
    char wbuf[4 + strlen(response)];
    length = (uint32_t)strlen(response);
    memcpy(&wbuf, &length, 4);
    memcpy(&wbuf, &response, length);
    return write_all(connfd, wbuf, 4 + length);
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

        // we can only serve one client connection at any time
        // until we have (event loops)?
        while (1) {
            int32_t err = one_request(connfd);
        }
        close(connfd);
    }

}
