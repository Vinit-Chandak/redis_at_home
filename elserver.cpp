#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <assert.h>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

using namespace std;

#define LOG_ERROR(fmt)                                                         \
  (fprintf(stderr, "[ERROR] %s:%d(): " fmt "\n", __FILE__, __LINE__))

#define LOG_SYS_ERROR(fmt)                                                     \
  (fprintf(stderr, "[ERROR] %s:%d(): " fmt "\n%s\n", __FILE__, __LINE__,       \
           strerror(errno)))

// maximum no of events epoll returns that are ready
const int MAX_EVENTS = 10;
const size_t MAX_MSG_SIZE = 1 << 24;
volatile bool running = true;

struct Connection {
  int32_t fd;
  size_t read_buffer_size = 0;
  size_t write_buffer_size = 0;
  size_t bytes_sent = 0;
  char read_buffer[4 + MAX_MSG_SIZE];
  char write_buffer[4 + MAX_MSG_SIZE];
};

struct epoll_event event, events[MAX_EVENTS];
unordered_map<int, Connection *> fd2Connection;

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

int32_t flush_write_buffer(Connection *conn) {
  int32_t rv = 0;
  while (1) {
    rv = write(conn->fd, conn->write_buffer + conn->bytes_sent,
               conn->write_buffer_size - conn->bytes_sent);
    if (rv < 0 && errno == EINTR)
      continue;
    else if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      break;
    else if (rv < 0) {
      LOG_SYS_ERROR("Error writing to fd");
      break;
    } else if (rv == 0) {
      break;
    } else {
      conn->bytes_sent += (size_t)rv;
    }
  }
  conn->write_buffer_size -= conn->bytes_sent;
  memmove(conn->write_buffer, conn->write_buffer + conn->bytes_sent,
          conn->write_buffer_size);
  conn->bytes_sent = 0;
  if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return 0;
  else if (rv < 0)
    return -1;
  else
    return 1;
}

int32_t try_one_request(Connection *conn, char *start) {
  if (conn->read_buffer_size < 4) {
    printf("Not enough data to read\n");
    return 0;
  }
  int32_t length;
  memcpy(&length, start, 4);
  length = ntohl(length);
  if (length < 0 || length > MAX_MSG_SIZE) {
    return -1;
  }
  if (conn->read_buffer_size < (length + 4 + (start - conn->read_buffer))) {
    printf("Not enough data to read\n");
    return 0;
  }
  printf("The client says: %.*s\n", length, start + 4);
  if ((MAX_MSG_SIZE + 4) - conn->write_buffer_size < (length + 4)) {
    printf("Write buffer full, cannot send a response to the client\n");
  } else {
    int32_t net_length = htonl(length);
    memcpy(conn->write_buffer + conn->write_buffer_size, &net_length, 4);
    conn->write_buffer_size += 4;
    memcpy(conn->write_buffer + conn->write_buffer_size, (start + 4), length);
    conn->write_buffer_size += length;
    int32_t rv = flush_write_buffer(conn);
  }
  return length + 4;
}

int32_t read_all(Connection *conn) {
  int32_t rv;
  while (1) {
    size_t capacity = (size_t)(MAX_MSG_SIZE + 4) - conn->read_buffer_size;
    rv = read(conn->fd, conn->read_buffer + conn->read_buffer_size, capacity);
    if (rv < 0 && errno == EINTR)
      continue;
    else if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return 1;
    else if (rv < 0) {
      LOG_SYS_ERROR("read() error");
      return -1;
    } else if (rv == 0) {
      printf("EOF, the client closed the connection\n");
      return 0;
    }
    conn->read_buffer_size += (size_t)rv;
    char *start = conn->read_buffer;
    while (1) {
      int32_t consumed = try_one_request(conn, start);
      if (consumed > 0) {
        start += consumed;
      } else if (consumed == 0) {
        break;
      } else {
        return false;
      }
    }
    size_t processed = start - conn->read_buffer;
    memmove(conn->read_buffer, start, processed);
    conn->read_buffer_size -= processed;
  }
  return 1;
}

int main() {
  // create a socket
  int fd = socket(AF_INET, SOCK_STREAM, 0);
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
  server_addr.sin_port = htons(3333);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int32_t rv =
      bind(fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));
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
  while (running) {
    // wait for an I/O event on any of the fd in the interest list
    int n = epoll_wait(epoll_fd, (struct epoll_event *)events, MAX_EVENTS, -1);
    if (n < 0) {
      LOG_SYS_ERROR("epoll_wait() error");
      break;
    }
    for (int i = 0; i < n; i++) {
      // check if the event if for the server socket(i.e new connection)
      if (events[i].data.fd == fd) {
        // accept a new client connection
        struct sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);

        int connfd =
            accept(fd, (struct sockaddr *)&client_addr, (socklen_t *)&socklen);
        if (connfd < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // if no pending connections are present on the queue
            continue;
          }
          LOG_ERROR("Error accepting the client connection.");
          continue;
        }
        printf("Accepted connection from %s:%d \n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // set the client connection fd to non blocking mode
        int32_t rv = set_fd_nb(connfd);
        if (rv < 0) {
          close(connfd);
          continue;
        }

        // add the client connection fd to the interest list
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = connfd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connfd,
                      (struct epoll_event *)&event) < 0) {
          LOG_SYS_ERROR("Cannot put the client fd on the interest list");
          close(connfd);
          continue;
        }

        // create a connection state and add it to the map
        Connection *conn = new Connection;
        conn->fd = connfd;
        fd2Connection[connfd] = conn;

      } else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        if (events[i].data.fd == fd) {
          LOG_SYS_ERROR("epoll error in listning socket, exiting");
          running = false;
        } else {
          LOG_SYS_ERROR("epoll error in client fd, closing the connection");
          Connection *conn = fd2Connection[events[i].data.fd];
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL) < 0) {
            LOG_SYS_ERROR("epoll_ctl() DEL error");
          }
          close(conn->fd);
          delete fd2Connection[conn->fd];
          fd2Connection.erase(conn->fd);
        }
      } else {
        Connection *conn = fd2Connection[events[i].data.fd];
        if (events[i].events & EPOLLIN) {
          // read the data from the fd into the connection buffer
          int32_t rv = read_all(conn);
          if (rv <= 0) {
            if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL) < 0) {
              LOG_SYS_ERROR("epoll_ctl() DEL error");
            }
            close(conn->fd);
            delete fd2Connection[conn->fd];
            fd2Connection.erase(conn->fd);
          } else {
            if (conn->write_buffer_size == 0) {
              struct epoll_event ev;
              ev.events = EPOLLIN | EPOLLET;
              ev.data.fd = conn->fd;
              if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
                LOG_SYS_ERROR("epoll_ctl() error");
                close(conn->fd);
                delete fd2Connection[conn->fd];
                fd2Connection.erase(conn->fd);
              }
            }
          }
        }
        if (events[i].events & EPOLLOUT) {
          if (conn->write_buffer_size > 0) {
            int32_t rv = flush_write_buffer(conn);
            if (rv < 0) {
              // if we get a write() error, we close the connection
              if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL) < 0) {
                LOG_SYS_ERROR("epoll_ctl() DEL error");
              }
              close(conn->fd);
              delete fd2Connection[conn->fd];
              fd2Connection.erase(conn->fd);
            } else {
              if (conn->write_buffer_size == 0) {
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = conn->fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
                  LOG_SYS_ERROR("epoll_ctl() error");
                  close(conn->fd);
                  delete fd2Connection[conn->fd];
                  fd2Connection.erase(conn->fd);
                }
              }
            }
          }
        }
      }
    }
  }
  for (auto it = fd2Connection.begin(); it != fd2Connection.end();) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, it->first, NULL);
    close(it->first);
    delete it->second;
    it = fd2Connection.erase(it);
  }
  close(epoll_fd);
  return 0;
}
