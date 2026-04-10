#include "utils.h"
#include "message.pb.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <string>

static bool send_all(int sockfd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sockfd, p + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

static bool recv_all(int sockfd, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t recvd = 0;
    while (recvd < len) {
        ssize_t n = recv(sockfd, p + recvd, len - recvd, 0);
        if (n <= 0) return false;
        recvd += (size_t)n;
    }
    return true;
}

int listening_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(sockfd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 128) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int connect_socket(const char *hostname, const int port) {
    hostent *he = gethostbyname(hostname);
    if (!he) return -1;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    std::memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    addr.sin_port = htons((uint16_t)port);

    if (connect(sockfd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int accept_connection(int sockfd) {
    int client_fd = accept(sockfd, nullptr, nullptr);
    if (client_fd < 0) return -1;
    return client_fd;
}

int recv_msg(int sockfd, int32_t *operation_type, int64_t *argument) {
    if (!operation_type || !argument) return 1;

    uint32_t net_len = 0;
    if (!recv_all(sockfd, &net_len, sizeof(net_len))) return 1;
    uint32_t len = ntohl(net_len);
    if (len == 0) return 1;

    std::string payload(len, '\0');
    if (!recv_all(sockfd, &payload[0], len)) return 1;

    sockets::message msg;
    if (!msg.ParseFromString(payload)) return 1;

    *operation_type = (int32_t)msg.type();
    if (msg.has_argument())
        *argument = msg.argument();
    else
        *argument = 0;

    return 0;
}

int send_msg(int sockfd, int32_t operation_type, int64_t argument) {
    sockets::message msg;

    sockets::message_OperationType t;
    if (operation_type == OPERATION_ADD) t = sockets::message_OperationType_ADD;
    else if (operation_type == OPERATION_SUB) t = sockets::message_OperationType_SUB;
    else if (operation_type == OPERATION_TERMINATION) t = sockets::message_OperationType_TERMINATION;
    else if (operation_type == OPERATION_COUNTER) t = sockets::message_OperationType_COUNTER;
    else return 1;

    msg.set_type(t);
    msg.set_argument(argument);

    std::string payload;
    if (!msg.SerializeToString(&payload)) return 1;

    uint32_t len = (uint32_t)payload.size();
    uint32_t net_len = htonl(len);

    if (!send_all(sockfd, &net_len, sizeof(net_len))) return 1;
    if (!send_all(sockfd, payload.data(), payload.size())) return 1;

    return 0;
}
