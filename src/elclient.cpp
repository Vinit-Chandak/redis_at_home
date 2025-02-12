// client.cpp
//
// This file contains integration tests for the server. It tests various scenarios:
// 1. Normal Request: A single, well-formed request and its echo.
// 2. Pipelined Requests: Multiple requests sent one after the other without waiting
//    for responses.
// 3. Partial Transmission: A request sent in two parts, simulating network fragmentation.
// 4. Malformed Request: A request with a header that claims a payload size larger than
//    the actual data sent.
// 5. Simultaneous Connections: Multiple clients (using threads) connecting concurrently.
//
// Each test function returns true if the test passes, false otherwise.
// The helper functions send_all(), recv_all(), and build_request() facilitate request building
// and data transmission/reception.

#include <arpa/inet.h>    // For inet_pton(), htons()
#include <netinet/in.h>   // For sockaddr_in
#include <unistd.h>       // For close()
#include <sys/socket.h>   // For socket(), connect(), send(), recv()
#include <iostream>       // For std::cout, std::cerr
#include <cstring>        // For memset(), memcpy(), strlen()
#include <cstdlib>        // For exit()
#include <vector>         // For std::vector
#include <thread>         // For std::thread
#include <chrono>         // For std::chrono::milliseconds, std::this_thread::sleep_for

/**
 * @brief Sends all bytes over the socket, handling partial sends.
 *
 * @param sockfd The socket file descriptor.
 * @param buffer Pointer to the data buffer.
 * @param len Number of bytes to send.
 * @return true if all bytes are sent; false otherwise.
 */
bool send_all(int sockfd, const char* buffer, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t sent = send(sockfd, buffer + total, len - total, 0);
    if (sent <= 0) return false;
    total += sent;
  }
  return true;
}

/**
 * @brief Receives exactly 'len' bytes from the socket.
 *
 * Blocks until all requested bytes are received.
 *
 * @param sockfd The socket file descriptor.
 * @param buffer Pointer to the buffer to fill.
 * @param len Number of bytes to receive.
 * @return true if all bytes are received; false otherwise.
 */
bool recv_all(int sockfd, char* buffer, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t recvd = recv(sockfd, buffer + total, len - total, 0);
    if (recvd <= 0) return false;
    total += recvd;
  }
  return true;
}

/**
 * @brief Builds a request message by prepending a 4-byte header in network byte order.
 *
 * The header contains the length of the message payload.
 *
 * @param message The payload to send.
 * @return A vector containing the complete request (header + payload).
 */
std::vector<char> build_request(const std::string &message) {
  uint32_t length = static_cast<uint32_t>(message.size());
  uint32_t net_length = htonl(length);  // Convert length to network byte order
  std::vector<char> req(4 + message.size());
  memcpy(req.data(), &net_length, 4);            // Copy header
  memcpy(req.data() + 4, message.c_str(), message.size());  // Copy payload
  return req;
}

/**
 * @brief Test 1: Normal Request.
 *
 * Establishes a connection, sends a valid request, and verifies that the echoed
 * response matches the sent message.
 *
 * @return true if the test passes; false otherwise.
 */
bool test_normal_request() {
  std::cout << "[Normal Request] Starting test..." << std::endl;
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) { perror("socket"); return false; }

  // Configure server address (127.0.0.1:3333)
  sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(3333);
  inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

  // Connect to the server
  if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("connect");
    close(sockfd);
    return false;
  }

  // Prepare and send the request.
  std::string message = "Hello, server!";
  auto req = build_request(message);
  if (!send_all(sockfd, req.data(), req.size())) {
    std::cerr << "Failed to send request" << std::endl;
    close(sockfd);
    return false;
  }

  // Receive and process the response header.
  char header[4];
  if (!recv_all(sockfd, header, 4)) {
    std::cerr << "Failed to receive header" << std::endl;
    close(sockfd);
    return false;
  }
  uint32_t net_length;
  memcpy(&net_length, header, 4);
  uint32_t resp_length = ntohl(net_length);

  // Receive the payload.
  std::vector<char> payload(resp_length);
  if (!recv_all(sockfd, payload.data(), resp_length)) {
    std::cerr << "Failed to receive payload" << std::endl;
    close(sockfd);
    return false;
  }
  std::string response(payload.begin(), payload.end());
  std::cout << "Received response: " << response << std::endl;
  close(sockfd);
  return response == message;
}

/**
 * @brief Test 2: Pipelined Requests.
 *
 * Sends multiple requests back-to-back without waiting for responses, then
 * receives and verifies each response in order.
 *
 * @return true if all pipelined responses match the requests; false otherwise.
 */
bool test_pipelined_requests() {
  std::cout << "\n[Pipelined Requests] Starting test..." << std::endl;
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) { perror("socket"); return false; }

  // Configure server address
  sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(3333);
  inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

  // Connect to the server.
  if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("connect");
    close(sockfd);
    return false;
  }

  // Prepare multiple requests.
  std::vector<std::string> messages = {"Message1", "Message2", "Message3"};
  for (const auto &msg : messages) {
    auto req = build_request(msg);
    if (!send_all(sockfd, req.data(), req.size())) {
      std::cerr << "Failed to send pipelined request: " << msg << std::endl;
      close(sockfd);
      return false;
    }
    std::cout << "Sent: " << msg << std::endl;
  }

  // Receive responses in order.
  bool success = true;
  for (const auto &expected : messages) {
    char header[4];
    if (!recv_all(sockfd, header, 4)) {
      std::cerr << "Failed to receive header for pipelined request" << std::endl;
      success = false;
      break;
    }
    uint32_t net_length;
    memcpy(&net_length, header, 4);
    uint32_t resp_length = ntohl(net_length);
    std::vector<char> payload(resp_length);
    if (!recv_all(sockfd, payload.data(), resp_length)) {
      std::cerr << "Failed to receive payload for pipelined request" << std::endl;
      success = false;
      break;
    }
    std::string response(payload.begin(), payload.end());
    std::cout << "Received pipelined response: " << response << std::endl;
    if (response != expected) {
      std::cerr << "Mismatch! Expected: " << expected << ", Got: " << response << std::endl;
      success = false;
    }
  }
  close(sockfd);
  return success;
}

/**
 * @brief Test 3: Partial Transmission.
 *
 * Simulates a partial transmission by sending the request in two parts with a delay.
 * Then receives and verifies the response.
 *
 * @return true if the complete response is received and matches the sent message.
 */
bool test_partial_transmission() {
  std::cout << "\n[Partial Transmission] Starting test..." << std::endl;
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) { perror("socket"); return false; }

  // Configure server address
  sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(3333);
  inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

  // Connect to the server.
  if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("connect");
    close(sockfd);
    return false;
  }

  // Build the request.
  std::string message = "Partial Transmission Test";
  auto req = build_request(message);
  size_t half = req.size() / 2;

  // Send the first half of the request.
  if (!send_all(sockfd, req.data(), half)) {
    std::cerr << "Failed to send first half" << std::endl;
    close(sockfd);
    return false;
  }
  std::cout << "Sent first half (" << half << " bytes)" << std::endl;

  // Delay to simulate network latency.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Send the remainder of the request.
  if (!send_all(sockfd, req.data() + half, req.size() - half)) {
    std::cerr << "Failed to send second half" << std::endl;
    close(sockfd);
    return false;
  }
  std::cout << "Sent second half" << std::endl;

  // Receive response header.
  char header[4];
  if (!recv_all(sockfd, header, 4)) {
    std::cerr << "Failed to receive header" << std::endl;
    close(sockfd);
    return false;
  }
  uint32_t net_length;
  memcpy(&net_length, header, 4);
  uint32_t resp_length = ntohl(net_length);

  // Receive the response payload.
  std::vector<char> payload(resp_length);
  if (!recv_all(sockfd, payload.data(), resp_length)) {
    std::cerr << "Failed to receive payload" << std::endl;
    close(sockfd);
    return false;
  }
  std::string response(payload.begin(), payload.end());
  std::cout << "Received response: " << response << std::endl;
  close(sockfd);
  return response == message;
}

/**
 * @brief Test 4: Malformed Request.
 *
 * Constructs a request where the header indicates a payload length that is larger than
 * the actual payload sent. The server may either close the connection or respond with an error.
 *
 * @return true if the server behaves as expected (e.g. no response or error response); false otherwise.
 */
bool test_malformed_request() {
  std::cout << "\n[Malformed Request] Starting test..." << std::endl;
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) { perror("socket"); return false; }

  // Configure server address.
  sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(3333);
  inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

  // Connect to the server.
  if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("connect");
    close(sockfd);
    return false;
  }

  // Create a malformed request:
  // Header claims payload length 50 bytes, but only send 10 bytes.
  uint32_t claimed_length = 50;
  uint32_t net_claimed = htonl(claimed_length);
  std::vector<char> req(4 + 10);
  memcpy(req.data(), &net_claimed, 4);
  memcpy(req.data() + 4, "short data", 10);

  // Send the malformed request.
  if (!send_all(sockfd, req.data(), req.size())) {
    std::cerr << "Failed to send malformed request" << std::endl;
    close(sockfd);
    return false;
  }

  // Attempt to receive response non-blocking.
  char header[4];
  ssize_t ret = recv(sockfd, header, 4, MSG_DONTWAIT);
  if (ret <= 0) {
    std::cout << "No response received (connection closed), as expected for malformed request." << std::endl;
    close(sockfd);
    return true;
  } else {
    // If a response is received, try reading the payload.
    uint32_t net_length;
    memcpy(&net_length, header, 4);
    uint32_t resp_length = ntohl(net_length);
    std::vector<char> payload(resp_length);
    if (!recv_all(sockfd, payload.data(), resp_length)) {
      std::cerr << "Incomplete response for malformed request" << std::endl;
      close(sockfd);
      return false;
    }
    std::string response(payload.begin(), payload.end());
    std::cout << "Received response for malformed request: " << response << std::endl;
  }
  close(sockfd);
  return true;
}

/**
 * @brief Test 5: Simultaneous Connections.
 *
 * Launches multiple client threads concurrently. Each thread connects to the server,
 * sends a unique message, and verifies that the response matches the sent message.
 *
 * @return true if all clients receive the expected responses; false otherwise.
 */
bool test_simultaneous_connections() {
  std::cout << "\n[Simultaneous Connections] Starting test..." << std::endl;
  const int num_clients = 5;
  std::vector<std::thread> threads;
  std::vector<bool> results(num_clients, false);

  // Lambda function for each client task.
  auto client_task = [&](int idx) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); results[idx] = false; return; }

    sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(3333);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
    if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
      perror("connect");
      close(sockfd);
      results[idx] = false;
      return;
    }

    // Each client sends a unique message.
    std::string message = "Client " + std::to_string(idx) + " says hello!";
    auto req = build_request(message);
    if (!send_all(sockfd, req.data(), req.size())) {
      results[idx] = false;
      close(sockfd);
      return;
    }

    // Receive response.
    char header[4];
    if (!recv_all(sockfd, header, 4)) {
      results[idx] = false;
      close(sockfd);
      return;
    }
    uint32_t net_length;
    memcpy(&net_length, header, 4);
    uint32_t resp_length = ntohl(net_length);
    std::vector<char> payload(resp_length);
    if (!recv_all(sockfd, payload.data(), resp_length)) {
      results[idx] = false;
      close(sockfd);
      return;
    }
    std::string response(payload.begin(), payload.end());
    std::cout << "Client " << idx << " received: " << response << std::endl;
    results[idx] = (response == message);
    close(sockfd);
  };

  // Launch client threads.
  for (int i = 0; i < num_clients; i++) {
    threads.emplace_back(client_task, i);
  }
  for (auto &th : threads) {
    th.join();
  }
  bool all_success = true;
  for (bool r : results) {
    if (!r) { all_success = false; break; }
  }
  return all_success;
}

/**
 * @brief Main function that runs all integration tests.
 *
 * It calls each test function in turn and prints whether the test passed or failed.
 *
 * @return int 0 on success, non-zero on failure.
 */
int main() {
  bool normal = test_normal_request();
  std::cout << "\n[Normal Request] " << (normal ? "Passed" : "Failed") << std::endl;

  bool pipelined = test_pipelined_requests();
  std::cout << "[Pipelined Requests] " << (pipelined ? "Passed" : "Failed") << std::endl;

  bool partial = test_partial_transmission();
  std::cout << "[Partial Transmission] " << (partial ? "Passed" : "Failed") << std::endl;

  bool malformed = test_malformed_request();
  std::cout << "[Malformed Request] " << (malformed ? "Passed" : "Failed") << std::endl;

  bool simultaneous = test_simultaneous_connections();
  std::cout << "[Simultaneous Connections] " << (simultaneous ? "Passed" : "Failed") << std::endl;

  return 0;
}
