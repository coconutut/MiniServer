#include <MiniServer.h>
using namespace std;

namespace{
    void die(const char* message){
        cerr << message << "failed, errno =" << errno << ", error =" << strerror(errno) << endl;
        exit(EXIT_FAILURE);
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

    bool read_file(const std::string& path, std::string& out){
        std::ifstream ifs(path, std::ios::in | std::ios::binary);
        if(!ifs.is_open()) return false;
        std::ostringstream oss;
        oss << ifs.rdbuf();
        out = oss.str();
        return true;
    }
}

MiniServer::MiniServer(int port, int ThreadCount) : m_port(port), m_ThreadCount(ThreadCount){
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
        {
            auto it = m_HttpConn.find(fd);
            if(it != m_HttpConn.end() && it->second){
                const std::string method =  it->second->getMethod();
                const std::string path = it->second->getPath();
                if(method == "GET" && path == "/login"){
                    const std::string file_path = "/root/TinyWebServer/MiniServer/www/login.html";
                    if(read_file(file_path, body)){
                        status = 200;
                        contentType = "text/html";
                    }else{
                        status = 500;
                        body = "Failed to load login.html";
                    }
                }else if(method == "GET" && path == "/style.css"){
                    const std::string file_path = "/root/TinyWebServer/MiniServer/www/style.css";
                    if(read_file(file_path, body)){
                        status = 200;
                        contentType = "text/css";
                    }else{
                        status = 404;
                        body = "style.css not found";
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
