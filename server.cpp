#include <arpa/inet.h>
#include <math.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <unordered_map>
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

    // Reuse address & port for TCP socket
    int enable = 1;
    DIE(setsockopt(listenfd_tcp, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) < 0,
        "setsockopt(SO_REUSEADDR) failed");

    DIE(setsockopt(listenfd_tcp, SOL_SOCKET, SO_REUSEPORT, &enable,
                   sizeof(enable)) < 0,
        "setsockopt(SO_REUSEPORT) failed");

    // Reuse address & port for UDP socket
    DIE(setsockopt(sockfd_udp, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) < 0,
        "setsockopt(SO_REUSEADDR) failed");

    DIE(setsockopt(sockfd_udp, SOL_SOCKET, SO_REUSEPORT, &enable,
                   sizeof(enable)) < 0,
        "setsockopt(SO_REUSEPORT) failed");

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
    std::unordered_map<std::string, int>& client_ids) {
    std::string client_id = clients[clientfd].id;
    std::cout << "Client " << client_id << " disconnected." << std::endl;

    // Remove from client_ids map to allow reconnection with same ID
    client_ids.erase(client_id);

    // Remove from clients map
    clients.erase(clientfd);
    close(clientfd);
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
                std::cerr << "Client " << client_id_str << " already connected."
                          << std::endl;
                close(client_sockfd);
                continue;
            }

            // Store the client ID
            Client client;
            client.id = client_id_str;
            client.subscriptions.clear();
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

        for (size_t i = 3; i < pfds.size(); i++) {
            if (pfds[i].revents & (POLLERR | POLLHUP)) {
                handle_client_disconnect(pfds[i].fd, clients, client_ids);
                pfds[i].fd = -1;  // Mark the fd as closed
                continue;
            }

            if (pfds[i].revents & POLLIN) {  // Incoming data from client
                int clientfd = pfds[i].fd;
                MsgSubscription msg_subscription;
                ssize_t recv_result = recv_all(clientfd, &msg_subscription,
                                               sizeof(msg_subscription));

                if (recv_result <= 0) {
                    // Client disconnected or error occurred
                    handle_client_disconnect(clientfd, clients, client_ids);
                    pfds[i].fd = -1;  // Mark the fd as closed
                    continue;
                }

                char* topic = new char[msg_subscription.topic_len + 1];
                if (recv_all(clientfd, topic, msg_subscription.topic_len) <=
                    0) {
                    // Error receiving topic
                    delete[] topic;
                    handle_client_disconnect(clientfd, clients, client_ids);
                    pfds[i].fd = -1;  // Mark the fd as closed
                    continue;
                }

                topic[msg_subscription.topic_len] = '\0';

                // Store the subscription
                clients[clientfd].subscriptions.insert(topic);

                delete[] topic;
            }
        }

        // Clean up closed file descriptors from the poll array
        for (size_t i = pfds.size() - 1; i > 2; --i) {
            if (pfds[i].fd < 0) {
                pfds.erase(pfds.begin() + i);
            }
        }
    }

    for (size_t i = 1; i < pfds.size(); ++i) {
        close(pfds[i].fd);
    }

    return 0;
}
