# Client-Server TCP and UDP Message Management System (2nd Homework Communication Networks class)

## Overview

This project represents a client-server application designed for message management using TCP and UDP protocols. The system allows TCP clients to subscribe to topics and receive messages published by UDP clients on those topics, with support for wildcard subscriptions.

## System Architecture

The system consists of three main components:

1. **Server**: Acts as a broker between UDP publishers and TCP subscribers
2. **TCP Clients**: Connect to the server, subscribe to topics, and receive messages
3. **UDP Clients**: Publish messages to topics through the server

## Protocol Design

### TCP Protocol

The TCP protocol is designed to handle client identification, subscriptions, and message forwarding. All messages follow a common header structure:

```cpp
struct MsgHeader {
    uint32_t len;  // Total message length in network byte order
    uint8_t type;  // Message type
};
```

#### Message Types

1. **Client ID Message**
   ```cpp
   struct MsgClientID {
       MsgHeader header;
       char id[10];  // Null-terminated client ID
   };
   ```

2. **Subscription/Unsubscription Message**
   ```cpp
   struct MsgSubscription {
       MsgHeader header;
       uint16_t topic_len;  // Topic length in network byte order
       // Followed by topic string (not null-terminated)
   };
   ```

3. **UDP Forward Message**
   ```cpp
   struct MsgUDPForward {
       MsgHeader header;
       uint32_t sender_ip;    // Sender's IP address
       uint16_t sender_port;  // Sender's port in network byte order
       uint16_t topic_len;    // Topic length in network byte order
       uint8_t data_type;     // Type of data (0=INT, 1=SHORT_REAL, 2=FLOAT, 3=STRING)
       uint16_t content_len;  // Content length in network byte order
       // Followed by topic string and content
   };
   ```

### UDP Protocol

UDP clients send messages in the following format:

```
| Topic (50 bytes) | Data Type (1 byte) | Content (variable) |
```

- **Topic**: Fixed 50-byte field, null-terminated for shorter topics
- **Data Type**: 1 byte indicating the content type (0=INT, 1=SHORT_REAL, 2=FLOAT, 3=STRING)
- **Content**: Variable length data formatted according to the data type

#### Data Types

1. **INT (0)**: Sign byte followed by a 32-bit integer in network byte order
2. **SHORT_REAL (1)**: 16-bit unsigned integer representing the number multiplied by 100
3. **FLOAT (2)**: Sign byte, followed by a 32-bit integer, followed by an 8-bit power of 10
4. **STRING (3)**: Null-terminated string

## Wildcard Subscription Implementation

The system supports two types of wildcards in topic subscriptions:

1. **Single-level wildcard (+)**: Matches exactly one level in a topic hierarchy
2. **Multi-level wildcard (*)**: Matches zero or more levels in a topic hierarchy

Wildcard matching is implemented using regular expressions:

```cpp
std::string subscription_to_regex(const std::string& subscription) {
    std::string regex_pattern = "^"; // Start anchor
    
    for (size_t i = 0; i < subscription.length(); i++) {
        char c = subscription[i];
        
        if (c == '+') {
            // '+' matches exactly one level (any characters except '/')
            regex_pattern += "([^/]+)";
        } else if (c == '*') {
            // '*' matches zero or more levels (any characters including '/')
            regex_pattern += "(.*)";
        } else if (c == '/' || c == '.' || c == '^' || c == '$' || c == '|' || 
                  c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || 
                  c == '}' || c == '\\' || c == '?' || c == '+') {
            // Escape regex special characters
            regex_pattern += '\\';
            regex_pattern += c;
        } else {
            // Regular character
            regex_pattern += c;
        }
    }
    
    regex_pattern += "$"; // End anchor
    return regex_pattern;
}
```

A regex cache is used to avoid recompiling the same patterns repeatedly:

```cpp
std::unordered_map<std::string, std::regex> regex_cache;
```

To figure this out, I have used [regex101](https://regex101.com/) with the following tests:
```
upb/precis/*/altceva/
upb/precis/ec101/ec101a/altceva/blabla/altceva/
upb/precis/ec101/altceva/
upb/precis/ec101/ec101a/altceva/blabla/altceva/oricealtceva/blablabla
upb/precis/ceva/chestii/
upb/precis/*/altceva/
```

With these as the regex patterns:
```
upb\/precis\/([a-zA-Z0-9\/]*)\/altceva\/([a-zA-Z0-9\/]*)\/blablabla
```

So basically we are escaping all the chaacters, and then replacing `*` with `([a-zA-Z0-9\/]*)`
and `+` with `([a-zA-Z0-9]+)`. This was further simplified to the ones that are implemented in the code `(.*)` and `([^/]+)`.

## Server Implementation

The server uses the `poll()` system call to multiplex I/O operations across multiple file descriptors:

1. Standard input (for the `exit` command)
2. TCP listening socket (for new client connections)
3. UDP socket (for incoming messages from UDP clients)
4. Connected TCP client sockets

### Key Data Structures

```cpp
std::unordered_map<int, Client> clients;  // Maps socket fd to client info
std::unordered_map<std::string, int> client_ids;  // Maps client ID to socket fd
std::unordered_map<std::string, std::unordered_set<std::string>> client_subscriptions;  // Persistent subscriptions
```

### Client Reconnection

When a client disconnects, its subscriptions are saved in the `client_subscriptions` map. When the client reconnects with the same ID, its subscriptions are restored.

### UDP Message Forwarding

When a UDP message is received, the server:
1. Extracts the topic, data type, and content
2. Determines which TCP clients are subscribed to the topic
3. Forwards the message to those clients

## TCP Client Implementation

The TCP client:
1. Connects to the server with a unique ID
2. Sends subscription/unsubscription messages based on user commands
3. Receives and displays messages forwarded by the server

## Compilation

The project can be compiled using the provided Makefile:
```bash
make # Compiles the server & TCP client
```

## Usage

### Server

```
./server <PORT>
```

- `PORT`: The port number on which the server will listen

### TCP Client

```
./subscriber <CLIENT_ID> <SERVER_IP> <SERVER_PORT>
```

- `CLIENT_ID`: A unique identifier for the client (max 10 characters)
- `SERVER_IP`: The IP address of the server
- `SERVER_PORT`: The port number of the server

#### Commands

- `subscribe <TOPIC>`: Subscribe to a topic (wildcards supported)
- `unsubscribe <TOPIC>`: Unsubscribe from a topic
- `exit`: Disconnect from the server and exit

### Wildcard Examples

- `subscribe UPB/+/temperature`: Subscribe to topics like `UPB/lab1/temperature` and `UPB/lab2/temperature`
- `subscribe UPB/lab1/*`: Subscribe to all topics under `UPB/lab1/`
- `subscribe */temperature`: Subscribe to all topics ending with `/temperature`

## Error Handling

The application implements robust error handling:
- Socket errors are checked and reported
- TCP connection failures are handled gracefully
- Invalid messages are detected and rejected
- Client disconnections are properly managed

## Performance Considerations

1. **Nagle's Algorithm**: Disabled using `TCP_NODELAY` to reduce latency
2. **Message Framing**: Implemented to handle TCP's stream-based nature
3. **Regex Caching**: Used to improve wildcard matching performance
4. **Efficient Memory Management**: Buffer sizes are optimized for the expected message sizes

## Testing

The implementation has been manually tested with:
- Multiple concurrent TCP clients
- High-frequency UDP message publishing
- Various wildcard subscription patterns
- Client disconnection and reconnection scenarios

It was also tested by using the `test.py` file provided by the Network Communications (PCOM) team.

## Conclusion

This client-server application provides a robust platform for message management using TCP and UDP protocols. The implementation supports wildcard subscriptions, client reconnection, and efficient message forwarding, making it suitable for various messaging applications.