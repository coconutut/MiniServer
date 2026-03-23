#include <utils/utils.h>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

const char* utils::ReasonCode(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

bool utils::ReadFile(const std::string& path, std::string& out) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out = oss.str();
    return true;
}

std::string utils::UrlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out.push_back(' ');
        } else if (s[i] == '%' && i + 2 < s.size() &&
                   std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            std::string hex = s.substr(i + 1, 2);
            char ch = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            out.push_back(ch);
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> utils::ParseForm(const std::string& body) {
    std::unordered_map<std::string, std::string> kv;
    std::stringstream ss(body);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq == std::string::npos) continue;
        std::string k = UrlDecode(pair.substr(0, eq));
        std::string v = UrlDecode(pair.substr(eq + 1));
        kv[k] = v;
    }
    return kv;
}

std::string utils::trim(const std::string& s){
        size_t l = 0;
        size_t r = s.size();
        while(l < r && std::isspace(static_cast<unsigned char>(s[l]))){
            l++;
        }
        while(r > l && std::isspace(static_cast<unsigned char>(s[r-1]))){
            r--;
        }
        return s.substr(l, r - l);
}

std::string utils::toHex(const unsigned char* data, size_t len){
    std::ostringstream oss;
    for(size_t i = 0; i < len; i++){
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::vector<unsigned char> utils::fromHex(const std::string& Hex){
    std::vector<unsigned char> out;
    out.reserve(Hex.size() / 2);
    for(size_t i = 0; i + 1 < Hex.size(); i += 2){
        std::string byte = Hex.substr(i, 2);
        out.push_back(static_cast<unsigned char>(std::strtol(byte.c_str(), nullptr, 16)));
    }
    return out;
}

std::string utils::generateSaltHex(size_t len){
    std::vector<unsigned char> salt(len);
    if(RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1){
        return "";
    }
    return toHex(salt.data(), salt.size());
}

std::string utils::pbkdf2Hash(const std::string& password,
                       const std::string& saltHex){
    std::vector<unsigned char> salt = fromHex(saltHex);
    std::vector<unsigned char> out(32);
    int ok = PKCS5_PBKDF2_HMAC(
        password.c_str(),
        static_cast<int>(password.size()),
        salt.data(),
        static_cast<int>(salt.size()),
        100000,
        EVP_sha256(),
        static_cast<int>(out.size()),
        out.data()
    );
    if(ok != 1) return "";
    return toHex(out.data(), out.size());          
}

BusinessResponse utils::MakeResponse(int status, std::string body, std::string contentType){
        BusinessResponse response;
        response.status = status;
        response.body = std::move(body);
        response.contentType = std::move(contentType);
        return response;
}
//escape - 转义
std::string utils::EscapeMysqlString(MYSQL* sql, const std::string& input){
    if(!sql) return "";
    std::string escaped;
    escaped.resize(input.size()*2 + 1);
    unsigned long escapedLen = mysql_real_escape_string(
        sql,
        escaped.data(),
        input.c_str(),
        static_cast<unsigned long>(input.size())
    );

    escaped.resize(static_cast<size_t>(escapedLen));
    return escaped;
}

std::string utils::toLower(const std::string& s){
    std::string res(s);
    std::transform(res.begin(), res.end(), res.begin(), ::tolower);
    return res;
}