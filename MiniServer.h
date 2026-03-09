#pragma once
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
using namespace std;

class MiniServer{
public:
    MiniServer(int port);
    ~MiniServer();

    void run();
private:
    void set_nonblocking(int fd);
    void handle_new_connection();
    void handle_message(int sockfd);
private:
    int m_port;
    int m_listenfd;
    int m_epollfd;
    epoll_event m_events[1024];
};