#include <arpa/inet.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG_ERROR(fmt)                                                         \
  (fprintf(stderr, "[ERROR] %s:%d(): " fmt "\n", __FILE__, __LINE__))

// max request size
const size_t MAX_MSG_SIZE = 4096;

/**
 * @brief Reads n bytes from the socket
 *
 * @param connfd the file descriptor of the socket
 * @param buf the buffer to store the data
 * @param n the number of bytes to read
 * @return int32_t the number of bytes read
 */
static int32_t read_all(int connfd, char *buf, size_t n) {
  size_t bytesRead = 0;
  while (n > 0) {
    int rv = read(connfd, buf, n);
    if (rv < 0) {
      perror("read() error");
      return -1;
    } else if (rv == 0) {
      break;
    } else {
      assert((size_t)rv <= n);
      n -= (size_t)rv;
      bytesRead += (size_t)rv;
      buf += rv;
    }
  }
  return bytesRead;
}

/**
 * @brief Writes n bytes to the socket
 *
 * @param connfd the file descriptor of the socket
 * @param buf the buffer to write to the socket
 * @param n the number of bytes to write
 * @return int32_t 0 on success, -1 on failure
 */
static int32_t write_all(int connfd, char *buf, size_t n) {
  while (n > 0) {
    int rv = write(connfd, buf, n);
    if (rv < 0) {
      perror("write() error");
      return -1;
    } else {
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

/**
 * @brief Reads a request from the client and sends a response
 *
 * @param connfd the file descriptor of the socket
 * @return int32_t 0 on success, -1 on failure
 */
static int32_t one_request(int connfd) {
  // 4 bytes length header
  char rbuf[4 + MAX_MSG_SIZE + 1] = {};

  // read the length of the message
  int32_t rv = read_all(connfd, rbuf, 4);
  if (rv < 0) {
    return -1;
  } else if (rv != 4) {
    if (rv == 0) {
      printf("Connection closed by the client.\n");
    } else {
      LOG_ERROR("Premature EOF reached.");
    }
    return -1;
  }

  // convert the length to host byte order
  int32_t length = 0;
  memcpy(&length, rbuf, 4);
  length = ntohl(length);

  // read the message
  rv = read_all(connfd, &rbuf[4], length);
  if (rv < 0) {
    return -1;
  } else if (rv != (size_t)length) {
    LOG_ERROR("Premature EOF reached.");
    return 0;
  }
  rbuf[4 + length] = '\0';
  printf("The client says: %s\n", &rbuf[4]);

  char response[] = "Hi Client!";
  char wbuf[4 + strlen(response)];
  length = (uint32_t)strlen(response);
  if (length > MAX_MSG_SIZE) {
    printf("Payload size too high! Please limit the message size to 4096 "
           "bytes!\n");
    return -1;
  }

  // make sure to send the data in network byte order
  if (!isLittleEndian()) {
    memcpy(&wbuf, &length, 4);
  } else {
    int32_t networkLength = htonl(length);
    memcpy(&wbuf, &networkLength, 4);
  }
  memcpy(&wbuf[4], response, length);
  return write_all(connfd, wbuf, 4 + length);
}

int main() {

  int fd = socket(AF_INET, SOCK_STREAM, 0);

  if (fd < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  int val = 1;

  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(1234);
  server_addr.sin_addr.s_addr = htonl(0);

  int rv = bind(fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));

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
    struct sockaddr client_addr = {};
    socklen_t socklen = sizeof(client_addr);

    int32_t connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);

    if (connfd < 0) {
      perror("Can't eastablish the connection");
      exit(EXIT_FAILURE);
    }

    // we can only serve one client connection at any time
    while (1) {
      int32_t err = one_request(connfd);
      if (err < 0)
        break;
    }
    close(connfd);
  }
  return 0;
}
