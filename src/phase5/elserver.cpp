// stdlib
#include <assert.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// system
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
// C++
#include <string>
#include <unordered_map>
#include <vector>
// project
#include "elserver.h"
#include "hashtable.h"
#include "logging.h"

using namespace std;

volatile bool running = true;
DB db;

// -----------------------------------------------------------------------
// set_fd_nb: sets fd to non-blocking mode
// -----------------------------------------------------------------------
int set_fd_nb(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    LOG_SYS_ERROR("error getting fd flags");
    return -1;
  }
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) == -1) {
    LOG_SYS_ERROR("error setting fd to non-blocking");
    return -1;
  }
  return 0;
}

// -----------------------------------------------------------------------
// flush_write_buffer
//   - We do real non-blocking writes
// -----------------------------------------------------------------------
int32_t flush_write_buffer(Connection *conn) {
  while (true) {
    ssize_t rv = write(conn->fd, conn->write_buffer + conn->bytes_sent,
                       conn->write_buffer_size - conn->bytes_sent);
    if (rv < 0 && errno == EINTR) {
      // retry
      continue;
    } else if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // can't write more, write buffer full
      return 0;
    } else if (rv < 0) {
      LOG_SYS_ERROR("error writing to fd");
      return -1;
    } else if (rv == 0) {
      // client closed connection
      return -1;
    } else {
      // wrote rv bytes
      conn->bytes_sent += rv;
      if (conn->bytes_sent == conn->write_buffer_size) {
        // done
        conn->write_buffer_size = 0;
        conn->bytes_sent = 0;
        return 1;
      }
    }
  }
  // never reached
  return 1;
}

bool cmp(HNode *lhs, HNode *rhs) {
  Entry *le = container_of(lhs, struct Entry, node);
  Entry *re = container_of(rhs, struct Entry, node);
  return le->key == re->key;
}

// FNV hash
static uint64_t str_hash(const uint8_t *data, size_t len) {
  uint32_t h = 0x811C9DC5;
  for (size_t i = 0; i < len; i++) {
    h = (h ^ data[i]) * 0x01000193;
  }
  return h;
}

void do_get(const char *key, RequestResponse *response) {
  Entry entry;
  entry.key = key;
  entry.node.hashcode = str_hash((const uint8_t *)key, strlen(key));

  HNode *node = hm_lookup(&db.hmap, &entry.node, &cmp);

  if (!node) {
    response->status = KEY_NOT_FOUND;
    response->response = "key not found\n";
  } else {
    string value = container_of(node, struct Entry, node)->value;
    response->status = SUCCESS;
    response->response = "get " + string(key) + " = " + value + "\n";
  }
}

void do_set(const char *key, const char *value, RequestResponse *response) {
  Entry entry;
  entry.key = key;
  entry.value = value;
  entry.node.hashcode = str_hash((const uint8_t *)key, strlen(key));

  HNode *node = hm_lookup(&db.hmap, &entry.node, &cmp);

  if (node) {
    container_of(node, struct Entry, node)->value = value;
  } else {
    Entry *ent = new Entry();
    ent->key = key;
    ent->value = value;
    ent->node.hashcode = str_hash((const uint8_t *)key, strlen(key));
    hm_insert(&db.hmap, &ent->node);
  }
  response->status = SUCCESS;
  response->response = "set " + string(key) + " to " + string(value) + "\n";
}

void do_del(const char *key, RequestResponse *response) {
  Entry entry;
  entry.key = key;
  entry.node.hashcode = str_hash((const uint8_t *)key, strlen(key));

  HNode *node = hm_lookup(&db.hmap, &entry.node, &cmp);

  if (!node) {
    response->status = KEY_NOT_FOUND;
    response->response = "key " + string(key) + " not found\n";
  } else {
    HNode *deletedNode = hm_delete(&db.hmap, &entry.node, &cmp);
    Entry *ent = container_of(deletedNode, struct Entry, node);
    free(ent);
    response->status = SUCCESS;
    response->response = "key " + string(key) + " deleted\n";
  }
}

// -----------------------------------------------------------------------
// process_request: executes a command vector
// -----------------------------------------------------------------------
RequestResponse process_request(const std::vector<std::string> &command) {
  RequestResponse response;
  if (command.empty()) {
    response.status = UNKNOWN_COMMAND;
    response.response = "unknown command\n";
    return response;
  }

  if (command[0] == "set") {
    if (command.size() != 3) {
      response.status = ERROR;
      response.response =
          "invalid number of arguments, set requires two arguments\n";
    } else {
      do_set(command[1].c_str(), command[2].c_str(), &response);
    }
  } else if (command[0] == "get") {
    if (command.size() != 2) {
      response.status = ERROR;
      response.response = "invalid number of arguments\n";
    } else {
      do_get(command[1].c_str(), &response);
    }
  } else if (command[0] == "del") {
    if (command.size() != 2) {
      response.status = ERROR;
      response.response =
          "invalid number of arguments, del requires one argument\n";
    } else {
      do_del(command[1].c_str(), &response);
    }
  } else {
    response.status = UNKNOWN_COMMAND;
    response.response = "unknown command\n";
  }
  return response;
}

// -----------------------------------------------------------------------
// try_one_request: parse & process exactly one request from the buffer
//   - returns 0 => partial request, need more data
//   - returns >0 => consumed that many bytes (fully parsed request)
//   - returns -1 => fatal error => close connection
// -----------------------------------------------------------------------
int32_t try_one_request(Connection *conn, char *start) {
  // Need at least 4 bytes for nStr
  if (conn->read_buffer_size < 4) {
    printf("not enough data to read\n");
    return 0;
  }
  int32_t nStr;
  memcpy(&nStr, start, 4);
  nStr = ntohl(nStr);
  start += 4;
  int32_t consumed = 4;

  if (nStr < 2 || nStr > 3) {
    // fatal
    const char msg[] = "invalid command\n";
    size_t off = conn->write_buffer_size;
    int32_t msgLen = (int32_t)strlen(msg);
    int32_t netLen = htonl(msgLen);
    memcpy(conn->write_buffer + off, &netLen, 4);
    memcpy(conn->write_buffer + off + 4, msg, msgLen);
    conn->write_buffer_size += (4 + msgLen);
    flush_write_buffer(conn);
    return -1;
  }

  // We'll track how many bytes the "full request" is taking
  size_t requestBytesSoFar = 4;

  std::vector<std::string> command;
  command.reserve(nStr);

  for (int i = 0; i < nStr; i++) {
    // Need 4 bytes for next string length
    if ((conn->read_buffer + conn->read_buffer_size) - start < 4) {
      printf("not enough data to read\n");
      return 0;
    }
    int32_t length;
    memcpy(&length, start, 4);
    length = ntohl(length);
    start += 4;
    consumed += 4;
    requestBytesSoFar += 4; // overhead for length

    // If the sum of lengths so far plus this string is > MAX_MSG_SIZE, fatal
    if (requestBytesSoFar + length > MAX_MSG_SIZE) {
      LOG_ERROR("oversized Request");
      char response[] = "oversized request\n";
      int32_t length = htonl(strlen(response));
      memcpy(conn->write_buffer + conn->write_buffer_size, &length, 4);
      memcpy(conn->write_buffer + conn->write_buffer_size + 4, response,
             strlen(response));
      conn->write_buffer_size += (4 + strlen(response));
      flush_write_buffer(conn);
      return -1;
    }

    // Check if enough leftover data for the string
    if ((conn->read_buffer + conn->read_buffer_size) - start < length) {
      printf("not enough data to read\n");
      return 0;
    }

    std::string val(start, length);
    command.push_back(val);
    start += length;
    consumed += length;
    requestBytesSoFar += length;
  }

  // We have a complete command
  RequestResponse resp = process_request(command);

  // Append the response
  size_t off = conn->write_buffer_size;
  int32_t sz = (int32_t)resp.response.size();
  int32_t net_sz = htonl(sz);
  memcpy(conn->write_buffer + off, &net_sz, 4);
  memcpy(conn->write_buffer + off + 4, resp.response.c_str(), sz);
  conn->write_buffer_size += (4 + sz);

  return consumed;
}

// -----------------------------------------------------------------------
// read_all: repeatedly read from fd and parse requests
//   - returns 0 => close connection
//   - returns -1 => read error => also close
//   - returns 1 => partial read but connection remains open
// -----------------------------------------------------------------------
int32_t read_all(Connection *conn) {
  while (true) {
    size_t capacity = sizeof(conn->read_buffer) - conn->read_buffer_size;
    ssize_t rv =
        read(conn->fd, conn->read_buffer + conn->read_buffer_size, capacity);
    if (rv < 0 && errno == EINTR) {
      continue;
    } else if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // no more data
      return 1;
    } else if (rv < 0) {
      LOG_SYS_ERROR("read() error");
      return -1;
    } else if (rv == 0) {
      printf("EOF, the client closed the connection\n");
      return 0;
    }
    // We read some data
    conn->read_buffer_size += rv;

    char *start = conn->read_buffer;
    while (true) {
      int32_t consumed = try_one_request(conn, start);
      if (consumed > 0) {
        start += consumed;
      } else if (consumed == 0) {
        // partial
        break;
      } else {
        // consumed < 0 => fatal
        return 0;
      }
    }
    // shift unconsumed data to front
    size_t used = (size_t)(start - conn->read_buffer);
    memmove(conn->read_buffer, start, conn->read_buffer_size - used);
    conn->read_buffer_size -= used;
  }
  return 1; // unreachable
}

// Global epoll-related
struct epoll_event event, events[MAX_EVENTS];
std::unordered_map<int, Connection *> fd2Connection;
std::unordered_map<std::string, std::string> kvStore;

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    LOG_SYS_ERROR("error creating socket");
    exit(EXIT_FAILURE);
  }
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(3333);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    LOG_SYS_ERROR("bind() error");
    exit(EXIT_FAILURE);
  }
  if (set_fd_nb(fd) < 0) {
    exit(EXIT_FAILURE);
  }
  if (listen(fd, 10) < 0) {
    LOG_SYS_ERROR("listen() error");
    exit(EXIT_FAILURE);
  }
  printf("server listening on port 3333\n");

  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    LOG_SYS_ERROR("epoll_create1() error");
    exit(EXIT_FAILURE);
  }

  event.events = EPOLLIN;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
    LOG_SYS_ERROR("epoll_ctl(ADD) server socket error");
    exit(EXIT_FAILURE);
  }

  while (running) {
    int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (n < 0) {
      LOG_SYS_ERROR("epoll_wait error");
      break;
    }
    for (int i = 0; i < n; i++) {
      if (events[i].data.fd == fd) {
        while (true) {
          struct sockaddr_in client_addr;
          socklen_t sz = sizeof(client_addr);
          int connfd = accept(fd, (struct sockaddr *)&client_addr, &sz);
          if (connfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }
            LOG_SYS_ERROR("accept() error");
            break;
          }
          printf("accepted connection from %s:%d\n",
                 inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

          if (set_fd_nb(connfd) < 0) {
            close(connfd);
            continue;
          }
          struct epoll_event ev;
          ev.events = EPOLLIN | EPOLLET;
          ev.data.fd = connfd;
          if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connfd, &ev) < 0) {
            LOG_SYS_ERROR("epoll_ctl(ADD) client error");
            close(connfd);
            continue;
          }
          Connection *c = new Connection;
          c->fd = connfd;
          fd2Connection[connfd] = c;
        }
      } else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        int cfd = events[i].data.fd;
        if (cfd == fd) {
          LOG_SYS_ERROR("epoll error on listening socket => exit");
          running = false;
        } else {
          LOG_SYS_ERROR("epoll error on client => close");
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cfd, nullptr) < 0) {
            LOG_SYS_ERROR("epoll_ctl(DEL) error");
          }
          close(cfd);
          auto it = fd2Connection.find(cfd);
          if (it != fd2Connection.end()) {
            delete it->second;
            fd2Connection.erase(it);
          }
        }
      } else {
        // handle read/write on existing client
        int cfd = events[i].data.fd;
        auto it = fd2Connection.find(cfd);
        if (it == fd2Connection.end()) {
          continue;
        }
        Connection *conn = it->second;

        if (events[i].events & EPOLLIN) {
          int rv = read_all(conn);
          if (rv <= 0) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cfd, nullptr);
            close(cfd);
            delete conn;
            fd2Connection.erase(cfd);
            continue;
          }
        }
        // If we have data to write, enable EPOLLOUT
        if (conn->write_buffer_size > 0) {
          struct epoll_event ev;
          ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
          ev.data.fd = cfd;
          epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cfd, &ev);
        }

        if (events[i].events & EPOLLOUT) {
          int wv = flush_write_buffer(conn);
          if (wv < 0) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cfd, nullptr);
            close(cfd);
            delete conn;
            fd2Connection.erase(cfd);
          } else if (conn->write_buffer_size == 0) {
            // turn off EPOLLOUT
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = cfd;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cfd, &ev);
          }
        }
      }
    }
  }

  // cleanup
  for (auto it = fd2Connection.begin(); it != fd2Connection.end();) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, it->first, nullptr);
    close(it->first);
    delete it->second;
    it = fd2Connection.erase(it);
  }
  close(epoll_fd);
  return 0;
}
