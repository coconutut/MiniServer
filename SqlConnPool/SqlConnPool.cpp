#include <SqlConnPool/SqlConnPool.h>

SqlConnPool::SqlConnPool() : 
    maxConn_(0), 
    useCount_(0), 
    isClosed_(true){};

SqlConnPool::~SqlConnPool(){
    ClosePool();
}

SqlConnPool& SqlConnPool::Instance(){
    static SqlConnPool sqlPool;
    return sqlPool;
}

bool SqlConnPool::Init(const std::string& host,
                       const std::string& port,
                       const std::string& user,
                       const std::string& passwd,
                       const std::string& dbName,
                       int ConnSize) {
        std::lock_guard<std::mutex> lock(mtx_);
        if(!isClosed_) return true;
        if(ConnSize <= 0) return false;
        unsigned int dbPort = 0;
        try{
            dbPort = static_cast<unsigned int>(std::stoul(port));  
        }
        catch(...){
            return false;
        }

        for(int i = 0; i < ConnSize; i++){
            MYSQL* sql = mysql_init(nullptr);
            if(!sql){
                ClosePool();
                std::cout << "Mysql Init failed" << std::endl;
                return false;
            }

            MYSQL* conn = mysql_real_connect(
                sql,
                host.c_str(),
                user.c_str(),
                passwd.c_str(),
                dbName.c_str(),
                dbPort,
                nullptr,
                0
            );

            if(!conn){
                mysql_close(sql);
                ClosePool();
                std::cout << "Mysql Init failed" << std::endl;
                return false;
            }

            connQueue_.push(conn);
        }

        maxConn_ = ConnSize;
        useCount_ = 0;
        isClosed_ = false;
        return true;
    }

int SqlConnPool::GetFreeConnCount(){
    return maxConn_ - useCount_;
}

MYSQL* SqlConnPool::GetConn(){
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]{
        return isClosed_ || !connQueue_.empty();
    });

    if(isClosed_) return nullptr;

    MYSQL* conn = connQueue_.front();
    connQueue_.pop();
    ++useCount_;
    return conn;
}

bool SqlConnPool::FreeConn(MYSQL* conn){
    if(!conn) return false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if(isClosed_) return false;
        connQueue_.push(conn);
        useCount_--;
    }
    cv_.notify_one();
    return true;
}

void SqlConnPool::ClosePool(){
    std::lock_guard<std::mutex> lock(mtx_);
    if(isClosed_) return;
    while(!connQueue_.empty()){
        MYSQL* conn = connQueue_.front();
        connQueue_.pop();
        mysql_close(conn);
    }
    maxConn_ = 0;
    useCount_ = 0;
    isClosed_ = true;
    cv_.notify_all();
}

SqlConnRAII::SqlConnRAII(MYSQL** sql, SqlConnPool* connPool) : sql_(nullptr), connPool_(connPool){
    if(sql && connPool_){
        sql_ = connPool_->GetConn();
        *sql = sql_;
    }
}

SqlConnRAII::~SqlConnRAII(){
    if(sql_ && connPool_){
        connPool_->FreeConn(sql_);
    }
}