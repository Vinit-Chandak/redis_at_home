/*
CLIENT:
    1. Create a socket
    2. Connect to the server
    3. Send a message to the server
    4. Read the response from the server
*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("Error creating a socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);

    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) {
        perror("Could not connect to the client");
        exit(EXIT_FAILURE);
    }

    char msg[] = "nah";
    ssize_t n = write(fd, msg, sizeof(msg));
    if (n < 0) {
        perror("write() error");
    }

    char rbuf[64] = {};
    n = read(fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        perror("read() error");
        return 1;
    }
    printf("The server says: %s\n", rbuf);
    close(fd);
}
