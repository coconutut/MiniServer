#include <services/BusinessHandler.h>
#include <unordered_map>
#include <utility>
#include <utils/utils.h>

BusinessHandler::BusinessHandler(std::string webRoot) : m_webRoot(webRoot){};

BusinessResponse BusinessHandler::Handle(const BusinessRequest& request, MYSQL* sql) const{
    if(!sql) return utils::MakeResponse(500, "database unavailable");
    //GET login
    if(request.method == "GET" && request.path == "/login"){
        return HandleGetLogin();
    }
    //style.css
    if(request.method == "GET" && request.path == "/style.css"){
        return HandleGetStyle();
    }
    //POST login
    if(request.method == "POST" && request.path == "/login"){
        return HandlePostLogin(request.body, sql);
    }
    //POST register
    if(request.method == "POST" && request.path == "/register"){
        return HandlePostRegister(request.body, sql);
    }
    //default
    return utils::MakeResponse(404, "404 Not Found");
}

BusinessResponse BusinessHandler::HandleGetLogin() const{
    std::string body;
    if(!utils::ReadFile(BuildWebPath("login.html"), body)){
        return utils::MakeResponse(500, "Failed to load login.html");
    }
    return utils::MakeResponse(200, body, "text/html");
}

BusinessResponse BusinessHandler::HandleGetStyle() const {
    std::string body;
    if (!utils::ReadFile(BuildWebPath("style.css"), body)) {
        return utils::MakeResponse(404, "style.css not found");
    }
    return utils::MakeResponse(200, body, "text/css");
}

BusinessResponse BusinessHandler::HandlePostLogin(const std::string& body, MYSQL* sql) const{
    std::unordered_map<std::string, std::string> form = utils::ParseForm(body);
    const std::string username = form["username"];
    const std::string password = form["password"];

    if(username.empty() || password.empty()){
        return utils::MakeResponse(400, "username/password required");
    }

    const std::string escapedUser = utils::EscapeMysqlString(sql, username);
    const std::string query = "SELECT password_hash, password_salt FROM users WHERE username='"
                               + escapedUser + "' LIMIT 1";
    
    if(mysql_query(sql, query.c_str()) != 0){
        return utils::MakeResponse(500, "mysql query failed");
    }

    MYSQL_RES* res = mysql_store_result(sql);
    if(!res){
        return utils::MakeResponse(500, "db result failed");
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if(!row){
        mysql_free_result(res); //清除缓存
        return utils::MakeResponse(401, "user not found");
    }

    const std::string dbHash = row[0] ? row[0] : "";
    const std::string dbSalt = row[1] ? row[1] : "";
    mysql_free_result(res);

    const std::string inputHash = utils::pbkdf2Hash(password, dbSalt);
    if(inputHash.empty()){
        return utils::MakeResponse(500, "password hash failed");
    }

    if(inputHash == dbHash){
        return utils::MakeResponse(200, "success login");
    }

    return utils::MakeResponse(401, "wrong password");
}

BusinessResponse BusinessHandler::HandlePostRegister(const std::string& body, MYSQL* sql) const {
    std::unordered_map<std::string, std::string> form = utils::ParseForm(body);
    const std::string username = form["username"];
    const std::string password = form["password"];

    if (username.empty() || password.empty()) {
        return utils::MakeResponse(400, "username/password required");
    }

    const std::string escapedUser = utils::EscapeMysqlString(sql, username);

    const std::string checkSql =
        "SELECT id FROM users WHERE username='" + escapedUser + "' LIMIT 1";

    if (mysql_query(sql, checkSql.c_str()) != 0) {
        return utils::MakeResponse(500, "db query failed");
    }

    MYSQL_RES* res = mysql_store_result(sql);
    if (!res) {
        return utils::MakeResponse(500, "db result failed");
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        mysql_free_result(res);
        return utils::MakeResponse(409, "user already exists");
    }
    mysql_free_result(res);

    const std::string saltHex = utils::generateSaltHex();
    const std::string hashHex = utils::pbkdf2Hash(password, saltHex);
    if (saltHex.empty() || hashHex.empty()) {
        return utils::MakeResponse(500, "password hash failed");
    }

    const std::string insertSql =
        "INSERT INTO users(username, password_hash, password_salt) VALUES('"
        + escapedUser + "','" + hashHex + "','" + saltHex + "')";

    if (mysql_query(sql, insertSql.c_str()) != 0) {
        return utils::MakeResponse(500, "register failed");
    }

    return utils::MakeResponse(200, "register success");
}

std::string BusinessHandler::BuildWebPath(const std::string& fileName) const{
    if(m_webRoot.empty()) return fileName;
    if(m_webRoot.back() == '/') return m_webRoot + fileName;
    return m_webRoot + "/" + fileName;
}