#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <string>

#define MSG_TYPE_CLIENT_ID 1
#define MSG_TYPE_SUBSCRIBE 2
#define MSG_TYPE_UNSUBSCRIBE 3
#define MSG_TYPE_FORWARD_UDP 4

#pragma pack(push, 1)

struct TcpHeader {
    uint32_t len;  // Total message length including header and payload
    uint8_t type;  // Message type
};

// Client ID message
struct MsgClientID {
    TcpHeader header;  // type = MSG_TYPE_CLIENT_ID
    char id[10];  // Fixed size client ID (max 10 chars as per requirements)
};

// Subscribe/Unsubscribe message
struct MsgSubscription {
    TcpHeader header;    // type = MSG_TYPE_SUBSCRIBE or MSG_TYPE_UNSUBSCRIBE
    uint16_t topic_len;  // Length of the topic string
    // Topic string follows (variable length)
};

// UDP message forwarding structure
struct MsgUDPForward {
    TcpHeader header;      // type = MSG_TYPE_FORWARD_UDP
    uint32_t sender_ip;    // IP address in network byte order
    uint16_t sender_port;  // Port in network byte order
    uint16_t topic_len;    // Length of the topic string
    uint8_t data_type;     // 0=INT, 1=SHORT_REAL, 2=FLOAT, 3=STRING
    uint16_t content_len;  // Length of the content
};

#pragma pack(pop)

int send_all(int sockfd, void* buffer, size_t len);
int recv_all(int sockfd, void* buffer, size_t len);