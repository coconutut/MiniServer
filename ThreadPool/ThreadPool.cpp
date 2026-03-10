#include <ThreadPool/ThreadPool.h>
using namespace std;

ThreadPool::ThreadPool(size_t threadCount) : stop(false){
    for(size_t i = 0; i < threadCount; i++){
        workers.emplace_back([this] {this->worker();});
    }
}

ThreadPool::~ThreadPool(){
    stop = true;
    condition.notify_all();
    for(auto &t : workers){
        if(t.joinable()) t.join();
    }
}

void ThreadPool::enqueue(function<void()> task){
    {
        lock_guard<mutex> lock(queueMutex);
        tasks.push(task);
    }
    condition.notify_one();
}

void ThreadPool::worker(){
    while(!stop){
        function<void()> task;
        {
            unique_lock<mutex> lock(queueMutex);
            condition.wait(lock, [this]{return stop || !tasks.empty();});
            if(stop && tasks.empty()) return;
            task = tasks.front();
            tasks.pop();
        }
        task();
    }
}