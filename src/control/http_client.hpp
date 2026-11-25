#pragma once

#include <string>

namespace nvj {

struct HttpExchange {
    std::string text;
    bool closed = false;
    int status = 0;
    std::string body;
};

HttpExchange httpExchange(int port, const std::string& request, int timeoutMs = 3000, bool halfClose = false);

}
