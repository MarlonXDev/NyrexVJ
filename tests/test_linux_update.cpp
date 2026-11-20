#include "check.hpp"

#include "platform/platform.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#define CHECK(expr) testing::check((expr), #expr, __LINE__)

namespace {

class MiniServer {
public:
    MiniServer() {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        socklen_t length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        listen(listener_, 8);
        thread_ = std::thread([this]() { serve(); });
    }

    ~MiniServer() {
        stop_.store(true);
        const int wake = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<uint16_t>(port_));
        connect(wake, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        close(wake);
        thread_.join();
        close(listener_);
    }

    std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
    int port() const { return port_; }

private:
    void serve() {
        while (!stop_.load()) {
            const int client = accept(listener_, nullptr, nullptr);
            if (client < 0) {
                continue;
            }
            if (stop_.load()) {
                close(client);
                break;
            }
            char buffer[4096];
            const ssize_t count = recv(client, buffer, sizeof(buffer) - 1, 0);
            std::string request = count > 0 ? std::string(buffer, static_cast<size_t>(count)) : std::string();
            std::string path;
            const size_t space = request.find(' ');
            if (space != std::string::npos) {
                const size_t end = request.find(' ', space + 1);
                path = request.substr(space + 1, end - space - 1);
            }
            std::string head;
            std::string body;
            if (path == "/data") {
                body.assign(300000, 'x');
                for (size_t i = 0; i < body.size(); ++i) {
                    body[i] = static_cast<char>('a' + (i % 26));
                }
                head = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
            } else if (path == "/redirect") {
                head = "HTTP/1.1 302 Found\r\nLocation: /data\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            } else if (path == "/loop") {
                head = "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            } else if (path == "/ua") {
                std::string agent;
                const size_t at = request.find("User-Agent: ");
                if (at != std::string::npos) {
                    const size_t end = request.find("\r\n", at);
                    agent = request.substr(at + 12, end - at - 12);
                }
                body = agent;
                head = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
            } else {
                body = "missing";
                head = "HTTP/1.1 404 Not Found\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
            }
            const std::string answer = head + body;
            size_t sent = 0;
            while (sent < answer.size()) {
                const ssize_t written = send(client, answer.data() + sent, answer.size() - sent, MSG_NOSIGNAL);
                if (written <= 0) {
                    break;
                }
                sent += static_cast<size_t>(written);
            }
            close(client);
        }
    }

    int listener_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void testMissingCurl() {
    nvj::platform::setCurlLibraryForTesting("/nonexistent/libcurl_missing.so.4");
    CHECK(!nvj::platform::updatesSupported());
    nvj::platform::HttpResult result;
    CHECK(!nvj::platform::httpGet("http://127.0.0.1:1/", result));
    CHECK(result.error.find("libcurl") != std::string::npos);
    nvj::platform::setCurlLibraryForTesting("");
}

void testDownload() {
    if (!nvj::platform::updatesSupported()) {
        std::printf("libcurl is not installed here, the download tests are skipped\n");
        return;
    }
    MiniServer server;
    {
        nvj::platform::HttpResult result;
        std::vector<size_t> seen;
        size_t lastTotal = 0;
        const bool ok = nvj::platform::httpGet(server.url("/data"), result, [&](size_t received, size_t total) {
            seen.push_back(received);
            lastTotal = total;
        });
        CHECK(ok);
        CHECK(result.status == 200);
        CHECK(result.body.size() == 300000);
        CHECK(result.error.empty());
        if (result.body.size() == 300000) {
            CHECK(result.body[0] == 'a' && result.body[25] == 'z' && result.body[26] == 'a');
        }
        CHECK(!seen.empty());
        CHECK(lastTotal == 300000);
        bool increasing = true;
        for (size_t i = 1; i < seen.size(); ++i) {
            increasing = increasing && seen[i] >= seen[i - 1];
        }
        CHECK(increasing);
        CHECK(seen.empty() || seen.back() == 300000);
    }
    {
        nvj::platform::HttpResult result;
        CHECK(nvj::platform::httpGet(server.url("/redirect"), result));
        CHECK(result.body.size() == 300000);
    }
    {
        nvj::platform::HttpResult result;
        CHECK(!nvj::platform::httpGet(server.url("/nothing"), result));
        CHECK(result.status == 404);
        CHECK(result.error == "HTTP 404");
    }
    {
        nvj::platform::HttpResult result;
        CHECK(!nvj::platform::httpGet(server.url("/loop"), result));
        CHECK(!result.error.empty());
    }
    {
        nvj::platform::HttpResult result;
        CHECK(nvj::platform::httpGet(server.url("/ua"), result));
        CHECK(std::string(result.body.begin(), result.body.end()) == "NyrexVJ-Updater");
    }
    {
        nvj::platform::HttpResult result;
        CHECK(!nvj::platform::httpGet("file:///etc/hostname", result));
        CHECK(!result.error.empty());
        CHECK(result.body.empty());
    }
    {
        int closedPort = 0;
        {
            MiniServer temporary;
            closedPort = temporary.port();
        }
        nvj::platform::HttpResult result;
        CHECK(!nvj::platform::httpGet("http://127.0.0.1:" + std::to_string(closedPort) + "/", result));
        CHECK(!result.error.empty());
    }
}

void testReplace() {
    const std::filesystem::path base = std::filesystem::temp_directory_path() / ("nvj_update_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(base);
    const std::string exe = (base / "NyrexVJ-linux-x64").string();
    {
        std::ofstream out(exe, std::ios::binary);
        out << "old program";
    }
    chmod(exe.c_str(), 0750);
    const std::vector<unsigned char> binary = {0x7F, 'E', 'L', 'F', 1, 2, 3, 4};
    nvj::platform::ReplaceMode mode = nvj::platform::ReplaceMode::Deferred;
    std::string error;
    CHECK(nvj::platform::replaceExecutable(exe, binary, mode, error));
    CHECK(mode == nvj::platform::ReplaceMode::InPlace);
    CHECK(error.empty());
    const std::string content = readFile(exe);
    CHECK(content.size() == binary.size());
    CHECK(content.size() == binary.size() && std::memcmp(content.data(), binary.data(), binary.size()) == 0);
    struct stat info{};
    CHECK(stat(exe.c_str(), &info) == 0);
    CHECK((info.st_mode & 0111) != 0);
    CHECK((info.st_mode & 0777) == 0751);
    CHECK(!std::filesystem::exists(exe + ".new"));

    std::ofstream(exe + ".old") << "x";
    std::ofstream(exe + ".new") << "x";
    nvj::platform::removeStaleFiles(exe);
    CHECK(!std::filesystem::exists(exe + ".old"));
    CHECK(!std::filesystem::exists(exe + ".new"));
    CHECK(std::filesystem::exists(exe));

    CHECK(!nvj::platform::replaceExecutable((base / "missing_dir" / "program").string(), binary, mode, error));
    CHECK(error.find("Cannot write") != std::string::npos);
    CHECK(!nvj::platform::replaceExecutable(exe, {}, mode, error));
    CHECK(readFile(exe).size() == binary.size());

    std::filesystem::remove_all(base);
}

void testExePath() {
    const std::string path = nvj::platform::currentExePath();
    CHECK(!path.empty());
    CHECK(path.find("(deleted)") == std::string::npos);
    CHECK(std::filesystem::exists(path));
    CHECK(nvj::platform::currentExePath() == path);
    int exitCode = 0;
    char* argv[] = {const_cast<char*>("x")};
    CHECK(!nvj::platform::handleUpdateHelper(1, argv, exitCode));
    std::string error;
    CHECK(!nvj::platform::startDeferredUpdate(path, 1, error));
    CHECK(!error.empty());
}

}

int main() {
    testMissingCurl();
    testDownload();
    testReplace();
    testExePath();
    return testing::finish();
}
