#REDIS AT HOME

## Phase - 1
We start with a simple client and a server setup. Initially, the server can form connections with multiple clients.
Each client sends a message to the server and receives a message.

STEPS TO START A SERVER

    1. Create a socket
    2. Set it's options
    3. Bind it to a address(this is a tuple of port and IP address)
    4. Start listening
    5. Accept a connection, do something with it and close the connection.

STEPS TO START A CLIENT

    1. Create a socket
    2. Connect to the server
    3. Send a message to the server
    4. Read the response from the server

## Phase - 2
TCP connections are considered "byte streams", this means that the data is transferred as a continuous stream of bytes.
So, we need to modify our server to process messages continuously, as it will handle multiple requests from a client.
For this we need to know where the message boundaries are.

The easiest way to split requests apart is by declaring how long the request is at the
beginning of the request. Let’s use the following scheme.

+-----+------+-----+------+-----+-----+-----+-----+

| len | msg1 | len | msg2 | more...

+-----+------+-----+------+-----+-----+-----+-----+

The protocol consists of 2 parts: a 4-byte little-endian integer indicating the length of the
following request, and a variable length request.

Now, initially, our server can only serve one client at any given time. Later, we use event loops to get around this.

We cannot simply use read() or write() to read/write the data from/to the buffer as there might be
partial reads/writes. Therefore we need to implement custom functions that continuously call read()/write() until the
desired number of bytes are read or written. Why partial read()/writes()?

| **Scenario**             | **When It Happens**                                                                                                                                       | **Impacts `read()`?** | **Impacts `write()`?** |
|---------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------|------------------------|
| **Network Buffering**     | Data available in the socket buffer is less than requested.                                                                                               | Yes                   | Yes                   |
| **Interruptions**         | Signal interrupts the system call before it completes.                                                                                                    | Yes                   | Yes                   |
| **Non-Blocking I/O**      | Socket/file is in non-blocking mode, and fewer bytes are immediately available for read/write.                                                             | Yes                   | Yes                   |
| **I/O Buffering**         | Limited buffer capacity for pipes, files, or devices.                                                                                                     | Yes                   | Yes                   |
| **End-of-File (EOF)**     | Reading beyond the end of the file or socket data stream.                                                                                                  | Yes                   | No                    |
| **Output Buffer Full**    | The socket/file’s output buffer is full, preventing more data from being written.                                                                          | No                    | Yes                   |
| **Flow Control (TCP)**    | TCP receiver signals the sender to slow down because its buffer is full.                                                                                  | No                    | Yes                   |

Things to look up in this part:
Blocking and Non Blocking System calls, EOF handling, when to user perror(), fprintf() etc.


## Phase - 3
elserver.cpp - event loop server
elclient.cpp - event loop client

Things to read up on: FDs, Non Blocking I/O, Event Loops, epoll,
https://copyconstruct.medium.com/nonblocking-i-o-99948ad7c957

Flow:
1. Create a socket, set it's options, bind it to an address, set it to non blocking mode and start listening.
2. Create an epoll instance and add the server socket to it so that everytime we get a request, we can add the client socket to the epoll instance.
3. Event Loop:
  a. Wait for an I/O event on any of the FDs in the intrest list.
  b. If the event is on the server socket, accept the connection.
  c. Set the client connection fd to non blocking mode and add it to the interest list with flags EPOLLIN, EPOLLOUT and EPOLLET.
  d. Create a connection state and add it to the map.
  e. If the event is on any of the client socket:
    i. If the event if EPOLLIN:
    ii. If the event is EPOLLOUT:

i. If the event is EPOLLIN:
  1. fetch the connection state from the map
  2. read_all()
  3. if EOF is encountered or there is a read error, close the connection and remove the connection state from the map.
  4. if we successfully read the data and we dont have anything in the write buffer, remove EPOLLOUT from the event notifs so that we don't get socket writable notifs even when we have nothing to read.
  5. if we successfully read the data and we have something in the write buffer, keep EPOLLOUT in the event notifs.

ii. If the event is EPOLLOUT:
  1. fetch the connection state from the map
  2. flush_write_buffer()
  3. if there is a write error, close the connection and remove the connection state from the map.
  4. if we have successfully written all the data in the write buffer, remove EPOLLOUT from the event notifs so that we don't get socket writable notifs even when we have nothing to write.

read_all():
  1. while(1):
    a. read the data from the client socket(at max curr capacity bytes)
    b. if the read rv is 0(EOF), close the connection, return 0.
    c. if the read rv is -1, check if the errno is EINTR(interrupt), retire and go to the next iteration of the loop.
    d. if the read rv is -1, check if the errno is EAGAIN(no data to read), return 1;
    e. if the read rv is < 0, read() error, return -1;
    f. if the read rv is > 0:
      i. append the data to the read buffer and increase its size
      ii. try to process the data in the buffer one message at a time(try_one_request)
      iii. move all the data at the start of the read buffer and do bookkeeping.
  2. return 1;

try_one_request():
  1. if the size of the read buffer is less than 4 bytes, return 0;
  2. get length of the message from the first 4 bytes of the read buffer
  3. if the size of the remaining read buffer is less than length of the message, return 0;
  4. process the message
  5. see if we can put the length and the message in the writebuffer
  6. if yes, copy the length and the message in the write buffer and try to flush it to the fd(flush_write_buffer)
  7. return length + 4;

flush_write_buffer():
  1. while(1):
    a. write the data from the write buffer to the client socket.
    c. if the write rv is -1, check if the errno is EINTR(interrupt), retire and go to the next iteration of the loop.
    d. if the write rv is -1, check if the errno is EAGAIN(fd write buffer full), break and return 1;
    e. if the write rv is < 0, write() error, log error, break and return -1;
    f. if the write rv is > 0, increment the count of number of bytes sent and go to the next iteration of the loop.
    g. if the write rv is 0, all the data has been written, break;
    h. do bookkeeping and return rv;


## Phase 4:
We will now implement a simple key value store using the event loop server and client.
The server will have a map of keys and values and will respond to the client with a response code and the value of the key.
We will implement 3 commands:
  get key - get the value of the key
  set key value - set the value of the key
  del key - delete the key
The command would be a list of strings. Both key and value will be strings.
+------+-----+------+-----+------+-----+-----+------+
| nstr | len | str1 | len | str2 | ... | len | strn |
+------+-----+------+-----+------+-----+-----+------+
The nstr is the number of strings and the len is the length of the following string. Both
are 32-bit integers.
The response is a 32-bit status code followed by the response string.
+-----+---------+
| res | data... |
+-----+---------+
EX:
+--------+----------+--------+----------+--------+----------+--------+
|  3     |   3      | "set"  |   3      | "key"  |   5      | "value"|
+--------+----------+--------+----------+--------+----------+--------+




#### Footnote - This readme is mostly for me to keep track of what I am doing, why and how.
