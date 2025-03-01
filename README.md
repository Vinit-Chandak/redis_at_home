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



## Phase 5:
Now we implement out custom hash table with progressive rehashing. Why can't we just use std::unordered_map?

### 1.1 Why Hash Table Resizing Can Stall a Service:

Hash tables need to expand periodically as you insert more elements to maintain a low load factor (i.e., the ratio of items to buckets). When std::unordered_map (and many other general-purpose implementations) crosses its load factor threshold, it typically:

Allocates a bigger array (typically 2× the old size).
Rehashes everything—meaning it iterates over every element in the old array and reinserts it into the new array in one big operation.
This rehash step is O(N), where N is the total number of elements. If N is large (e.g., tens or hundreds of millions of entries), that rehash can take a significant amount of time. Because standard libraries assume relatively generic usage, they do the rehash as soon as it’s needed, all at once. This can be acceptable for programs where a stall of a few milliseconds (or even seconds) is tolerable. But in a service like Redis, which may run single-threaded and needs sub-millisecond latencies, blocking the entire server for a massive rehash can cause unavailability for clients.

### 1.2 Why This Matters at Large Scale:

Modern servers can easily have hundreds of gigabytes of RAM, which can hold very large hash tables. Even if rehashing a smaller dataset takes just milliseconds or seconds, rehashing a massive in-memory structure could stall the process for unacceptably long periods. A single operation that goes from microseconds to seconds can bring down the overall quality of service, causing timeouts and disconnects for many clients.

### 1.3 How Progressive Resizing Works:

Instead of rehashing everything all at once:

Allocate a new, larger table in the background but do not immediately move all entries.
Move a small portion of entries from the old table to the new table on each subsequent hash table operation. For example, whenever you do an insert, a lookup, or a delete, you also move a fixed number (say, a few hundred or thousand) of entries.
Check both tables during lookups until all elements have migrated. Once the old table is fully transferred, deallocate it.
This approach spreads the rehash cost across many smaller increments. Each increment is O(k) work, where k is the small batch of items moved, rather than O(N) in one shot. So, any individual operation may be slightly slower than normal during rehashing, but you avoid the catastrophic multi-second stall.

### 1.4 Trade-Off: Higher Average Overhead but Lower Worst-Case:

Pros:

Eliminates the huge stall caused by a single monolithic O(N) rehash.
Ensures the system remains responsive, maintaining strict latency requirements.

Cons:

During the resizing phase, each operation may have a small overhead because you have to check or move items incrementally.
The code is more complex: you need logic for “migrate a few entries on each operation” and a strategy for lookups across two tables.
From a worst-case latency perspective, this is a huge win. The worst-case cost is bounded by the small incremental batch size, rather than proportional to the total number of items N.

### 1.5 Why Zero-Initialization Matters:

Hash tables typically store metadata in each slot (for instance, whether the slot is occupied, a pointer to a node, or some other sentinel to indicate empty). If these need to be set to zero initially, performing a manual memset() on a multi-gigabyte array is expensive and immediate. For enormous tables, this initialization itself can stall the thread for a significant time.

### 1.6 The Difference Between malloc() and calloc():

malloc(size)

Allocates a block of memory of size bytes but does not clear or zero it.
If you want the memory zeroed, you’d typically do memset(ptr, 0, size), which forces an immediate O(size) pass over that memory.

calloc(n, elem_size)

Allocates space for n elements, each of elem_size bytes, and initializes all of it to zero.
For large allocations, this can be done with the help of the OS in a way that defers the cost of zeroing until pages are actually accessed.

### 1.7 How Large Allocations Use mmap():

On Linux and similar Unix-like systems, when you calloc() a large block of memory (bigger than a threshold determined by the libc memory allocator), the runtime will typically use mmap() under the hood. The steps conceptually look like this:

### 1.7.1 mmap() calls the kernel like this:

void *ptr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

This reserves a range of virtual address space and marks it as readable and writable, but doesn’t physically allocate pages yet (beyond minimal bookkeeping).

### 1.7.2 Zero pages with a copy-on-write strategy:

The kernel often uses a special “zero page” that is mapped copy-on-write into the process’ address space. This means that until the process actually writes to a page in that region, the physical memory isn’t allocated, and the page is conceptually “all zeros” from the process’s perspective.

### 1.7.3 calloc() ensures it is “logically zeroed”:

Because the memory is mapped to zeroed pages, any read from that region sees zeros. The kernel defers real physical memory allocation (and any actual zeroing of pages) until the program writes to that page. This is referred to as demand paging.

Thus, with a large calloc(), you don’t pay an immediate O(size) cost to zero out memory. It’s “backed” by a zero page until you touch it. As you progressively migrate keys or otherwise access the new table, individual pages become physically allocated and zero-initialized—but in small increments rather than all at once.

### 4. Allocations Below the Threshold: Immediate Zeroing:

For smaller allocations, the C library typically uses memory from the heap (often managed via brk() or small internal free lists) rather than calling mmap() directly. In these cases, calloc() usually must explicitly write zeros for the entire size. While that can still be an O(N) pass, it’s generally acceptable because “small” in this context is often just kilobytes or a few megabytes, not gigabytes. The threshold can vary (it might be on the order of tens or hundreds of kilobytes in glibc or other implementations), but the principle is:

If it’s below a certain size, the runtime can zero out the memory quickly, and you pay that cost immediately.

If it’s above that size, the runtime likely switches to mmap(), enabling demand paging.

## Generic Collections in C:

## 1. Use void* pointers:

- Pros:
    - Simple to code
- Cons:
    - Double indirection to access the data, first we have to access the void pointer and then the data.
    - No compile time checking

## 2. Generate code with C macros and code generators:

C++ templates generate per-type code, which can be emulated with C macros.
  ~~~cpp
  #define DEFINE_NODE(T) struct Node_ ## T {
      T data;
      struct Node_ ## T *next;
  }
  ~~~
-> DEFINE_NODE(T):x
  ~~~cpp
  struct Node_int {
    int data;
    struct Node_int *next;
  }
  ~~~
- this is very hard to debug and maintain, so not really useful.

## 3. Intrusive Data Structures:

### What we do usually:
  ~~~cpp
  template <class T>
  struct Node {
    T data;
    struct Node *next;
  }
  ~~~

### What we can do usually:
  ~~~cpp
  struct Node {
    struct Node *next;
  }

  struct MyData {
    int data;
    Node node; // embedded node structure in data struct
    // more data
  }
  ~~~

### How is this generic?
  ~~~cpp
  size_t list_size (struct Node *node) {
    size_t cnt = 0;
    for (; node != NULL; node = node -> next) {
      cnt++;
    }
    return cnt;
  }
~~~
  list_size() touches no data, it just works on the node structs.

### How do we get the data back?

  Just offset the address of the struct. Suppose you have a pointer *pnode to the node struct;
  ~~~cpp
  Node *pnode = some_ds_code();
  MyData *pdata = (MyData *)((char *)pnode - offsetof(MyData, node));
  ~~~
### Make this less verbose and error-prone with a macro:

  ~~~cpp
  #define container_of(ptr, T, member) ((T *)( (char *)ptr - offsetof(T, member) ))
  MyData *pdata = container_of(pnode, MyData, node);
  ~~~

  In C, `offsetof(MyData, node)` gives us the byte offset of node within MyData.



#### Footnote - This readme is mostly for me to keep track of what I am doing, why and how.
