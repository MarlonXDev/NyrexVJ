#include "core/http_message.hpp"

#include <algorithm>
#include <cctype>

namespace nvj {

namespace {

bool isTokenChar(unsigned char c) {
    if (std::isalnum(c) != 0) {
        return true;
    }
    switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
        return true;
    default:
        return false;
    }
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool validFieldValue(const std::string& value) {
    for (const char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c == '\t') {
            continue;
        }
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

bool validMethod(const std::string& method) {
    if (method.empty() || method.size() > 16) {
        return false;
    }
    return std::all_of(method.begin(), method.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
}

bool bodyForbidden(const std::string& method) {
    return method == "GET" || method == "HEAD" || method == "OPTIONS" || method == "DELETE";
}

bool hasDotSegment(const std::string& path) {
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        const std::string segment = path.substr(start, end - start);
        if (segment == "." || segment == "..") {
            return true;
        }
        start = end + 1;
    }
    return false;
}

}

std::string lowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string trimHttpSpace(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool decodePercent(const std::string& input, std::string& output) {
    output.clear();
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c != '%') {
            output.push_back(c);
            continue;
        }
        if (i + 2 >= input.size()) {
            return false;
        }
        const int high = hexValue(input[i + 1]);
        const int low = hexValue(input[i + 2]);
        if (high < 0 || low < 0) {
            return false;
        }
        const int value = high * 16 + low;
        if (value < 0x20 || value == 0x7f || value == '/' || value == '\\') {
            return false;
        }
        output.push_back(static_cast<char>(value));
        i += 2;
    }
    return true;
}

const std::string* HttpRequest::header(const std::string& lowerName) const {
    for (const auto& entry : headers) {
        if (entry.first == lowerName) {
            return &entry.second;
        }
    }
    return nullptr;
}

int HttpRequest::headerCount(const std::string& lowerName) const {
    int count = 0;
    for (const auto& entry : headers) {
        if (entry.first == lowerName) {
            ++count;
        }
    }
    return count;
}

void HttpResponse::setHeader(const std::string& name, const std::string& value) {
    auto clean = [](const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (const char c : text) {
            if (c != '\r' && c != '\n' && c != '\0') {
                out.push_back(c);
            }
        }
        return out;
    };
    const std::string cleanName = clean(name);
    const std::string cleanValue = clean(value);
    const std::string lower = lowerAscii(cleanName);
    for (auto& entry : headers) {
        if (lowerAscii(entry.first) == lower) {
            entry.second = cleanValue;
            return;
        }
    }
    headers.emplace_back(cleanName, cleanValue);
}

const std::string* HttpResponse::header(const std::string& name) const {
    const std::string lower = lowerAscii(name);
    for (const auto& entry : headers) {
        if (lowerAscii(entry.first) == lower) {
            return &entry.second;
        }
    }
    return nullptr;
}

HttpRequestParser::HttpRequestParser(size_t maxHeaderBytes, size_t maxBodyBytes) : maxHeaderBytes_(maxHeaderBytes), maxBodyBytes_(maxBodyBytes) {}

void HttpRequestParser::fail(int status, const std::string& text) {
    state_ = HttpParseState::Failed;
    errorStatus_ = status;
    errorText_ = text;
    buffer_.clear();
}

HttpParseState HttpRequestParser::feed(const char* data, size_t size) {
    if (state_ == HttpParseState::Complete) {
        extraBytes_ += size;
        return state_;
    }
    if (state_ == HttpParseState::Failed) {
        return state_;
    }
    if (size > 0 && data != nullptr) {
        buffer_.append(data, size);
    }
    if (!headersDone_) {
        for (size_t i = scanned_; i < buffer_.size(); ++i) {
            const char c = buffer_[i];
            if (c == '\n' && (i == 0 || buffer_[i - 1] != '\r')) {
                fail(400, "A line ends without a carriage return");
                return state_;
            }
            if (c == '\r' && i + 1 < buffer_.size() && buffer_[i + 1] != '\n') {
                fail(400, "A carriage return stands alone");
                return state_;
            }
            if (c == '\0') {
                fail(400, "The request contains a null byte");
                return state_;
            }
        }
        const size_t from = scanned_ >= 3 ? scanned_ - 3 : 0;
        scanned_ = buffer_.size();
        const size_t end = buffer_.find("\r\n\r\n", from);
        if (end == std::string::npos) {
            if (buffer_.size() > maxHeaderBytes_) {
                fail(431, "The request header is too large");
            }
            return state_;
        }
        if (end + 4 > maxHeaderBytes_) {
            fail(431, "The request header is too large");
            return state_;
        }
        parseHead(end);
        if (state_ == HttpParseState::Failed) {
            return state_;
        }
        headersDone_ = true;
        buffer_.erase(0, end + 4);
    }
    if (buffer_.size() >= bodyLength_) {
        request_.body = buffer_.substr(0, bodyLength_);
        extraBytes_ = buffer_.size() - bodyLength_;
        buffer_.clear();
        state_ = HttpParseState::Complete;
    }
    return state_;
}

void HttpRequestParser::parseHead(size_t headEnd) {
    const std::string head = buffer_.substr(0, headEnd + 2);
    size_t position = 0;
    bool first = true;
    size_t headerLines = 0;
    while (position < head.size()) {
        const size_t lineEnd = head.find("\r\n", position);
        if (lineEnd == std::string::npos) {
            fail(400, "The request header is malformed");
            return;
        }
        const std::string line = head.substr(position, lineEnd - position);
        position = lineEnd + 2;
        if (first) {
            first = false;
            if (!parseRequestLine(line)) {
                return;
            }
            continue;
        }
        if (++headerLines > kHttpMaxHeaders) {
            fail(431, "The request has too many header lines");
            return;
        }
        if (!parseHeaderLine(line)) {
            return;
        }
    }
    if (first) {
        fail(400, "The request line is missing");
        return;
    }
    finishHeaders();
}

bool HttpRequestParser::parseRequestLine(const std::string& line) {
    const size_t firstSpace = line.find(' ');
    const size_t secondSpace = firstSpace == std::string::npos ? std::string::npos : line.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        fail(400, "The request line is malformed");
        return false;
    }
    request_.method = line.substr(0, firstSpace);
    request_.target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    request_.version = line.substr(secondSpace + 1);
    if (!validMethod(request_.method)) {
        fail(400, "The method is not valid");
        return false;
    }
    if (request_.version.find(' ') != std::string::npos) {
        fail(400, "The request line is malformed");
        return false;
    }
    if (request_.version != "HTTP/1.1" && request_.version != "HTTP/1.0") {
        if (request_.version.compare(0, 5, "HTTP/") == 0) {
            fail(505, "Only HTTP/1.1 and HTTP/1.0 are supported");
        } else {
            fail(400, "The request line is malformed");
        }
        return false;
    }
    const std::string& target = request_.target;
    if (target.size() > kHttpMaxTargetBytes) {
        fail(414, "The address is too long");
        return false;
    }
    if (target.empty() || target[0] != '/' || (target.size() > 1 && target[1] == '/')) {
        fail(400, "The address must start with a single slash");
        return false;
    }
    for (const char raw : target) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c <= 0x20 || c >= 0x7f || c == '#' || c == '\\') {
            fail(400, "The address contains a character that is not allowed");
            return false;
        }
    }
    const size_t question = target.find('?');
    const std::string rawPath = question == std::string::npos ? target : target.substr(0, question);
    request_.query = question == std::string::npos ? std::string() : target.substr(question + 1);
    if (!decodePercent(rawPath, request_.path)) {
        fail(400, "The address is not valid");
        return false;
    }
    if (hasDotSegment(request_.path)) {
        fail(400, "The address contains a dot segment");
        return false;
    }
    return true;
}

bool HttpRequestParser::parseHeaderLine(const std::string& line) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) {
        fail(400, "A header line is malformed");
        return false;
    }
    const std::string name = line.substr(0, colon);
    for (const char c : name) {
        if (!isTokenChar(static_cast<unsigned char>(c))) {
            fail(400, "A header name is not valid");
            return false;
        }
    }
    const std::string value = trimHttpSpace(line.substr(colon + 1));
    if (!validFieldValue(value)) {
        fail(400, "A header value is not valid");
        return false;
    }
    request_.headers.emplace_back(lowerAscii(name), value);
    return true;
}

bool HttpRequestParser::finishHeaders() {
    const int hosts = request_.headerCount("host");
    if (hosts > 1 || (request_.version == "HTTP/1.1" && hosts != 1)) {
        fail(400, "The Host header is missing or repeated");
        return false;
    }
    if (hosts == 1 && request_.header("host")->empty()) {
        fail(400, "The Host header is empty");
        return false;
    }
    for (const char* single : {"authorization", "origin", "content-type", "content-length"}) {
        if (request_.headerCount(single) > 1) {
            fail(400, std::string("The header ") + single + " is repeated");
            return false;
        }
    }
    if (request_.header("transfer-encoding") != nullptr) {
        fail(501, "Transfer encodings are not supported");
        return false;
    }
    if (request_.header("expect") != nullptr) {
        fail(417, "Expect is not supported");
        return false;
    }
    bodyLength_ = 0;
    if (const std::string* length = request_.header("content-length")) {
        if (length->empty() || length->size() > 9 || !std::all_of(length->begin(), length->end(), [](char c) { return c >= '0' && c <= '9'; })) {
            fail(400, "The Content-Length is not valid");
            return false;
        }
        const size_t value = static_cast<size_t>(std::stoul(*length));
        if (value > maxBodyBytes_) {
            fail(413, "The request body is too large");
            return false;
        }
        if (value > 0 && bodyForbidden(request_.method)) {
            fail(400, "This method does not take a body");
            return false;
        }
        bodyLength_ = value;
    }
    return true;
}

const char* httpReasonPhrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 417: return "Expectation Failed";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 505: return "HTTP Version Not Supported";
    default: return "Status";
    }
}

std::string serializeHttpResponse(const HttpResponse& response) {
    std::string out;
    out.reserve(response.body.size() + 256);
    out += "HTTP/1.1 ";
    out += std::to_string(response.status);
    out += ' ';
    out += httpReasonPhrase(response.status);
    out += "\r\nContent-Length: ";
    out += std::to_string(response.body.size());
    out += "\r\nConnection: close\r\n";
    for (const auto& entry : response.headers) {
        const std::string lower = lowerAscii(entry.first);
        if (lower == "content-length" || lower == "connection") {
            continue;
        }
        out += entry.first;
        out += ": ";
        out += entry.second;
        out += "\r\n";
    }
    out += "\r\n";
    out += response.body;
    return out;
}

}
