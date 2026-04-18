# mg_http_client Design Spec

**Date:** 2026-04-18  
**Status:** Approved

## Overview

基于 mongoose 的轻量 HTTP 客户端库，面向嵌入式/IoT 场景。单线程事件循环，回调式异步 API，支持文件上传、文件下载、POST 三种操作，内存和 CPU 占用最小化。

## Goals

- 零额外线程，完全非阻塞，调用方驱动 `mg_mgr_poll`
- 上传/下载均流式处理，内存中不缓冲完整文件
- 三个独立接口，职责清晰，互不耦合
- 提供完整压测报告：吞吐、p50/p95/p99 延迟、RSS 内存、CPU 时间

## Directory Structure

```
mg_http_client/
├── CMakeLists.txt
├── mongoose.h
├── mongoose.c
├── mhc.h
├── mhc.c
├── test/
│   ├── CMakeLists.txt
│   ├── server.py       # Python 测试服务器（标准库，无需额外安装）
│   ├── test_basic.c    # 功能测试
│   └── bench.c         # 压测程序
└── docs/
    ├── bench_report.md  # 压测结果（运行后生成）
    └── superpowers/specs/
```

## Public API

```c
// 完成回调：status > 0 为 HTTP 状态码，-1 为网络错误/超时
typedef void (*mhc_data_fn)(int status, void *userdata);

// 上传本地文件到 URL（库自己打开文件、流式读取、上传）
void mhc_upload(struct mg_mgr *mgr,
                const char *url,
                const char *filepath,
                mhc_data_fn cb,
                void *userdata);

// 从 URL 下载文件保存到本地路径（库自己写文件）
void mhc_download(struct mg_mgr *mgr,
                  const char *url,
                  const char *savepath,
                  mhc_data_fn cb,
                  void *userdata);

// POST 小 body（如 JSON 指令），响应状态通过回调返回
void mhc_post(struct mg_mgr *mgr,
              const char *url,
              const char *content_type,
              const void *body,
              size_t body_len,
              mhc_data_fn cb,
              void *userdata);
```

## Internal Design

### 独立结构体

每种操作有自己的私有结构体，存在堆上（~64 bytes），请求完成后在事件函数内 `free()`：

```c
struct mhc_upload_req { const char *url; struct mg_fd *fd; size_t fsize, fsent; mhc_data_fn cb; void *ud; uint64_t deadline; };
struct mhc_dl_req    { const char *url; const char *savepath; struct mg_fd *fd; mhc_data_fn cb; void *ud; uint64_t deadline; };
struct mhc_post_req  { const char *url; const void *body; size_t body_len; const char *ct; mhc_data_fn cb; void *ud; uint64_t deadline; };
```

### 三个独立事件函数

- `upload_fn()` — `MG_EV_CONNECT` 发 POST 头；`MG_EV_WRITE` 背压控制（`send.len < MG_IO_SIZE` 时才从文件读一块 `alloca(MG_IO_SIZE)`）；`MG_EV_HTTP_MSG` 触发回调
- `download_fn()` — `MG_EV_CONNECT` 发 GET；`MG_EV_HTTP_HDRS` 打开目标文件；`MG_EV_READ` 流式写文件，清空 recv buffer；`MG_EV_HTTP_MSG` 触发完成回调
- `post_fn()` — `MG_EV_CONNECT` 发 POST 头+body；`MG_EV_HTTP_MSG` 触发回调
- 三者均在 `MG_EV_POLL` 检查超时，`MG_EV_ERROR` 触发错误回调并释放结构体

### 内存控制

- 上传读缓冲：`alloca(MG_IO_SIZE)`，栈上 2KB，不 malloc
- 发送背压：仅在 `c->send.len < MG_IO_SIZE` 时追加数据，防止 send buffer 无限增长
- download 流式写磁盘，recv buffer 每次处理后清零（`c->recv.len = 0`）

## Build

```cmake
# 根 CMakeLists：静态库 libmhc
add_library(mhc STATIC mongoose.c mhc.c)
target_compile_options(mhc PRIVATE -O2 -Wall)

# test/CMakeLists：两个可执行文件
add_executable(test_basic test_basic.c)
target_link_libraries(test_basic mhc)

add_executable(bench bench.c)
target_link_libraries(bench mhc)
```

## Python Test Server

使用 Python 标准库 `http.server`，无需安装额外包，用本地 venv 启动：

- `POST /upload/<name>` — 保存到 `/tmp/mhc_test/`
- `GET  /download/<name>` — 从 `/tmp/mhc_test/` 返回文件
- `POST /echo` — 返回请求 body

## Benchmark Design

- 测试文件：程序自动生成 5MB 随机二进制文件
- 迭代次数：上传 100 次 + 下载 100 次
- 指标采集：

| 指标 | 方法 |
|------|------|
| 每次耗时 | `mg_millis()` 差值 |
| p50/p95/p99 | 排序后取分位数 |
| 平均吞吐 MB/s | 总字节 / 总时间 |
| 峰值 RSS | `/proc/self/status` VmRSS |
| CPU 用户态时间 | `getrusage(RUSAGE_SELF)` |

- 输出：打印到 stdout，同时写入 `docs/bench_report.md`
