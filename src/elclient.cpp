#include "logging.h"
#include <arpa/inet.h> // For htons(), inet_pton(), ntohl(), etc.
#include <cstdlib>     // For EXIT_FAILURE, EXIT_SUCCESS, atoi()
#include <cstring>     // For memcpy(), memset(), strerror()
#include <iostream>
#include <string>
#include <sys/socket.h> // For socket functions
#include <unistd.h>     // For close()
#include <vector>

// Hardcoded server address and port.
const std::string SERVER_HOST = "127.0.0.1";
const int SERVER_PORT = 3333;

// Helper function to send all bytes over the socket.
bool send_all(int sockfd, const char *buffer, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t sent = send(sockfd, buffer + total, len - total, 0);
    if (sent <= 0) {
      // Use LOG_ERROR for a generic error message with no punctuation
      LOG_ERROR("failed to send all bytes");
      return false;
    }
    total += sent;
  }
  return true;
}

// Helper function to receive exactly 'len' bytes from the socket.
bool recv_all(int sockfd, char *buffer, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t recvd = recv(sockfd, buffer + total, len - total, 0);
    if (recvd <= 0) {
      LOG_ERROR("failed to receive all bytes");
      return false;
    }
    total += recvd;
  }
  return true;
}

// Build the request message in our custom protocol.
// The protocol format is as follows:
//   - First 4 bytes: an integer (in network byte order) representing the number
//   of strings in the command.
//   - Then, for each token: 4 bytes for the string length (network order)
//   followed by the string bytes.
std::vector<char> build_request(const std::vector<std::string> &tokens) {
  // Calculate the total size needed.
  size_t total_size = 4; // For the string count.
  for (const auto &token : tokens) {
    total_size +=
        4 + token.size(); // 4 bytes for string length plus the string bytes.
  }

  std::vector<char> request(total_size);
  char *ptr = request.data();

  // Write the number of strings.
  int32_t nTokens = tokens.size();
  int32_t net_nTokens = htonl(nTokens);
  memcpy(ptr, &net_nTokens, 4);
  ptr += 4;

  // For each string, write its length and the string data.
  for (const auto &token : tokens) {
    int32_t token_len = token.size();
    int32_t net_token_len = htonl(token_len);
    memcpy(ptr, &net_token_len, 4);
    ptr += 4;
    memcpy(ptr, token.data(), token_len);
    ptr += token_len;
  }
  return request;
}

int main(int argc, char *argv[]) {
  // Expect at least one command-line argument (the command).
  if (argc < 2) {
    // Construct an error message that includes argv[0].
    char usageBuf[256];
    snprintf(usageBuf, sizeof(usageBuf), "usage: %s <command> [arguments...]",
             argv[0]);
    LOG_ERROR(usageBuf);
    return EXIT_FAILURE;
  }

  // Build the tokens vector from command line arguments.
  std::vector<std::string> tokens;
  for (int i = 1; i < argc; ++i) {
    tokens.push_back(argv[i]);
  }

  // Create a socket.
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    // Use LOG_SYS_ERROR to include strerror(errno).
    LOG_SYS_ERROR("socket");
    return EXIT_FAILURE;
  }

  // Set up the server address.
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if (inet_pton(AF_INET, SERVER_HOST.c_str(), &serv_addr.sin_addr) <= 0) {
    // We log the invalid address string, starting with lowercase
    char msg[256];
    snprintf(msg, sizeof(msg), "invalid address: %s", SERVER_HOST.c_str());
    LOG_ERROR(msg);
    close(sockfd);
    return EXIT_FAILURE;
  }

  // Connect to the server.
  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    LOG_SYS_ERROR("connect() error");
    close(sockfd);
    return EXIT_FAILURE;
  }

  // Build the request message.
  std::vector<char> request = build_request(tokens);
  if (!send_all(sockfd, request.data(), request.size())) {
    LOG_ERROR("failed to send request");
    close(sockfd);
    return EXIT_FAILURE;
  }

  // Receive the response: first, read the 4-byte response length.
  char header[4];
  if (!recv_all(sockfd, header, 4)) {
    LOG_ERROR("failed to receive response header");
    close(sockfd);
    return EXIT_FAILURE;
  }
  int32_t net_resp_length;
  memcpy(&net_resp_length, header, 4);
  int32_t resp_length = ntohl(net_resp_length);
  if (resp_length < 0) {
    LOG_ERROR("invalid response length received");
    close(sockfd);
    return EXIT_FAILURE;
  }

  // Now receive the response payload.
  std::vector<char> resp(resp_length);
  if (!recv_all(sockfd, resp.data(), resp_length)) {
    LOG_ERROR("failed to receive response payload");
    close(sockfd);
    return EXIT_FAILURE;
  }

  // Print the response (assume it’s text).
  std::string response(resp.begin(), resp.end());
  std::cout << response;

  close(sockfd);
  return EXIT_SUCCESS;
}
