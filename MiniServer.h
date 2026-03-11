#pragma once
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <ThreadPool/ThreadPool.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
using namespace std;

class MiniServer{
public:
    MiniServer(int port, int ThreadCount);
    ~MiniServer();

    void run();
private:
    void set_nonblocking(int fd);
    void handle_new_connection();
    void handle_message(int sockfd);
    void handle_write(int sockfd);
    void close_connection(int sockfd);
private:
    int threadCount;
    int m_port;
    int m_listenfd;
    int m_epollfd;
    int m_ThreadCount;
    ThreadPool* m_ThreadPool;
    epoll_event m_events[1024];
    unordered_map<int, string> m_pendingWrite;
    mutex m_pendingMutex;
};