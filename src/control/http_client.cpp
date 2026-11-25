#include "control/http_client.hpp"

#include "control/tcp_socket.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <vector>

namespace nvj {

HttpExchange httpExchange(int port, const std::string& request, int timeoutMs, bool halfClose) {
    HttpExchange out;
    std::string error;
    std::unique_ptr<TcpConnection> connection = tcpConnectLoopback(port, error);
    if (!connection) {
        out.status = -1;
        return out;
    }
    size_t sent = 0;
    bool shut = false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < until) {
        std::vector<TcpPollEntry> entries(1);
        entries[0].connection = connection.get();
        entries[0].wantWrite = sent < request.size();
        tcpPoll(entries, 20);
        if (sent < request.size()) {
            const int wrote = connection->send(request.data() + sent, static_cast<int>(std::min<size_t>(request.size() - sent, 8192)));
            if (wrote > 0) {
                sent += static_cast<size_t>(wrote);
            } else if (wrote < 0) {
                sent = request.size();
            }
        } else if (halfClose && !shut) {
            connection->shutdownWrite();
            shut = true;
        }
        char buffer[8192];
        const int got = connection->receive(buffer, sizeof(buffer));
        if (got > 0) {
            out.text.append(buffer, static_cast<size_t>(got));
        } else if (got < 0) {
            out.closed = true;
            break;
        }
    }
    if (out.text.compare(0, 9, "HTTP/1.1 ") == 0 && out.text.size() >= 12) {
        out.status = std::atoi(out.text.substr(9, 3).c_str());
        const size_t split = out.text.find("\r\n\r\n");
        if (split != std::string::npos) {
            out.body = out.text.substr(split + 4);
        }
    }
    return out;
}

}
