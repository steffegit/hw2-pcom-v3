#include <arpa/inet.h>
#include <math.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "client.h"
#include "include/common.h"
#include "include/tcp_protocol.h"
#include "include/utils.h"

void initialize_server(int port, int& listenfd_tcp, int& sockfd_udp) {
    listenfd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    DIE(listenfd_tcp < 0, "socket creation failed for TCP");

    sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(sockfd_udp < 0, "socket creation failed for UDP");

    int enable = 1;
    DIE(setsockopt(listenfd_tcp, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) < 0,
        "setsockopt(SO_REUSEADDR) failed");

    DIE(setsockopt(sockfd_udp, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) < 0,
        "setsockopt(SO_REUSEADDR) failed");

    // Disable Nagle's algorithm for TCP socket
    DIE(setsockopt(listenfd_tcp, IPPROTO_TCP, TCP_NODELAY, (char*)&enable,
                   sizeof(int)) < 0,
        "setsockopt TCP_NODELAY failed");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    DIE(bind(listenfd_tcp, (struct sockaddr*)&server_addr,
             sizeof(server_addr)) < 0,
        "bind failed");

    DIE(bind(sockfd_udp, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
            0,
        "bind failed");

    DIE(listen(listenfd_tcp, SOMAXCONN) < 0, "listen failed");
}

bool handle_stdin_command() {
    std::string command;
    if (std::getline(std::cin, command)) {
        if (!command.empty() && command.back() == '\n') {
            command.pop_back();
        }
        if (command == "exit") {
            return true;
        }
    }
    return false;
}

void handle_client_disconnect(
    int clientfd,
    std::unordered_map<int, Client>& clients,
    std::unordered_map<std::string, int>& client_ids,
    std::unordered_map<std::string, std::unordered_set<std::string>>&
        client_subscriptions) {
    std::string client_id = clients[clientfd].id;
    std::cout << "Client " << client_id << " disconnected." << std::endl;

    // Save the client's subscriptions before removing from clients map
    client_subscriptions[client_id] =
        std::unordered_set<std::string>(clients[clientfd].subscriptions.begin(),
                                        clients[clientfd].subscriptions.end());

    // Remove from client_ids map to allow reconnection with same ID
    client_ids.erase(client_id);

    // Remove from clients map
    clients.erase(clientfd);
    close(clientfd);
}

// Convert a subscription pattern with wildcards to a regex pattern
std::string subscription_to_regex(const std::string& subscription) {
    std::string regex_pattern = "^";  // Start anchor

    // Process the subscription character by character
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

    regex_pattern += "$";  // End anchor
    return regex_pattern;
}

// Check if a topic matches a subscription pattern with wildcards
bool topic_matches(const std::string& subscription,
                   const std::string& topic,
                   std::unordered_map<std::string, std::regex>& regex_cache) {
    std::regex pattern;

    auto it = regex_cache.find(subscription);
    if (it != regex_cache.end()) {
        pattern = it->second;
    } else {
        std::string regex_pattern = subscription_to_regex(subscription);
        pattern = std::regex(regex_pattern);
        regex_cache[subscription] = pattern;
    }

    return std::regex_match(topic, pattern);
}

void handle_udp_forwarding(
    std::unordered_map<int, Client>& clients,
    std::unordered_map<std::string, int>& client_ids,
    const std::string& topic,
    uint8_t data_type,
    const std::vector<char>& content,
    uint32_t sender_ip,
    uint16_t sender_port,
    std::unordered_map<std::string, std::regex>& regex_cache) {
    for (const auto& client_pair : clients) {
        int clientfd = client_pair.first;
        const Client& client = client_pair.second;

        bool should_receive = false;
        for (const auto& subscription : client.subscriptions) {
            if (topic_matches(subscription, topic, regex_cache)) {
                should_receive = true;
                break;
            }
        }

        if (should_receive) {
            MsgUDPForward msg_udp_forward;
            uint32_t struct_size = sizeof(msg_udp_forward);
            uint32_t topic_size = topic.size();
            uint32_t content_size = content.size();
            msg_udp_forward.header.len =
                htonl(struct_size + topic_size + content_size);
            msg_udp_forward.header.type = MSG_TYPE_FORWARD_UDP;
            msg_udp_forward.sender_ip = sender_ip;
            msg_udp_forward.sender_port = sender_port;
            msg_udp_forward.topic_len = htons(topic_size);
            msg_udp_forward.data_type = data_type;
            msg_udp_forward.content_len = htons(content_size);

            std::vector<char> send_buf;
            send_buf.resize(struct_size + topic_size + content_size);

            memcpy(send_buf.data(), &msg_udp_forward, struct_size);
            memcpy(send_buf.data() + struct_size, topic.c_str(), topic_size);
            if (content_size > 0) {
                memcpy(send_buf.data() + struct_size + topic_size,
                       content.data(), content_size);
            }

            send_all(clientfd, send_buf.data(), send_buf.size());
        }
    }
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <PORT>" << std::endl;
        exit(EXIT_FAILURE);
    }
    int PORT = atoi(argv[1]);

    int listenfd_tcp, sockfd_udp;
    initialize_server(PORT, listenfd_tcp, sockfd_udp);

    std::vector<struct pollfd> pfds;
    pfds.push_back({.fd = STDIN_FILENO, .events = POLLIN, .revents = 0});
    pfds.push_back({.fd = listenfd_tcp, .events = POLLIN, .revents = 0});
    pfds.push_back({.fd = sockfd_udp, .events = POLLIN, .revents = 0});

    std::unordered_map<int, Client> clients;  // clients by socket fd
    std::unordered_map<std::string, int>
        client_ids;  // map client ID to socket fd
    std::unordered_map<std::string, std::unordered_set<std::string>>
        client_subscriptions;  // map client ID to subscriptions

    std::unordered_map<std::string, std::regex> regex_cache;

    while (true) {
        int poll_result = poll(pfds.data(), pfds.size(), -1);
        DIE(poll_result < 0, "poll failed");

        if (pfds[0].revents & POLLIN) {
            if (handle_stdin_command()) {
                break;
            }
        }

        if (pfds[1].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_sockfd =
                accept(listenfd_tcp, (struct sockaddr*)&client_addr, &addr_len);
            DIE(client_sockfd < 0, "accept failed");

            int enable = 1;
            int result = setsockopt(client_sockfd, IPPROTO_TCP, TCP_NODELAY,
                                    (char*)&enable, sizeof(int));
            DIE(result < 0, "setsockopt TCP_NODELAY failed");

            // Check if the client ID is present
            MsgClientID msg_client_id;
            DIE(recv_all(client_sockfd, &msg_client_id, sizeof(msg_client_id)) <
                    0,
                "recv_all MSG_CLIENT_ID failed");

            std::string client_id_str = std::string(msg_client_id.id);

            // Check if it's a duplicate client ID (already connected)
            if (client_ids.find(client_id_str) != client_ids.end()) {
                std::cout << "Client " << client_id_str << " already connected."
                          << std::endl;
                close(client_sockfd);
                continue;
            }

            // Store the client ID
            Client client;
            client.id = client_id_str;
            client.subscriptions.clear();

            // Restore subscriptions if this client has connected before
            if (client_subscriptions.find(client_id_str) !=
                client_subscriptions.end()) {
                for (const auto& topic : client_subscriptions[client_id_str]) {
                    client.subscriptions.insert(topic);
                }
            }

            clients[client_sockfd] = client;

            // Map the client ID to its socket fd
            client_ids[client_id_str] = client_sockfd;

            // Add the client socket to the poll list
            pfds.push_back(
                {.fd = client_sockfd, .events = POLLIN, .revents = 0});

            std::cout << "New client " << client.id << " connected from "
                      << inet_ntoa(client_addr.sin_addr) << ":"
                      << ntohs(client_addr.sin_port) << "." << std::endl;
        }

        if (pfds[2].revents & POLLIN) {
            char buffer[1552];  // 50 (topic) + 1 (data_type) + 1500 (max
                                // content) + 1 (null terminator)
            struct sockaddr_in udp_client_addr;
            socklen_t addr_len = sizeof(udp_client_addr);
            ssize_t bytes_received =
                recvfrom(sockfd_udp, buffer, sizeof(buffer), 0,
                         (struct sockaddr*)&udp_client_addr, &addr_len);
            DIE(bytes_received < 0, "recvfrom failed");

            // Extract topic (first 50 bytes, null-terminated)
            size_t actual_topic_len = strnlen(buffer, 50);
            std::string topic(buffer, actual_topic_len);

            // Extract data type (1 byte after topic)
            uint8_t data_type = static_cast<uint8_t>(buffer[50]);

            // Extract content (remaining bytes)
            int content_len =
                bytes_received - 51;  // 50 (topic) + 1 (data_type)
            std::vector<char> content(buffer + 51, buffer + 51 + content_len);

            // Forward the UDP message to subscribed clients
            handle_udp_forwarding(clients, client_ids, topic, data_type,
                                  content, udp_client_addr.sin_addr.s_addr,
                                  udp_client_addr.sin_port, regex_cache);
        }

        for (size_t i = 3; i < pfds.size(); i++) {
            if (pfds[i].revents & (POLLERR | POLLHUP)) {
                handle_client_disconnect(pfds[i].fd, clients, client_ids,
                                         client_subscriptions);
                pfds[i].fd = -1;  // Mark the fd as closed
                continue;
            }

            if (pfds[i].revents & POLLIN) {  // Incoming data from client
                int clientfd = pfds[i].fd;
                MsgSubscription msg_subscription;
                ssize_t recv_result = recv_all(clientfd, &msg_subscription,
                                               sizeof(msg_subscription));

                if (recv_result <= 0) {
                    handle_client_disconnect(clientfd, clients, client_ids,
                                             client_subscriptions);
                    pfds[i].fd = -1;  // Mark the fd as closed
                    continue;
                }

                uint16_t topic_len_host = ntohs(msg_subscription.topic_len);

                std::vector<char> topic_buffer(topic_len_host);

                if (recv_all(clientfd, topic_buffer.data(), topic_len_host) <=
                    0) {
                    handle_client_disconnect(clientfd, clients, client_ids,
                                             client_subscriptions);
                    pfds[i].fd = -1;  // Mark the fd as closed
                    continue;
                }

                std::string topic_str(topic_buffer.begin(), topic_buffer.end());

                if (msg_subscription.header.type == MSG_TYPE_SUBSCRIBE) {
                    clients[clientfd].subscriptions.insert(topic_str);
                    // Also update the persistent subscriptions map
                    client_subscriptions[clients[clientfd].id].insert(
                        topic_str);

                } else if (msg_subscription.header.type ==
                           MSG_TYPE_UNSUBSCRIBE) {
                    clients[clientfd].subscriptions.erase(topic_str);
                    // Also update the persistent subscriptions map
                    client_subscriptions[clients[clientfd].id].erase(topic_str);

                } else {
                    std::cerr << "Received unexpected message type from client "
                              << clients[clientfd].id << std::endl;
                    handle_client_disconnect(clientfd, clients, client_ids,
                                             client_subscriptions);
                    pfds[i].fd = -1;
                    continue;
                }
            }
        }

        // Clean up closed file descriptors from the poll array
        for (size_t i = pfds.size() - 1; i > 2; --i) {
            if (pfds[i].fd < 0) {
                pfds.erase(pfds.begin() + i);
            }
        }
    }

    for (const auto& client_pair : clients) {
        std::cout << "Closing connection to client " << client_pair.second.id
                  << std::endl;
        close(client_pair.first);
    }
    clients.clear();
    client_ids.clear();

    for (size_t i = 1; i < pfds.size(); ++i) {
        close(pfds[i].fd);
    }

    return 0;
}
