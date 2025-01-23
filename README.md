# REDIS AT HOME

## Phase - 1
We start with a simple client and a server setup. Initially, the server can form connections with multiple clients.
Each client sends a message to the server and receives a message.

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
