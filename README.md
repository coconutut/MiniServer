# MiniServer

一个基于 C++17 的轻量级 Reactor Web 服务器示例项目，使用 epoll + 非阻塞 IO 处理连接，线程池执行业务逻辑，MySQL 连接池处理数据库访问，并通过 eventfd 将业务线程结果回传主事件循环。

## 项目特性

- 事件驱动网络模型：`epoll` + `EPOLLET` + `EPOLLONESHOT`
- 非阻塞 Socket：主线程负责连接管理与读写事件分发
- 自定义线程池：业务逻辑异步执行，避免阻塞 Reactor
- MySQL 连接池：减少重复建连开销
- 基础 HTTP 解析：请求行、Header、Body（支持 `Content-Length`）
- 简单业务路由：
  - `GET /login` 返回登录页面
  - `GET /style.css` 返回样式文件
  - `POST /login` 登录校验
  - `POST /register` 用户注册
- 密码安全处理：`PBKDF2-HMAC-SHA256` + 随机盐（OpenSSL）

## 技术栈

- C++17
- CMake (>= 3.10)
- Linux 系统调用：`epoll`、`eventfd`、`socket`
- MySQL C API（libmysqlclient 或 libmariadb）
- OpenSSL（Crypto）

## 目录结构

```text
MiniServer/
├── main.cpp
├── MiniServer.cpp / MiniServer.h      # 主 Reactor 与事件循环
├── HttpConn/                          # HTTP 连接对象与解析
├── ThreadPool/                        # 线程池
├── SqlConnPool/                       # MySQL 连接池 + RAII
├── services/                          # 业务路由与处理
├── utils/                             # 工具函数（表单解析、哈希、文件读取等）
├── www/                               # 静态资源（登录页、CSS）
├── post_login.lua                     # wrk 压测脚本
└── CMakeLists.txt
```

## 环境依赖

以 Ubuntu/Debian 为例：

```bash
sudo apt update
sudo apt install -y build-essential cmake libmysqlclient-dev libssl-dev
```

如果你使用 MariaDB 开发包，也可替代 `libmysqlclient-dev`。

## 数据库准备

示例 SQL：

```sql
CREATE DATABASE IF NOT EXISTS mydb CHARACTER SET utf8mb4;
USE mydb;

CREATE TABLE IF NOT EXISTS users (
  id BIGINT PRIMARY KEY AUTO_INCREMENT,
  username VARCHAR(64) NOT NULL UNIQUE,
  password_hash VARCHAR(128) NOT NULL,
  password_salt VARCHAR(64) NOT NULL,
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

## 编译与运行

```bash
mkdir -p build
cd build
cmake ..
cmake --build -j
./main
```

默认监听端口：`7896`

浏览器访问：

```text
http://127.0.0.1:7896/login
```

## 接口说明

### 1) 获取登录页

```http
GET /login
```

成功返回 `200 text/html`。

### 2) 登录

```http
POST /login
Content-Type: application/x-www-form-urlencoded

username=alice&password=123456
```

可能返回：

- `200 success login`
- `401 user not found`
- `401 wrong password`
- `400 username/password required`

### 3) 注册

```http
POST /register
Content-Type: application/x-www-form-urlencoded

username=alice&password=123456
```

可能返回：

- `200 register success`
- `409 user already exists`
- `400 username/password required`

## 压测示例（wrk）

项目自带 `post_login.lua`，示例：

```bash
wrk -t4 -c100 -d30s -s post_login.lua http://127.0.0.1:7896/login
```

## 已知限制

当前实现更偏向教学/实验性质，尚有以下改进空间：

- 数据库连接参数与 Web 根目录路径在代码中硬编码
- 业务路由较少，仅覆盖登录/注册演示
- 错误处理、日志、配置管理、测试体系不完善
- HTTP 能力有限（未完整覆盖协议细节）

## 后续优化建议

- 引入配置文件（端口、DB、静态目录、线程数）
- 增加日志模块与可观测性指标
- 补充单元测试/集成测试
- 增加更多路由与静态资源处理能力
- 优化连接生命周期管理（如 Keep-Alive 完整支持）
