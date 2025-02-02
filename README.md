# REDIS AT HOME

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
