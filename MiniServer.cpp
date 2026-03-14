#include <MiniServer.h>
using namespace std;

namespace{
    void die(const char* message){
        cerr << message << "failed, errno =" << errno << ", error =" << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
}

MiniServer::MiniServer(int port, int ThreadCount) : m_port(port), m_ThreadCount(ThreadCount){
    //sql连接池初始化
    if(!SqlConnPool::Instance().Init("127.0.0.1", "3306", "root", "Monster12300!", "mydb", 10)){
        die("SqlPool init failed");
    }
    
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(m_listenfd == -1) die("socket");

    m_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(m_eventfd == -1) die("eventfd");

    set_nonblocking(m_listenfd);

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);

    //端口复用
    int reuse = 1;
    if(setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1){
        die("setsockopt");
    }

    if(bind(m_listenfd, (struct sockaddr*)&address, sizeof(address)) == -1){
        die("bind");
    }

    if(listen(m_listenfd, 5) == -1) die("listen");

    //初始化线性池
    m_ThreadPool = new ThreadPool(m_ThreadCount);

    m_epollfd = epoll_create(5);
    if(m_epollfd == -1) die("epoll_create");
    epoll_event event;
    event.data.fd = m_listenfd;
    event.events = EPOLLIN; //LT触发
    if(epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_listenfd, &event) == -1)
        die("epoll_ctl: add listenfd");
    
    epoll_event ev;
    ev.data.fd = m_eventfd;
    ev.events = EPOLLIN;
    if(epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_eventfd, &ev) == -1){
        die("epoll_ctl: eventfd");
    }
}

MiniServer::~MiniServer(){
    close(m_listenfd);
    close(m_epollfd);
    close(m_eventfd);
    delete m_ThreadPool;
}

void MiniServer::set_nonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    if(old_option == -1) 
        die("fcntl(F_GETFL)");
    if(fcntl(fd, F_SETFL,  old_option | O_NONBLOCK) == -1)
        die("fcntl(F_SETFL)");
}

void MiniServer::close_connection(int sockfd){
    epoll_ctl(m_epollfd, EPOLL_CTL_DEL, sockfd, nullptr);
    close(sockfd);
    m_HttpConn.erase(sockfd);
    cout << "connection closed: " << sockfd << endl;
}

void MiniServer::handle_new_connection(){
    while(true){
        struct sockaddr_in client_address;
        socklen_t client_addrlength = sizeof(client_address);        
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if(connfd == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }
            cerr << "accept failed, errno=" << errno << ", error=" << strerror(errno) << endl;
            continue;
        }
        set_nonblocking(connfd);
        m_HttpConn[connfd] = make_unique<HttpConn>(connfd);

        epoll_event event;
        event.data.fd = connfd;
        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

        if(epoll_ctl(m_epollfd, EPOLL_CTL_ADD, connfd, &event) == -1){
            cerr << "epoll_ctl : add connfd failed, errno=" << errno << ", error=" << strerror(errno) << endl;
            close(connfd);
            continue;
        }
        cout << "new connection accepted, fd: " << connfd << endl;
    }
}

void MiniServer::submitBusinessTask(int fd){
    auto it = m_HttpConn.find(fd);
    if(it == m_HttpConn.end() || !it->second){
        return;
    }

    HttpConn* conn = it->second.get();

    if(!conn->isRequestReady() || conn->isTaskSubmitted()){
        return;
    }

    conn->setTaskSubmitted(true);
    m_ThreadPool->enqueue([this, fd, conn]{
        int status = 404;
        std::string contentType = "text/plain";
        std::string body = "404 Not Found";

        MYSQL* sql = nullptr;
        SqlConnRAII raii(&sql, &SqlConnPool::Instance());
        if(!sql){
            status = 500;
            body = "database unavailable";
            contentType = "text/plain";
        }else{
            if(mysql_query(sql, "SELECT 1") != 0){
                status = 500;
                body = "database query failed";
                contentType = "text/plain";
            }
            else{
                MYSQL_RES* pingRes = mysql_store_result(sql);
                if(pingRes){
                    mysql_free_result(pingRes);
                }
                auto it = m_HttpConn.find(fd);
                if(it != m_HttpConn.end() && it->second){
                    const std::string method =  it->second->getMethod();
                    const std::string path = it->second->getPath();
                    const std::string rBody = it->second->getBody();
                    if(method == "GET" && path == "/login"){
                        const std::string file_path = "/root/TinyWebServer/MiniServer/www/login.html";
                        if(utils::ReadFile(file_path, body)){
                            status = 200;
                            contentType = "text/html";
                        }else{
                            status = 500;
                            body = "Failed to load login.html";
                        }
                    }else if(method == "GET" && path == "/style.css"){
                        const std::string file_path = "/root/TinyWebServer/MiniServer/www/style.css";
                        if(utils::ReadFile(file_path, body)){
                            status = 200;
                            contentType = "text/css";
                        }else{
                            status = 404;
                            body = "style.css not found";
                            contentType = "text/plain";
                        }
                    }else if(method == "POST" && path == "/login"){
                        status = 200;
                        std::unordered_map<std::string, std::string> Form = utils::ParseForm(rBody);
                        auto username = Form["username"];
                        auto password = Form["password"];
                        if(username.empty() || password.empty()){
                            status = 400;
                            body = "username/password required";
                            contentType = "text/plain";
                        }else{
                            char userEsc[256] = {0};
                            mysql_real_escape_string(sql, userEsc, username.c_str(), static_cast<unsigned long>(username.size()));
                            std::string query = "SELECT password_hash, password_salt From users where username = '" + std::string(userEsc) + "'LIMIT 1";
                            if(mysql_query(sql, query.c_str()) != 0){
                                status = 500;
                                body = "mysql query failed";
                                contentType = "text/plain";
                            }else{
                                MYSQL_RES* res = mysql_store_result(sql);
                                if(!res){
                                    status = 500;
                                    body = "db result failed";
                                    contentType = "text/plain";
                                }
                                else{
                                    MYSQL_ROW row = mysql_fetch_row(res);
                                    if(!row){
                                        status = 401;
                                        body = "user not found";
                                        contentType = "text/plain";
                                    }
                                    else{
                                        std::string dbHash = row[0] ? row[0] : "";
                                        std::string dbSalt = row[1] ? row[1] : "";
                                        std::string inputHash = utils::pbkdf2Hash(password, dbSalt);
                                        if(!inputHash.empty() && inputHash == dbHash){
                                            status = 200;
                                            body = "login sucess";
                                            contentType = "text/plain";
                                        }else{
                                            status = 401;
                                            body = "wrong password";
                                            contentType = "text/plain";
                                        }
                                    }
                                    mysql_free_result(res);
                                }
                            }
                        }
                    }else if(method == "POST" && path == "/register"){
                        std::unordered_map<std::string, std::string> form = utils::ParseForm(rBody);
                        std::string username = form["username"];
                        std::string password = form["password"];
                        contentType = "text/plain";

                        if(username.empty() || password.empty()){
                            status = 400;
                            body = "username/password required";
                        }else{
                            char userEsc[256] = {0};
                            mysql_real_escape_string(sql, userEsc, username.c_str(),
                            static_cast<unsigned long>(username.size()));
                                // 1) 检查用户名是否已存在
                            std::string checkSql =
                            "SELECT id FROM users WHERE username='" + std::string(userEsc) + "' LIMIT 1";
                            if(mysql_query(sql, checkSql.c_str()) != 0){
                                status = 500;
                                body = "db query failed";
                            }else{
                                MYSQL_RES* res = mysql_store_result(sql);
                                if(!res){
                                    status = 500;
                                    body = "db result failed";
                                }else{
                                    MYSQL_ROW row = mysql_fetch_row(res);
                                    if(row){
                                        status = 409;
                                        body = "user already exists";
                                        mysql_free_result(res);
                                    }else{
                                            mysql_free_result(res);
                                            // 2) 生成盐 + PBKDF2 哈希
                                            std::string saltHex = utils::generateSaltHex();
                                            std::string hashHex = utils::pbkdf2Hash(password, saltHex);
                                            if(saltHex.empty() || hashHex.empty()){
                                                status = 500;
                                                body = "password hash failed";
                                            }else{
                                            // 3) 写入用户
                                                std::string insertSql =
                                                "INSERT INTO users(username, password_hash, password_salt) VALUES('"
                                                + std::string(userEsc) + "','" + hashHex + "','" + saltHex + "')";
                                                if(mysql_query(sql, insertSql.c_str()) != 0){
                                                    status = 500;
                                                    body = "register failed";
                                                }else{
                                                    status = 200;
                                                    body = "register success";
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                    }else{
                        status = 404;
                        body = "404 Not Found";
                        contentType = "text/plain";
                    }
                }
            }
        }
        {
            std::lock_guard<mutex> lock(m_resultMutex);
            m_resultQueue.push(BusinessResult{fd, status, body, contentType});
        }
        uint64_t one = 1;
        ssize_t n = write(m_eventfd, &one, sizeof(one));
        if(n < 0 && errno != EAGAIN) cout << "event notice failed!" << endl;
    });
}

void MiniServer::drainBusinessResult(){
    while(true){
        BusinessResult res;
        {
            std::lock_guard<mutex> lock(m_resultMutex);
            if(m_resultQueue.empty()) return;
            res = m_resultQueue.front();
            m_resultQueue.pop();
        }
        auto it = m_HttpConn.find(res.fd);
        if(it == m_HttpConn.end() || !it->second) continue;
        it->second->setBusinessResult(res.status, res.body, res.contentType);
        epoll_event event;
        event.data.fd = res.fd;
        event.events = it->second->desiredEvents() | EPOLLET | EPOLLONESHOT;
        epoll_ctl(m_epollfd, EPOLL_CTL_MOD, res.fd, &event);
    }
}

void MiniServer::run(){
    cout << "Server is runing on port: " << m_port << endl;
    while(true){
        int number = epoll_wait(m_epollfd, m_events, 1024, -1);
        for(int i = 0; i < number; i++){
            int sockfd = m_events[i].data.fd;
            if(m_events[i].events & (EPOLLERR | EPOLLRDHUP | EPOLLHUP)){
                close_connection(sockfd);
                continue;
            }
            //新连接
            if(sockfd == m_listenfd){
                handle_new_connection(); 
                continue;
            }
            //事件通知
            if(sockfd == m_eventfd){
                uint64_t cnt;
                while(read(m_eventfd, &cnt, sizeof(cnt)) > 0) {}
                drainBusinessResult();
                continue;
            }
            //请求解析、回复
            bool alive = true;
            auto it = m_HttpConn.find(sockfd);
            if(it == m_HttpConn.end() || !it->second) continue;
            if(m_events[i].events & EPOLLIN){
                alive = it->second->onReadable();
                if(it->second->isRequestReady())
                    submitBusinessTask(sockfd);
            }
            if(alive && (m_events[i].events & EPOLLOUT)){
                alive = it->second->onWritable();
            }
            if(!alive){
                close_connection(sockfd);
                continue;
            }
        }
        drainBusinessResult();
    }
}
