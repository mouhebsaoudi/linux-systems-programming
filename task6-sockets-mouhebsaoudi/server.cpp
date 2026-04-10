#include <atomic>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <poll.h>
#include <unistd.h>
#include <cstdlib>

#include "utils.h"

std::atomic<int64_t> number{0};
std::mutex print_mutex;

struct Worker {
    std::mutex mtx;
    std::vector<int> sockets;
};

void worker_loop(Worker *worker) {
    while (true) {
        std::vector<int> fds;
        {
            std::lock_guard<std::mutex> lock(worker->mtx);
            fds = worker->sockets;
        }

        if (fds.empty()) {
            usleep(10000);
            continue;
        }

        std::vector<pollfd> poll_fds(fds.size());
        for (size_t i = 0; i < fds.size(); i++) {
            poll_fds[i].fd = fds[i];
            poll_fds[i].events = POLLIN;
            poll_fds[i].revents = 0;
        }

        int ret = poll(poll_fds.data(), poll_fds.size(), 100);
        if (ret <= 0) {
            continue;
        }

        std::vector<int> closed;

        for (size_t i = 0; i < poll_fds.size(); i++) {
            if (!(poll_fds[i].revents & POLLIN)) {
                continue;
            }

            int sockfd = poll_fds[i].fd;
            int32_t op = 0;
            int64_t arg = 0;

            if (recv_msg(sockfd, &op, &arg) != 0) {
                closed.push_back(sockfd);
                close(sockfd);
                continue;
            }

            if (op == OPERATION_ADD) {
                number.fetch_add(arg);
            } else if (op == OPERATION_SUB) {
                number.fetch_sub(arg);
            } else if (op == OPERATION_TERMINATION) {
                int64_t value = number.load();
                send_msg(sockfd, OPERATION_COUNTER, value);

                {
                    std::lock_guard<std::mutex> lock(print_mutex);
                    std::cout << value << std::endl;
                }

                closed.push_back(sockfd);
                close(sockfd);
            }
        }

        if (!closed.empty()) {
            std::lock_guard<std::mutex> lock(worker->mtx);
            auto &vec = worker->sockets;
            for (int fd : closed) {
                for (auto it = vec.begin(); it != vec.end(); ) {
                    if (*it == fd) {
                        it = vec.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
    }
}

int main(int args, char *argv[]) {
    if (args < 3) {
        std::cerr << "usage: ./server <numThreads> <port>\n";
        return 1;
    }

    int numThreads = std::atoi(argv[1]);
    int port = std::atoi(argv[2]);

    if (numThreads <= 0) {
        return 1;
    }

    int listen_fd = listening_socket(port);
    if (listen_fd < 0) {
        std::cerr << "failed to create listening socket\n";
        return 1;
    }

    std::vector<Worker> workers(numThreads);
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back(worker_loop, &workers[i]);
    }

    int connection_count = 0;

    while (true) {
        int client_fd = accept_connection(listen_fd);
        if (client_fd < 0) {
            continue;
        }

        int idx = connection_count % numThreads;
        connection_count++;

        Worker &w = workers[idx];
        {
            std::lock_guard<std::mutex> lock(w.mtx);
            w.sockets.push_back(client_fd);
        }
    }

    close(listen_fd);
    return 0;
}
