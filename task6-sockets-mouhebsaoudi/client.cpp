#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <cstdlib>
#include <unistd.h>

#include "utils.h"

std::mutex print_mutex;

void client_thread(
    const std::string hostname,
    int port,
    int numMessages,
    int add,
    int sub
) {
    int sockfd = connect_socket(hostname.c_str(), port);
    if (sockfd < 0) {
        return;
    }

    for (int i = 0; i < numMessages; i++) {
        int32_t op;
        int64_t arg;

        if (i % 2 == 0) {
            op = OPERATION_ADD;
            arg = add;
        } else {
            op = OPERATION_SUB;
            arg = sub;
        }

        if (send_msg(sockfd, op, arg) != 0) {
            close(sockfd);
            return;
        }
    }

    if (send_msg(sockfd, OPERATION_TERMINATION, 0) != 0) {
        close(sockfd);
        return;
    }

    int32_t resp_op = 0;
    int64_t resp_arg = 0;
    if (recv_msg(sockfd, &resp_op, &resp_arg) != 0) {
        close(sockfd);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << resp_arg << std::endl;
    }

    close(sockfd);
}

int main(int args, char *argv[]) {
    if (args < 7) {
        std::cerr << "usage: ./client <num_threads> <hostname> <port> "
                     "<num_messages> <add> <sub>\n";
        return 1;
    }

    int numClients   = std::atoi(argv[1]);
    std::string hostname = argv[2];
    int port         = std::atoi(argv[3]);
    int numMessages  = std::atoi(argv[4]);
    int add          = std::atoi(argv[5]);
    int sub          = std::atoi(argv[6]);

    if (numClients <= 0) {
        return 0;
    }

    std::vector<std::thread> threads;
    threads.reserve(numClients);

    for (int i = 0; i < numClients; i++) {
        threads.emplace_back(
            client_thread,
            hostname,
            port,
            numMessages,
            add,
            sub
        );
    }

    for (int i = 0; i < numClients; i++) {
        threads[i].join();
    }

    return 0;
}
