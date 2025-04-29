#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <vector>
#include "common.h"
#include "tcp_protocol.h"
#include "utils.h"

void handle_stdin(int sockfd_tcp) {
    std::string line;
    std::getline(std::cin, line);

    std::string command;
    std::string topic;

    std::istringstream iss(line);
    iss >> command;

    if (command == "exit") {
        close(sockfd_tcp);
        close(STDIN_FILENO);
        exit(EXIT_SUCCESS);
    }

    if (command == "subscribe" || command == "unsubscribe") {
        iss >> topic;
        if (topic.empty()) {
            // std::cerr << "Topic is required for subscribe/unsubscribe
            // command"
            //           << std::endl;
            return;
        }
        // command and topic already read: topic may already be in variable
        // Prepare subscription or unsubscription message
        MsgSubscription msg;
        uint32_t total_len = sizeof(msg) + topic.size();
        msg.header.len = htonl(total_len);
        msg.header.type = (command == "subscribe" ? MSG_TYPE_SUBSCRIBE
                                                  : MSG_TYPE_UNSUBSCRIBE);
        msg.topic_len = htons(topic.size());
        // Send header and topic_len
        int send_status = send_all(sockfd_tcp, &msg, sizeof(msg));
        DIE(send_status < 0, "Failed to send subscription message");
        // Send topic string
        send_status = send_all(sockfd_tcp, const_cast<char*>(topic.c_str()),
                               topic.size());
        DIE(send_status < 0, "Failed to send subscription message");

        if (command == "subscribe") {
            std::cout << "Subscribed to topic " << topic << std::endl;
        } else {
            std::cout << "Unsubscribed from topic " << topic << std::endl;
        }
    }
}

void handle_tcp(int sockfd_tcp) {
    MsgUDPForward msg_udp_forward;
    ssize_t recv_result =
        recv_all(sockfd_tcp, &msg_udp_forward, sizeof(msg_udp_forward));
    if (recv_result <= 0) {
        // std::cerr << "Server disconnected." << std::endl;
        close(sockfd_tcp);
        exit(EXIT_FAILURE);
    }

    uint16_t topic_len = ntohs(msg_udp_forward.topic_len);
    std::vector<char> topic_buffer(topic_len + 1,
                                   '\0');  // +1 for null terminator

    recv_result = recv_all(sockfd_tcp, topic_buffer.data(), topic_len);
    if (recv_result <= 0) {
        // std::cerr << "Error receiving topic from server." << std::endl;
        return;
    }

    topic_buffer[topic_len] = '\0';
    std::string topic_str(topic_buffer.data());

    uint16_t content_len = ntohs(msg_udp_forward.content_len);
    std::vector<char> content(content_len);
    if (content_len > 0) {
        recv_result = recv_all(sockfd_tcp, content.data(), content_len);
        if (recv_result <= 0) {
            // std::cerr << "Error receiving content from server." << std::endl;
            return;
        }
    }

    std::string type_str;
    std::string value_str;
    bool format_success = format_udp_content(msg_udp_forward.data_type, content,
                                             type_str, value_str);

    if (format_success) {
        std::cout << inet_ntoa(*(struct in_addr*)&msg_udp_forward.sender_ip)
                  << ":" << ntohs(msg_udp_forward.sender_port) << " - "
                  << topic_str << " - " << type_str << " - " << value_str
                  << std::endl;
    } else {
        // std::cerr << "Failed to format UDP message" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 4) {
        // std::cerr << "Usage: " << argv[0] << " <client_id> <IP> <PORT>"
        //           << std::endl;
        exit(EXIT_FAILURE);
    }

    char* client_id = argv[1];
    int id_len = strlen(client_id);
    if (id_len > 10) {
        // std::cerr << "Client ID must be less than 10 characters" <<
        // std::endl;
        exit(EXIT_FAILURE);
    }

    char* IP = argv[2];  // IP address as a string (e.g. 192.168.10.10)
    uint16_t PORT;
    int rc = sscanf(argv[3], "%hu", &PORT);
    DIE(rc != 1, "Given port is invalid");

    int sockfd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd_tcp < 0, "socket TCP creation failed");

    // Set TCP_NODELAY option to disable Nagle's algorithm
    int enable = 1;
    int result = setsockopt(sockfd_tcp, IPPROTO_TCP, TCP_NODELAY,
                            (char*)&enable, sizeof(int));
    DIE(result < 0, "setsockopt TCP_NODELAY failed");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(IP);
    server_addr.sin_port = htons(PORT);

    int connect_status = connect(sockfd_tcp, (struct sockaddr*)&server_addr,
                                 sizeof(server_addr));
    DIE(connect_status < 0, "TCP connection failed");

    // Send client ID
    MsgClientID msg_client_id;
    msg_client_id.header.len = htonl(sizeof(msg_client_id));
    msg_client_id.header.type = MSG_TYPE_CLIENT_ID;
    memset(msg_client_id.id, 0, sizeof(msg_client_id.id));
    memcpy(msg_client_id.id, client_id, id_len);
    int send_status =
        send_all(sockfd_tcp, &msg_client_id, sizeof(msg_client_id));
    DIE(send_status < 0, "Failed to send client ID");

    struct pollfd fds[2];

    // STDIN
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    // TCP socket
    fds[1].fd = sockfd_tcp;
    fds[1].events = POLLIN | POLLERR | POLLHUP;
    fds[1].revents = 0;

    while (true) {
        DIE(poll(fds, 2, -1) < 0, "poll failed");

        if (fds[0].revents & POLLIN) {
            handle_stdin(sockfd_tcp);
        }

        if (fds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            if (fds[1].revents & (POLLERR | POLLHUP)) {
                // std::cerr << "Server disconnected." << std::endl;
                close(sockfd_tcp);
                break;
            }

            if (fds[1].revents & POLLIN) {
                handle_tcp(sockfd_tcp);
            }
        }
    }
    close(sockfd_tcp);
    return 0;
}
