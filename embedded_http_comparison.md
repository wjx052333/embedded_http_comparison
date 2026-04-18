# 嵌入式 HTTP 库对比报告：Mongoose vs CivetWeb vs cpp-httplib

> 评估日期：2026-04-18  
> 评估维度：内存/CPU 占用、HTTP Client+Server 适用性、嵌入式环境兼容性

---

## 一、架构模型

| 维度 | Mongoose | CivetWeb | cpp-httplib |
|---|---|---|---|
| 并发模型 | 事件驱动（单线程 event loop） | 每连接一线程 | 每连接一线程（blocking） |
| C 标准库依赖 | 极少，可裸机运行 | 依赖 POSIX/pthreads | 依赖 C++11 STL + 线程 |
| 源码形态 | 单文件 `mongoose.c/h` | 多文件，有 CMake | 单头文件 `httplib.h` |
| 代码行数（本地版本） | ~34K | ~23K | ~20K |

---

## 二、内存占用

### Mongoose（最优）
- 事件驱动，所有连接共享单一调用栈；每个连接仅占 `struct mg_connection`（约 200 字节）
- 支持静态内存分配，可在无 `malloc` 的裸机上运行
- 典型嵌入式使用 RAM < 8KB
- 支持 ESP32、STM32、NXP、Nordic 等主流 MCU，以及 FreeRTOS、RT-Thread、lwIP

### CivetWeb（中等）
- 每个连接需要独立线程栈，默认约 64KB/线程
- 支持线程池配置，可控，但内存底线远高于 Mongoose
- 依赖 POSIX 线程，无法裸机运行

### cpp-httplib（最差，嵌入式角度）
- `std::string`、`std::map`、`std::vector` 等 STL 容器有额外 heap 分配开销
- 需要完整 C++ 运行时和异常支持（`-fno-exceptions` 需额外适配）
- 无法运行于无 OS 环境

---

## 三、CPU 占用

| | Mongoose | CivetWeb | cpp-httplib |
|---|---|---|---|
| 空闲时 | 几乎零（epoll/select 睡眠） | 线程阻塞睡眠，接近零 | 同 CivetWeb |
| 高并发 | 单线程无切换开销，优势明显 | 线程切换开销随连接数增长 | 同 CivetWeb，另有 STL 分配开销 |
| 低频轮询场景 | 最适合 | 可接受 | 可接受 |

---

## 四、HTTP Client 支持

| | Mongoose | CivetWeb | cpp-httplib |
|---|---|---|---|
| Client API | `mg_http_connect()` 回调式异步 | **无原生 HTTP client** | `Client::Get/Post()` 同步阻塞 |
| 使用难度 | 较复杂（回调/事件驱动） | 不支持，需手工实现 | 最简单 |
| 嵌入式适用性 | 适合（无阻塞，资源可控） | 不适合 | 仅限有 OS 环境 |

**CivetWeb 不提供 HTTP client，是其在 client+server 场景中的关键缺陷。**

---

## 五、综合评分

| 评估项 | Mongoose | CivetWeb | cpp-httplib |
|---|---|---|---|
| 裸机/RTOS 支持 | ★★★★★ | ★☆☆☆☆ | ☆☆☆☆☆ |
| 内存占用 | ★★★★★ | ★★★☆☆ | ★★☆☆☆ |
| CPU 效率 | ★★★★★ | ★★★☆☆ | ★★★☆☆ |
| HTTP Client | ★★★☆☆ | ☆☆☆☆☆ | ★★★★★ |
| HTTP Server | ★★★★★ | ★★★★☆ | ★★★☆☆ |
| API 易用性 | ★★★☆☆ | ★★★★☆ | ★★★★★ |

---

## 六、结论与建议

### 嵌入式（无 OS 或 RTOS）→ **Mongoose**，无竞争
- 唯一支持裸机/FreeRTOS/lwIP 的方案
- 单线程事件循环内存最低
- Client + Server 均原生支持

### 嵌入式 Linux（有 OS，资源较充裕）
- 若需 client+server 一体：仍推荐 **Mongoose**，API 一致性好
- 若 server 为主、client 偶发：**CivetWeb** 作 server + **cpp-httplib** 作 client 可组合使用，但引入两个依赖

### 仅需 HTTP Server，有 OS → **CivetWeb**
- 比 cpp-httplib 更轻量，线程模型对嵌入式 Linux 更可控
- API 较 Mongoose 更直观

### 不推荐 cpp-httplib 用于嵌入式 Server
- STL 开销和阻塞模型不适合资源受限场景
- 作为 client 库在 Linux 上可接受
