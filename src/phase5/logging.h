#ifndef LOGGING_H
#define LOGGING_H

#define LOG_ERROR(msg)                                                         \
  fprintf(stderr, "[ERROR] %s:%d(): %s\n", __FILE__, __LINE__, (msg))

#define LOG_SYS_ERROR(msg)                                                     \
  fprintf(stderr, "[ERROR] %s:%d(): %s\n%s\n", __FILE__, __LINE__, (msg),      \
          strerror(errno))

#endif // LOGGING_H
