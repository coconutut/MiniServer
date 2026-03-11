#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <sys/epoll.h>

enum class ParseState{
    RequestLine,
    Headers,
    Body,
    Complete,
    Error
};

class HttpConn{
public:
    explicit HttpConn(int fd);
    int fd() const;

    bool onReadable();
    bool onWritable();
    uint32_t desiredEvents() const;

private:
    bool parseRequest();
    bool parseRequestLine();
    bool parseHeaders();
    bool parseBody();
    void buildResponse();
    void markErrorResponse(int code, const std::string& msg);

private:
    int m_fd;
    ParseState m_parseState;
    std::string m_inBuffer;
    std::string m_outBuffer;
    std::string m_method;
    std::string m_path;
    std::string m_version;
    std::unordered_map<std::string, std::string> m_headers;
    size_t m_contentLength;
};