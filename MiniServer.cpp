#include <MiniServer.h>
using namespace std;

MiniServer::MiniServer(int port, int ThreadCount) : m_port(port), m_ThreadCount(ThreadCount){
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(m_listenfd == -1) cout << "listenfd create failed!"<< endl;

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);

    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    listen(m_listenfd, 5);

    //初始化线性池
    m_ThreadPool = new ThreadPool(m_ThreadCount);


    m_epollfd = epoll_create(5);
    epoll_event event;
    event.data.fd = m_listenfd;
    event.events = EPOLLIN;
    epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_listenfd, &event);
}

MiniServer::~MiniServer(){
    close(m_listenfd);
    close(m_epollfd);
    delete m_ThreadPool;
}

void MiniServer::set_nonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL,  old_option | O_NONBLOCK);
}

void MiniServer::handle_new_connection(){
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);

    set_nonblocking(connfd);
    epoll_event event;
    event.data.fd = connfd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(m_epollfd, EPOLL_CTL_ADD, connfd, &event);
    cout << "new connection accepted, fd: " << connfd << endl;
}

void MiniServer::handle_message(int sockfd){
    m_ThreadPool->enqueue([sockfd, this]{
        char buff[1024];
        memset(buff, '\0', sizeof(buff));
        int ret = recv(sockfd, buff, 1023, 0); //结尾要有'\0'
        if(ret <= 0){
            epoll_ctl(this->m_epollfd, EPOLL_CTL_DEL, sockfd, 0);
            close(sockfd);
            cout << "Connection closed, fd : " << sockfd << endl;
        }
        else{
            cout << "request from fd " << sockfd << endl;
            const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nI LOVE YOU";
            send(sockfd, response, strlen(response), 0);
            close(sockfd);
        }
    });
}

void MiniServer::run(){
    cout << "Server is runing on port: " << m_port << endl;
    while(true){
        int number = epoll_wait(m_epollfd, m_events, 1024, -1);
        for(int i = 0; i < number; i++){
            int sockfd = m_events[i].data.fd;
            if(sockfd == m_listenfd){
                handle_new_connection();
            }else if(m_events[i].events & EPOLLIN){
                handle_message(sockfd);
            }
        }
    }
}
