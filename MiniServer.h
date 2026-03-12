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
#include <HttpConn/HttpConn.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <queue>
#include <sys/eventfd.h>
#include <stdint.h>
#include <fstream>
#include <sstream>
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
    void submitBusinessTask(int fd);
    void drainBusinessResult();
    void rearm_connection(int fd, uint32_t events);

private:
    int threadCount;
    int m_port;
    int m_listenfd;
    int m_epollfd;
    int m_eventfd;
    int m_ThreadCount;
    ThreadPool* m_ThreadPool;
    epoll_event m_events[1024];
    unordered_map<int, string> m_pendingWrite;
    mutex m_pendingMutex;
    unordered_map<int, unique_ptr<HttpConn>> m_HttpConn;
    struct BusinessResult{
        int fd;
        int status;
        std::string body;
        string contentType;
    };
    std::queue<BusinessResult> m_resultQueue;
    std::mutex m_resultMutex;
};