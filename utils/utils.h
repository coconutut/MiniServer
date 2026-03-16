#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <services/BusinessHandler.h>

class utils{
public:
    static const char* ReasonCode(int Code);

    static bool ReadFile(const std::string& path, std::string& out);

    static std::string UrlDecode(const std::string& s);

    static std::unordered_map<std::string, std::string> ParseForm(const std::string& body);
    
    static std::string trim(const std::string& s);

    static std::string toHex(const unsigned char* data, size_t len);

    static std::vector<unsigned char> fromHex(const std::string& hex);

    static std::string generateSaltHex(size_t len = 16);

    static std::string pbkdf2Hash(const std::string& password,
                                  const std::string& saltHex); 
    
    static BusinessResponse MakeResponse(int status, std::string body,
                              std::string contentType = "text/plain");
    
    static std::string EscapeMysqlString(MYSQL* sql, const std::string& input);

};