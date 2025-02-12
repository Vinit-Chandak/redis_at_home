// tests/server_test.cpp
#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include "../src/elserver.h" // Must declare Connection, set_fd_nb, flush_write_buffer, try_one_request, and read_all

// ----------------------------------------------------------------------
// Test for set_fd_nb: verifies that a valid file descriptor gets O_NONBLOCK
// ----------------------------------------------------------------------
TEST(SetFdNbTest, SetsNonBlockingFlag) {
    // Create a temporary file so we have a valid file descriptor.
    int fd = open("tempfile", O_CREAT | O_RDWR, 0666);
    ASSERT_GE(fd, 0) << "Failed to open temporary file.";

    // Clear the non-blocking flag.
    int flags = fcntl(fd, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);

    // Call set_fd_nb, which should add the O_NONBLOCK flag.
    EXPECT_EQ(set_fd_nb(fd), 0);

    // Verify that O_NONBLOCK is now set.
    flags = fcntl(fd, F_GETFL, 0);
    EXPECT_TRUE(flags & O_NONBLOCK);

    close(fd);
    remove("tempfile");
}

// ----------------------------------------------------------------------
// Test for flush_write_buffer (using the stub version under UNIT_TEST)
// ----------------------------------------------------------------------
TEST(FlushWriteBufferTest, StubbedFlush) {
    // Allocate a Connection on the heap.
    Connection *conn = new Connection;
    conn->fd = 0; // Dummy value, not used by the stub.
    // Pre-load the write buffer with known test data.
    const char* testData = "Test Data";
    size_t data_len = strlen(testData);
    memcpy(conn->write_buffer, testData, data_len);
    conn->write_buffer_size = data_len;
    conn->bytes_sent = 0;

    // Call flush_write_buffer.
    int32_t rv = flush_write_buffer(conn);
    EXPECT_EQ(rv, 1);
    // The stub should mark all bytes as "sent" and then clear the write buffer.
    EXPECT_EQ(conn->bytes_sent, data_len);
    EXPECT_EQ(conn->write_buffer_size, 0);

    delete conn;
}

// ----------------------------------------------------------------------
// Unit tests for try_one_request
// ----------------------------------------------------------------------

// Test 1: Valid Message
TEST(TryOneRequestTest, ValidMessage) {
    // Allocate a Connection on the heap.
    Connection *conn = new Connection;
    conn->fd = 0;  // Dummy value; not used in UNIT_TEST mode.
    conn->read_buffer_size = 0;
    conn->write_buffer_size = 0;
    conn->bytes_sent = 0;
    memset(conn->read_buffer, 0, sizeof(conn->read_buffer));
    memset(conn->write_buffer, 0, sizeof(conn->write_buffer));

    // Prepare a valid message with a header (4 bytes) and payload.
    const char* message = "Hello, GoogleTest!";
    int32_t length = static_cast<int32_t>(strlen(message));
    int32_t net_length = htonl(length);
    memcpy(conn->read_buffer, &net_length, 4);
    memcpy(conn->read_buffer + 4, message, length);
    conn->read_buffer_size = 4 + length;

    // Call try_one_request; it should consume header+payload.
    int32_t consumed = try_one_request(conn, conn->read_buffer);
    EXPECT_EQ(consumed, 4 + length);

    // (Note: In our stub, flush_write_buffer clears the write buffer.
    // To inspect echoed data, you could modify the stub to leave data intact.)

    delete conn;
}

// Test 2: Insufficient Data for Header (< 4 bytes)
TEST(TryOneRequestTest, InsufficientDataHeader) {
    Connection *conn = new Connection;
    conn->fd = 0;
    conn->read_buffer_size = 2;  // Only 2 bytes available.
    conn->write_buffer_size = 0;
    conn->bytes_sent = 0;
    memset(conn->read_buffer, 0, sizeof(conn->read_buffer));
    memset(conn->write_buffer, 0, sizeof(conn->write_buffer));

    int32_t consumed = try_one_request(conn, conn->read_buffer);
    EXPECT_EQ(consumed, 0);

    delete conn;
}

// Test 3: Insufficient Data for Payload (header present but payload incomplete)
TEST(TryOneRequestTest, InsufficientDataPayload) {
    Connection *conn = new Connection;
    conn->fd = 0;
    conn->write_buffer_size = 0;
    conn->bytes_sent = 0;
    memset(conn->read_buffer, 0, sizeof(conn->read_buffer));
    memset(conn->write_buffer, 0, sizeof(conn->write_buffer));

    // Set a header indicating a 20-byte payload, but only provide 10 bytes.
    int32_t length = 20;
    int32_t net_length = htonl(length);
    memcpy(conn->read_buffer, &net_length, 4);
    const char *partialPayload = "1234567890";
    memcpy(conn->read_buffer + 4, partialPayload, 10);
    conn->read_buffer_size = 4 + 10;

    int32_t consumed = try_one_request(conn, conn->read_buffer);
    EXPECT_EQ(consumed, 0);

    delete conn;
}

// Test 4: Out-of-Bounds Length (length > MAX_MSG_SIZE)
TEST(TryOneRequestTest, OutOfBoundsLength) {
    Connection *conn = new Connection;
    conn->fd = 0;
    conn->read_buffer_size = 0;
    conn->write_buffer_size = 0;
    conn->bytes_sent = 0;
    memset(conn->read_buffer, 0, sizeof(conn->read_buffer));
    memset(conn->write_buffer, 0, sizeof(conn->write_buffer));

    // Use a length value greater than MAX_MSG_SIZE.
    int32_t length = MAX_MSG_SIZE + 1;
    int32_t net_length = htonl(length);
    memcpy(conn->read_buffer, &net_length, 4);
    // Provide dummy payload data.
    memset(conn->read_buffer + 4, 'A', length);
    conn->read_buffer_size = 4 + length;

    int32_t consumed = try_one_request(conn, conn->read_buffer);
    EXPECT_EQ(consumed, -1);

    delete conn;
}

// Test 5: Write Buffer Full
TEST(TryOneRequestTest, WriteBufferFull) {
    Connection *conn = new Connection;
    conn->fd = 0;
    // Pre-fill the write buffer nearly to capacity.
    // For testing, MAX_MSG_SIZE is 1024 so total write buffer capacity is 1028 bytes.
    conn->write_buffer_size = 1025;  // Only 3 bytes free.
    conn->bytes_sent = 0;
    memset(conn->read_buffer, 0, sizeof(conn->read_buffer));
    // Fill write_buffer with dummy data.
    memset(conn->write_buffer, 'X', sizeof(conn->write_buffer));

    // Create a message that needs 14 bytes (4 header + 10 payload).
    const char* message = "0123456789";
    int32_t length = static_cast<int32_t>(strlen(message));
    int32_t net_length = htonl(length);
    memcpy(conn->read_buffer, &net_length, 4);
    memcpy(conn->read_buffer + 4, message, length);
    conn->read_buffer_size = 4 + length;

    int32_t consumed = try_one_request(conn, conn->read_buffer);
    EXPECT_EQ(consumed, 4 + length);
    // Since the write buffer was nearly full, no new data should be added.
    EXPECT_EQ(conn->write_buffer_size, 1025);

    delete conn;
}

// ----------------------------------------------------------------------
// Tests for read_all
// ----------------------------------------------------------------------

// Test 6: read_all reads a complete message from a pipe.
// We simulate a file descriptor by creating a pipe, writing a complete message,
// then closing the write end so that read_all eventually sees an EOF.
TEST(ReadAllTest, ReadsCompleteMessage) {
    int pipefd[2];
    // Create a pipe.
    ASSERT_EQ(pipe(pipefd), 0) << "Failed to create pipe.";

    // Prepare a message: 4-byte header + payload.
    const char* message = "Hello Pipe";
    int32_t length = static_cast<int32_t>(strlen(message));
    int32_t net_length = htonl(length);
    char buffer[4 + 100] = {0};
    memcpy(buffer, &net_length, 4);
    memcpy(buffer + 4, message, length);

    // Write the complete message into the pipe.
    ssize_t written = write(pipefd[1], buffer, 4 + length);
    ASSERT_EQ(written, 4 + length);

    // Crucial: close the write end so that read() will eventually return 0 (EOF).
    close(pipefd[1]);

    // Create a Connection and set its fd to the read end.
    Connection *conn = new Connection;
    conn->fd = pipefd[0];
    conn->read_buffer_size = 0;
    conn->write_buffer_size = 0;
    conn->bytes_sent = 0;
    memset(conn->read_buffer, 0, sizeof(conn->read_buffer));
    memset(conn->write_buffer, 0, sizeof(conn->write_buffer));

    // Call read_all. Since the write end is closed, after reading the data,
    // read() should return 0 (EOF) and read_all will return 0.
    int32_t rv = read_all(conn);
    // We expect that the message is read before encountering EOF.
    // In our implementation, after processing the message, read_all loops again,
    // then read() returns 0, prints "EOF, the client closed the connection", and returns 0.
    EXPECT_EQ(rv, 0);
    // After processing, the read_buffer should be empty (or contain leftover data if not fully consumed).
    // Here, we expect it to be 0 because try_one_request would have consumed the message.
    EXPECT_EQ(conn->read_buffer_size, 0);

    close(pipefd[0]);
    delete conn;
}
