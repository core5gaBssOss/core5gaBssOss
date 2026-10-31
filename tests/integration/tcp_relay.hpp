#pragma once

// A TCP relay between an NF and one of its datastores, so a test can take the datastore away
// after the NF has started (RULE-02). An NF that finds its store down at startup exits by design
// (the fail-fast startup rule), so pointing it at a closed port never reaches the runtime error
// path; starting it through a relay to the real store and then cut()-ing the relay does.
//
// cut() closes the listener and every relayed connection: the NF's open connection sees EOF or a
// reset, and its reconnects are refused, which is what a store that dies looks like to the NF.

#include <arpa/inet.h>
#include <atomic>
#include <cstdint>
#include <netinet/in.h>
#include <poll.h>
#include <stop_token>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace nf_test {

class TcpRelay {
public:
    // Listens on 127.0.0.1 on an ephemeral port and relays each connection to
    // target_ipv4:target_port. port() is 0 if the listener could not be opened.
    TcpRelay(std::string target_ipv4, std::uint16_t target_port)
        : target_ipv4_(std::move(target_ipv4)), target_port_(target_port) {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof a;
        if (listener_ < 0 || ::bind(listener_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 ||
            ::listen(listener_, 16) != 0 ||
            ::getsockname(listener_, reinterpret_cast<sockaddr*>(&a), &len) != 0) {
            close_fd(listener_);
            return;
        }
        port_ = ntohs(a.sin_port);
        worker_ = std::jthread([this](const std::stop_token& st) { run(st); });
    }

    TcpRelay(const TcpRelay&) = delete;
    TcpRelay& operator=(const TcpRelay&) = delete;
    ~TcpRelay() { cut(); }

    [[nodiscard]] std::uint16_t port() const { return port_; }

    // The store "dies": no new connections, every relayed one closed. Idempotent.
    void cut() {
        if (cut_.exchange(true)) {
            return;
        }
        worker_.request_stop();
        if (worker_.joinable()) {
            worker_.join();
        }
        close_fd(listener_);
        for (auto& [a, b] : pairs_) {
            close_fd(a);
            close_fd(b);
        }
        pairs_.clear();
    }

private:
    static void close_fd(int& fd) {
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
            fd = -1;
        }
    }

    int connect_target() const {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(target_port_);
        if (fd < 0 || ::inet_pton(AF_INET, target_ipv4_.c_str(), &a.sin_addr) != 1 ||
            ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
            if (fd >= 0) {
                ::close(fd);
            }
            return -1;
        }
        return fd;
    }

    // One thread, poll() over the listener and both ends of every pair; 100 ms ticks so a stop
    // request is seen promptly.
    void run(const std::stop_token& st) {
        char buf[16384];
        while (!st.stop_requested()) {
            std::vector<pollfd> fds{{listener_, POLLIN, 0}};
            for (const auto& [a, b] : pairs_) {
                fds.push_back({a, POLLIN, 0});
                fds.push_back({b, POLLIN, 0});
            }
            if (::poll(fds.data(), fds.size(), 100) <= 0) {
                continue;
            }
            if ((fds[0].revents & POLLIN) != 0) {
                int in = ::accept(listener_, nullptr, nullptr);
                int out = in >= 0 ? connect_target() : -1;
                if (out >= 0) {
                    pairs_.emplace_back(in, out);
                } else {
                    close_fd(in);
                }
            }
            for (std::size_t i = 1; i < fds.size(); ++i) {
                if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                    continue;
                }
                auto& [a, b] = pairs_[(i - 1) / 2];
                const int from = (i % 2 == 1) ? a : b;
                const int to = (i % 2 == 1) ? b : a;
                const ssize_t n = from >= 0 ? ::read(from, buf, sizeof buf) : 0;
                if (n <= 0 || ::send(to, buf, static_cast<std::size_t>(n), MSG_NOSIGNAL) != n) {
                    close_fd(a);
                    close_fd(b);
                }
            }
            std::erase_if(pairs_, [](const auto& p) { return p.first < 0; });
        }
    }

    std::string target_ipv4_;
    std::uint16_t target_port_;
    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> cut_{false};
    std::vector<std::pair<int, int>> pairs_; // owned by the worker until cut() has joined it
    std::jthread worker_;
};

} // namespace nf_test
