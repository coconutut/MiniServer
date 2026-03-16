#pragma once
#include <mysql/mysql.h>
#include <string>

struct BusinessRequest{
    std::string method;
    std::string body;
    std::string path;
};

struct BusinessResponse{
    int status = 404;
    std::string body = "404 Not Found";
    std::string contentType = "text/plain";
};

class BusinessHandler{
public:
    explicit BusinessHandler(std::string webRoot);
    BusinessResponse Handle(const BusinessRequest& request, MYSQL* sql) const;

private:
    BusinessResponse HandleGetLogin() const;
    BusinessResponse HandleGetStyle() const;
    BusinessResponse HandlePostLogin(const std::string& body, MYSQL* sql) const;
    BusinessResponse HandlePostRegister(const std::string& body, MYSQL* sql) const;
    std::string BuildWebPath(const std::string& fileName) const;
private:
    std::string m_webRoot;
};