#include <MiniServer.h>
using namespace std;

namespace{
    void die(const char* message){
        cerr << message << "failed, errno =" << errno << ", error =" << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
}

MiniServer::MiniServer(int port, int ThreadCount) : m_port(port), m_ThreadCount(ThreadCount){
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(m_listenfd == -1) die("socket");

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
}

MiniServer::~MiniServer(){
    close(m_listenfd);
    close(m_epollfd);
    delete m_ThreadPool;
}

void MiniServer::set_nonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    if(old_option == -1) 
        die("fcntl(F_GETFL)");
    if(fcntl(fd, F_SETFL,  old_option | O_NONBLOCK) == -1)
        die("fcntl(F_SETFL)");
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

void MiniServer::handle_message(int sockfd){
    m_ThreadPool->enqueue([sockfd, this]{
        char buff[1024];
        memset(buff, '\0', sizeof(buff));
        string request;
        while(true){
            int ret = recv(sockfd, buff, sizeof(buff), 0);
            if(ret > 0){
                request.append(buff, ret);
                continue;
            }
            if(ret == 0){
                epoll_ctl(m_epollfd, EPOLL_CTL_DEL, sockfd, nullptr);
                close(sockfd);
                cout << "Connection closed, fd : " << sockfd << endl;
                return;
            }
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }else{
                epoll_ctl(m_epollfd, EPOLL_CTL_DEL, sockfd, nullptr);
                close(sockfd);
                return;
            }
        }
        cout << "receive request : " << request << endl;
        string body = "Hello from MiniServer";
        string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Connection: close\r\n"
            "Content-Length: " + to_string(body.size()) + "\r\n\r\n" +
            body;

        size_t sent = 0;
        while(sent < response.size()){
            int ret = send(sockfd, response.c_str() + sent, response.size() - sent, 0);
            if(ret > 0){
                sent += ret;
                continue;
            }
            if(ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
                lock_guard<mutex> lock(m_pendingMutex);
                m_pendingWrite[sockfd] = response.substr(sent);
                epoll_event event;
                event.data.fd = sockfd;
                event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
                epoll_ctl(m_epollfd, EPOLL_CTL_MOD, sockfd, &event);
                return;
            }
            //ret == 0时候
            close_connection(sockfd);
            return;
        }
        close_connection(sockfd);
    });
}

void MiniServer::close_connection(int sockfd){
    epoll_ctl(m_epollfd, EPOLL_CTL_DEL, sockfd, nullptr);
    close(sockfd);
    lock_guard<mutex> lock(m_pendingMutex);
    m_pendingWrite.erase(sockfd);
}

void MiniServer::handle_write(int sockfd){
    string pending;
    {
        lock_guard<mutex> lock(m_pendingMutex);
        auto it = m_pendingWrite.find(sockfd);
        if(it == m_pendingWrite.end()) return;
        pending = it->second;
    }

    size_t sent = 0;
    while(sent < pending.size()){
        int ret  = send(sockfd, pending.c_str() + sent, pending.size() - sent, 0);
        if(ret > 0){
            sent += ret;
            continue;
        }
        if(ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            lock_guard<mutex> lock(m_pendingMutex);
            m_pendingWrite[sockfd] = pending.substr(sent);
            epoll_event event;
            event.data.fd = sockfd;
            event.events = EPOLLOUT | EPOLLONESHOT | EPOLLET;
            epoll_ctl(m_epollfd, EPOLL_CTL_MOD, sockfd, &event);
            return;
        }
        //ret == 0时候
        close_connection(sockfd);
        return;
    }
    {
        lock_guard<mutex> lock(m_pendingMutex);
        m_pendingWrite.erase(sockfd);
    }
    close_connection(sockfd);
}

void MiniServer::run(){
    cout << "Server is runing on port: " << m_port << endl;
    while(true){
        int number = epoll_wait(m_epollfd, m_events, 1024, -1);
        for(int i = 0; i < number; i++){
            int sockfd = m_events[i].data.fd;
            if(m_events[i].events & (EPOLLERR | EPOLLRDHUP | EPOLLHUP)){
                epoll_ctl(m_epollfd, EPOLL_CTL_DEL, sockfd, nullptr);
                close(sockfd);
                continue;
            }
            if(sockfd == m_listenfd){
                handle_new_connection(); 
                continue;
            }
            if(m_events[i].events & EPOLLIN){
                handle_message(sockfd);
            }
            if(m_events[i].events & EPOLLOUT){
                handle_write(sockfd);
            }
        }
    }
}
