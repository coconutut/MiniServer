#include <HttpConn/HttpConn.h>
#include <cerrno>
#include <cctype>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace{
    std::string trim(const std::string& s){
        size_t l = 0;
        size_t r = s.size();
        while(l < r && std::isspace(static_cast<unsigned char>(s[l]))){
            l++;
        }
        while(r > l && std::isspace(static_cast<unsigned char>(s[r]))){
            r--;
        }
        return s.substr(l, r - l);
    }

    const char* reason_code(int code){
        switch(code){
            case 200: return "OK";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 500: return "Internal Server Error";
            default: return "Unknown";
        }
    }
}

HttpConn::HttpConn(int fd){
    m_fd = fd;
    m_parseState = ParseState::RequestLine;
    m_contentLength = 0;
    markRequestReady();
}

int HttpConn::fd() const{return m_fd;}

bool HttpConn::onReadable(){
    char buff[4096];
    while(true){
        int ret = recv(m_fd, buff, sizeof(buff), 0);
        if(ret > 0){
            m_inBuffer.append(buff, ret);
            continue;
        }
        if(ret == 0) return false;
        if(errno == EAGAIN || errno == EWOULDBLOCK){
            break;
        }
        return false;
    }
    parseRequest();
    return true;
}

bool HttpConn::onWritable(){
    while(m_responseReady){
        int ret = send(m_fd, m_outBuffer.data(), m_outBuffer.size(), 0);
        if(ret > 0){
            m_outBuffer.erase(0, static_cast<size_t>(ret));
            continue;
        }
        if(ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            m_responseReady = false;
            return true;
        }
        return false;
    }
    return false;
}

uint32_t HttpConn::desiredEvents() const{
    return m_responseReady ? EPOLLOUT : EPOLLIN;
}

bool HttpConn::parseRequest(){
    while(true){
        switch(m_parseState){
            case ParseState::RequestLine:
                if(!parseRequestLine()) return true;
                break;
            case ParseState::Headers:
                if(!parseHeaders()) return true;
                break;
            case ParseState::Body:
                if(!parseBody()) return true;
                break;
            case ParseState::Complete:
                m_requestReady = true;
                return true;
            case ParseState::Error:
                return true;
            default:
                return true;
        }
    }
}

bool HttpConn::parseRequestLine(){
    size_t pos = m_inBuffer.find("\r\n");
    if(pos == std::string::npos) return false;
    std::string line = m_inBuffer.substr(0, pos);
    m_inBuffer.erase(0, pos+2);
    std::istringstream iss(line);
    if(!(iss >> m_method >> m_path >> m_version)){
        markErrorResponse(400, "invalid request line");
        return true;
    }
    if(m_version.rfind("HTTP/", 0) != 0){
        markErrorResponse(400, "invalid http version");
        return true;
    }
    m_parseState = ParseState::Headers;
    return true;
}

bool HttpConn::parseHeaders(){
    size_t pos = m_inBuffer.find("\r\n\r\n");
    if(pos == std::string::npos) return false;
    std::string HeadBlock = m_inBuffer.substr(0, pos+4);
    m_inBuffer.erase(0, pos+4);

    m_headers.clear();
    m_contentLength = 0;
    size_t start = 0;
    //解析header键值对
    while(start < HeadBlock.size()){
        size_t lineEnd = HeadBlock.find("\r\n", start);
        if(lineEnd == std::string::npos) lineEnd = HeadBlock.size();
        std::string string_line = HeadBlock.substr(start, lineEnd - start);
        start = lineEnd + 2;
        if(string_line.empty()) continue;
        size_t colon = string_line.find(':');
        if(colon == std::string::npos){
            markErrorResponse(400, "malformed hearer");
            return true;
        }
        std::string key = trim(string_line.substr(0, colon));
        std::string val = trim(string_line.substr(colon + 1));
        m_headers[key] = val;
    }
    auto it = m_headers.find("Content-Length");
    if(it != m_headers.end()){
        try{
            m_contentLength = static_cast<size_t>(std::stoul(it->second));
        }
        catch(...){
            markErrorResponse(400, "invalid content-length");
            return true;
        }
    }
    if(m_contentLength > 0){
        m_parseState = ParseState::Body;
    }else{
        m_parseState = ParseState::Complete;
    }
    return true;
}

bool HttpConn::parseBody(){
    if(m_inBuffer.size() < m_contentLength){
        return false;
    }
    //无业务，仅消费
    m_inBuffer.erase(0, m_contentLength);
    m_parseState = ParseState::Complete;
    return true;
}

void HttpConn::markErrorResponse(int code, const std::string& msg){
    m_parseState = ParseState::Error;
    std::string body = std::to_string(code) + " " + reason_code(code) + "\n" + msg;
    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " " << reason_code(code) << "\r\n"
        << "Content-Type: text/plain; charset=utf-8\r\n"
        << "Connection: close\r\n"
        << "Content-Length: " << body.size() << "\r\n\r\n"
        << body;
    m_outBuffer = oss.str();
}

//const函数代表函数不会修改对象成员，除mutable成员外
bool HttpConn::isRequestReady() const{
    return m_requestReady;
}

bool HttpConn::isResponseReady() const{
    return m_responseReady;
}

bool HttpConn::isTaskSubmitted() const{
    return m_taskSubmitted;
}

void HttpConn::setTaskSubmitted(bool v){
    m_taskSubmitted = v;
}

void HttpConn::setBusinessResult(int status, const std::string& body, const std::string& contentType){
    m_responseReady = true;
    m_taskSubmitted = false;
    m_requestReady = false;

    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << reason_code(status) << "\r\n"
        << "Content-Type: " << contentType << "; charset=utf-8\r\n"
        << "Connection: close\r\n"
        << "Content-Length: " << body.size() << "\r\n\r\n"
        << body;
    m_outBuffer = oss.str();
}

void HttpConn::markRequestReady(){
    m_responseReady = false;
    m_taskSubmitted = false;
    m_requestReady = false;
}

std::string& HttpConn::getMethod(){
    return m_method;
}

std::string& HttpConn::getPath(){
    return m_path;
}