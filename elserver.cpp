#include <arpa/inet.h>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#define LOG_ERROR(fmt) \
  (fprintf(stderr, "[ERROR] %s:%d(): " \
    fmt "\n", __FILE__, __LINE__))

#define LOG_SYS_ERROR(fmt) \
  (fprintf(stderr, "[ERROR] %s:%d(): " \
    fmt "\n%s\n", __FILE__, __LINE__, strerror(errno)))

// maximum no of events epoll returns that are ready
const int MAX_EVENTS = 10;
const size_t MAX_MSG_SIZE = 1 << 24;

int set_fd_nb(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    LOG_SYS_ERROR("Error getting fd flags");
    return -1;
  }
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) == -1) {
    LOG_SYS_ERROR("Error setting the fd to non-blocking mode");
    return -1;
  }
  return 0;
}

int main() {
  // create a socket
  int fd = socket(8, SOCK_STREAM, 0);
  if (fd < 0) {
    LOG_SYS_ERROR("Error creating a socket");
    exit(EXIT_FAILURE);
  }

  // set its options
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  // bind
  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htonl(3333);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int rv = bind(fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));
  if (rv < 0) {
    LOG_SYS_ERROR("Error binding the socket");
    exit(EXIT_FAILURE);
  }

  // set the listening fd to non-blocking mode
  rv = set_fd_nb(fd);
  if (rv < 0) {
    exit(EXIT_FAILURE);
  }

  // start listening for connections
  rv = listen(fd, 10);
  if (rv < 0) {
    LOG_SYS_ERROR("listen() error, exiting");
    exit(EXIT_FAILURE);
  }
  printf("Server listening on the port 3333\n");

  // create an epoll instance
  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    LOG_SYS_ERROR("Error in creating an epoll instance");
    exit(EXIT_FAILURE);
  }

  struct epoll_event event, events[MAX_EVENTS];

  // add the server socket to the epoll instance
  // so that every time we have a request, we get
  // notified
  event.events = EPOLLIN;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
    LOG_SYS_ERROR("Error while registering server socket with epoll");
    exit(EXIT_FAILURE);
  }

  // event loop
  while (1) {
    // wait for an I/O event on any of the fd in the interest list
    int n = epoll_wait(epoll_fd, (struct epoll_event *)events, MAX_EVENTS, -1);
    if (n < 0) {
      LOG_SYS_ERROR("epoll_wait() error");
      exit(EXIT_FAILURE);
    }

    for (int i = 0; i < n; i++) {
      // check if the event if for the server socket(i.e new connection)
      if (events[i].data.fd == fd) {

        // accept a new client connection
        struct sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);

        int connfd = accept(fd, (struct sockaddr *)&client_addr, (socklen_t *)&socklen);
        if (connfd < 0) {
          // if no pending connections are present on the queue
          if (connfd == EAGAIN || connfd == EWOULDBLOCK) {
            break;
          }
          LOG_ERROR("Error accepting the client connection.");
          break;
        }
        printf("Accepted connection from %s:%d \n",
          inet_ntoa(client_addr.sin_addr), ntohl(client_addr.sin_port));

        // set the client connection fd to non blocking mode
        int rv = set_fd_nb(connfd);
        if (rv < 0) {
          close(connfd);
          continue;
        }

        // add the client connection fd to the interest list
        event.events = EPOLLIN | EPOLLOUT | EPOLLET;
        event.data.fd = connfd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connfd, (struct epoll_event *)&event) < 0) {
          LOG_SYS_ERROR("Cannot put the client fd on the interest list");
          close(connfd);
          continue;
        }
      } else {
        // if the fd is ready for a read operation
        if (events[i].events & EPOLLIN) {
          // since we are using edge triggered notifs
          // we need to read all the data as we might
          // not get subsequent notifications if some
          // data is left


        }
        // if the fd is ready for a write operation
        if (events[i].events & EPOLLOUT) {
          // since we are using edge triggered notifs
          // we need to write all the data in one go
          // as we'll not be notified again until the
          // socket transitions from nto writable to
          // writable

        }
      }
    }
  }




}
