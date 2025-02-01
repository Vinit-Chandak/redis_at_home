#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOG_ERROR(fmt)                                                         \
  (fprintf(stderr, "[ERROR] %s:%d(): " fmt "\n", __FILE__, __LINE__))

// max request size
const size_t MAX_MSG_SIZE = 4096;

static bool isLittleEndian() {
  uint32_t test = 1;
  return (*(unsigned char *)&test) == 1;
}

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

/**
 * @brief Reads a request from the client and sends a response
 *
 * @param connfd the file descriptor of the socket
 * @return int32_t 0 on success, -1 on failure
 */
int32_t query(int connfd, const char *message) {
  int32_t length = (int32_t)strlen(message);
  if (length > MAX_MSG_SIZE) {
    printf(
        "Payload size too high! Please limit the message size to 4096 bytes!");
    return -1;
  }
  char wbuf[4 + length];

  // we need to send the data in network byte order
  if (!isLittleEndian()) {
    memcpy(&wbuf, &length, 4);
  } else {
    int32_t networkLength = htonl(length);
    memcpy(&wbuf, &networkLength, 4);
  }
  memcpy(&wbuf[4], message, length);
  int32_t rv = write_all(connfd, wbuf, 4 + length);
  if (rv < 0) {
    return -1;
  }

  char rbuf[4 + 4096 + 1] = {};
  rv = read_all(connfd, rbuf, 4);
  if (rv < 0) {
    return -1;
  } else if (rv != 4) {
    if (rv == 0) {
      printf("Connection closed by the server.\n");
    } else {
      LOG_ERROR("Premature EOF reached.");
    }
    return -1;
  }

  length = 0;
  memcpy(&length, rbuf, 4);
  if (isLittleEndian()) {
    length = ntohl(length);
  }
  rv = read_all(connfd, &rbuf[4], length);
  if (rv < 0) {
    return -1;
  } else if (rv != (size_t)length) {
    LOG_ERROR("Premature EOF reached.");
    return 0;
  }
  rbuf[4 + length] = '\0';
  printf("The server says: %s\n", &rbuf[4]);
  return 0;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("Error creating a socket");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (rv) {
    perror("Could not connect to the client");
    exit(EXIT_FAILURE);
  }

  rv = query(fd, "hello server!");
  if (rv) {
    close(fd);
    return -1;
  }
  rv = query(fd, "What is your name?");
  if (rv) {
    close(fd);
    return -1;
  }
  rv = query(fd, "What is your name?");
  if (rv) {
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}
