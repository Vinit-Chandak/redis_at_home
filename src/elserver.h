#ifndef SERVER_H
#define SERVER_H

#include <cstddef>
#include <cstdint>

// Maximum message size for data (1 << 10, as defined in your server.cpp)
const size_t MAX_MSG_SIZE = 1 << 10;

// Connection structure that holds information about a client connection.
// It includes file descriptor, read/write buffers, and related sizes.
struct Connection {
    int32_t fd;
    size_t read_buffer_size;
    size_t write_buffer_size;
    size_t bytes_sent;
    char read_buffer[4 + MAX_MSG_SIZE];
    char write_buffer[4 + MAX_MSG_SIZE];
};

// Sets a file descriptor to non-blocking mode.
// Returns 0 on success and -1 on error.
int set_fd_nb(int fd);

// Flushes the connection's write buffer by writing data to the socket.
// Returns a positive value on success, 0 if more data remains, or -1 on error.
int32_t flush_write_buffer(Connection *conn);

// Processes a single request from the connection's read buffer.
// It reads the 4-byte length header, validates the message, and echoes the message back.
// Returns the number of bytes consumed (header + message), 0 if not enough data,
// or a negative value on error.
int32_t try_one_request(Connection *conn, char *start);

// Reads data from the connection's file descriptor into the read buffer.
// Returns 1 on success, 0 if EOF is reached, or -1 on error.
int32_t read_all(Connection *conn);

#endif // SERVER_H
