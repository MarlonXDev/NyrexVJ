#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace nvj {

constexpr size_t kHttpMaxHeaderBytes = 8192;
constexpr size_t kHttpMaxHeaders = 48;
constexpr size_t kHttpMaxBodyBytes = 65536;
constexpr size_t kHttpMaxTargetBytes = 2048;

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string version;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    const std::string* header(const std::string& lowerName) const;
    int headerCount(const std::string& lowerName) const;
};

struct HttpResponse {
    int status = 200;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    void setHeader(const std::string& name, const std::string& value);
    const std::string* header(const std::string& name) const;
};

enum class HttpParseState { NeedMore, Complete, Failed };

class HttpRequestParser {
public:
    explicit HttpRequestParser(size_t maxHeaderBytes = kHttpMaxHeaderBytes, size_t maxBodyBytes = kHttpMaxBodyBytes);

    HttpParseState feed(const char* data, size_t size);
    HttpParseState state() const { return state_; }
    const HttpRequest& request() const { return request_; }
    int errorStatus() const { return errorStatus_; }
    const std::string& errorText() const { return errorText_; }
    size_t extraBytes() const { return extraBytes_; }
    size_t buffered() const { return buffer_.size(); }
    bool headersComplete() const { return headersDone_; }

private:
    void fail(int status, const std::string& text);
    void parseHead(size_t headEnd);
    bool parseRequestLine(const std::string& line);
    bool parseHeaderLine(const std::string& line);
    bool finishHeaders();

    size_t maxHeaderBytes_;
    size_t maxBodyBytes_;
    HttpParseState state_ = HttpParseState::NeedMore;
    HttpRequest request_;
    std::string buffer_;
    size_t scanned_ = 0;
    bool headersDone_ = false;
    size_t bodyLength_ = 0;
    size_t extraBytes_ = 0;
    int errorStatus_ = 0;
    std::string errorText_;
};

const char* httpReasonPhrase(int status);
std::string serializeHttpResponse(const HttpResponse& response);
bool decodePercent(const std::string& input, std::string& output);
std::string lowerAscii(const std::string& text);
std::string trimHttpSpace(const std::string& text);

}
