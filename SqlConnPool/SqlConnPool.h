#pragma once
#include <mysql/mysql.h>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <iostream>
#include <stdexcept>

class SqlConnPool{
public:
    static SqlConnPool& Instance();

    bool Init(const std::string& host, 
        const std::string& port, 
        const std::string& user, 
        const std::string& passwd,
        const std::string& dbName,
        int ConnSize
    );

    MYSQL* GetConn();

    bool FreeConn(MYSQL* conn);

    int GetFreeConnCount();

    void ClosePool();

private:
    SqlConnPool();
    ~SqlConnPool();
    
    //显示禁止 拷贝构造和赋值构造 
    SqlConnPool(const SqlConnPool&) = delete;
    SqlConnPool& operator=(const SqlConnPool&) = delete;

private:
    int maxConn_;
    int useCount_;
    bool isClosed_;

    std::queue<MYSQL*> connQueue_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

class SqlConnRAII{
public:
    SqlConnRAII(MYSQL** sql, SqlConnPool* connPool);
    ~SqlConnRAII();

    SqlConnRAII(const SqlConnRAII&) = delete;
    SqlConnRAII& operator=(const SqlConnRAII&) = delete;

private:
    MYSQL* sql_;
    SqlConnPool* connPool_;
};


