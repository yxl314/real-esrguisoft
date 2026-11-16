# stacklog

一个面向 C++ / Qt 项目的轻量日志库。

特性：

- DEBUG / INFO / WARN / ERROR 四级日志
- 线程安全
- 按文件大小轮转
- 运行期检测日志文件被外部删除/轮转，并自动重开
- 归档失败时继续写当前文件，写失败有有限重试
- `trace_scope` 追踪调用链，日志包含当前节点和父亲节点 ID
- 显式 `trace_frame` + `restore_scope` 支持跨线程接续
- C++11，无 Qt / 第三方依赖（目录创建使用薄平台封装）
- CMake 与 qmake 集成

## 目录结构

```text
stacklog/
├── include/stacklog/stacklog.hpp
├── src/stacklog.cpp
├── CMakeLists.txt
├── stacklog.pri
├── check.sh
├── .clang-format
├── .clang-tidy
├── ci.toml
├── standardize.toml
├── audit.toml
├── LICENSE
├── examples/
│   ├── cpp_demo/
│   ├── qt_demo/
│   └── qt_worker_demo/
├── benchmarks/
├── docs/
└── tests/
```

## 快速开始

```cpp
#include <stacklog/stacklog.hpp>

int main()
{
    stacklog::config cfg;
    cfg.dir = "logs";
    cfg.base_name = "app";
    cfg.min_level = stacklog::level::info;

    if (!stacklog::init(cfg))
    {
        return 1;
    }

    STACKLOG_INFO() << "application started";

    {
        STACKLOG_TRACE("load-config");
        STACKLOG_INFO() << "loading config";

        {
            STACKLOG_TRACE("parse-file");
            STACKLOG_INFO() << "parsing file";
        }
    }

    stacklog::shutdown();
    return 0;
}
```

对应日志：

```text
2026-11-10 16:56:38.485|INFO|t-...-1|t-...-1-t1-1|-|load-config|main.cpp:15|loading config
2026-11-10 16:56:38.485|INFO|t-...-1|t-...-1-t1-2|t-...-1-t1-1|parse-file|main.cpp:18|parsing file
```

`parse-file` 的 `parent_id` 就是 `load-config` 的 `node_id`。

## 日志格式

```text
时间|级别|trace_id|node_id|parent_id|scope|file:line|message
```

空字段使用 `-` 占位。消息中的 `|`、反斜杠、回车和换行会转义，保证一行一条日志。

## 日志级别

```cpp
STACKLOG_DEBUG() << "debug";
STACKLOG_INFO() << "info";
STACKLOG_WARN() << "warn";
STACKLOG_ERROR() << "error";

stacklog::set_level(stacklog::level::debug);
```

初始化配置中的 `min_level` 是运行期最低级别。`level::off` 关闭日志。

## 文件轮转

默认：

```text
单文件上限 5 MiB
保留 5 份历史文件
```

文件命名：

```text
app.log
app.log.1
app.log.2
...
app.log.5
```

超过上限时关闭当前文件，依次重命名归档，再重新打开 `app.log`。

运行期每写入 1024 条会检查一次当前文件是否仍在原路径上。文件被外部删除、截断或重命名后，会重新打开并继续写；归档重命名失败时不会丢弃当前文件，而是继续追加，并在文件再增长一个 `max_file_size` 后重试轮转。

配置：

```cpp
stacklog::config cfg;
cfg.max_file_size = 1024 * 1024;
cfg.max_files = 10;
```

只做按大小轮转，不做按天轮转、压缩或多进程协调。

## 追踪作用域

```cpp
STACKLOG_TRACE("name");
```

- 构造时入栈，析构时出栈
- 同一线程内自动形成父子关系
- 根节点 `parent_id` 为 `-`
- 子节点 `parent_id` 为父节点 `node_id`
- 同一行不能重复使用；不同行可正常嵌套

## 跨线程追踪

显式捕获与恢复：

```cpp
const stacklog::trace_frame frame = stacklog::capture_trace();

// 把 frame 传给工作线程或线程池任务
QThreadPool::globalInstance()->start([frame] {
    STACKLOG_RESTORE_TRACE(frame, "worker-task");
    STACKLOG_INFO() << "worker doing task";
});
```

`STACKLOG_RESTORE_TRACE(frame, name)` 等价于：

```cpp
restore_scope restore(frame);
trace_scope scope(name);
```

要求：

- 必须显式把 `trace_frame` 传到目标线程
- 每个线程池任务各自 capture/restore
- `restore_scope` 必须是作用域最外层
- 不自动传播，不依赖 TLS 继承

## 初始化失败

`init()` 返回 `false` 时，日志降级到 stderr。初始化失败的具体错误会立即打印；运行期降级提示只打印一次。日志库不会因为初始化失败而终止进程。

## CMake 集成

作为子目录：

```cmake
add_subdirectory(stacklog)
target_link_libraries(your_target PRIVATE stacklog::stacklog)
```

```cpp
#include <stacklog/stacklog.hpp>
```

安装后使用：

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix /usr/local
```

```cmake
find_package(stacklog 0.2 REQUIRED)
target_link_libraries(your_target PRIVATE stacklog::stacklog)
```

版本宏：

```cpp
STACKLOG_VERSION_STRING
STACKLOG_VERSION_MAJOR
STACKLOG_VERSION_MINOR
STACKLOG_VERSION_PATCH
```

## qmake 集成

在 `.pro` 文件中：

```qmake
include(path/to/stacklog/stacklog.pri)
```

`stacklog.pri` 已包含头文件路径、`src/stacklog.cpp` 和 pthread 链接。

## 构建与测试

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

要求：C++11、CMake 3.16+；qmake 集成使用 Qt 5/6 的 qmake。

测试覆盖：

```text
格式、转义、空消息、级别切换
轮转、max_files=1、超长单行、归档内容完整
三层父子关系、多根、作用域结束
多线程并发写
非法配置和降级路径
跨线程 capture/restore、并发 restore、无效帧
外部删除、轮转失败、写失败重试
并发轮转压力
CMake install + find_package + consumer
qmake + Qt 工作线程 trace/轮转断言
```

当前有 qmake 时 CTest 共 10 项测试。

## Qt 工作线程示例

```bash
cd examples/qt_worker_demo
qmake qt_worker_demo.pro
make -j
./qt_worker_demo
```

示例使用：

- 主线程捕获 `trace_frame`
- `QThread` 工作线程用 `restore_scope` 接续同一条 trace
- 工作线程内部继续创建 trace_scope
- 用户可写日志目录
- 小文件上限触发轮转

日志位于：

```text
QStandardPaths::AppLocalDataLocation/stacklog/stacklog-qt-worker-demo/
```

## 工程检查

项目根目录提供 `ci.toml`、`standardize.toml` 和 `audit.toml`，用外部工具替代托管 CI。排版和命名规则与 C++ 标准库一致：类型、函数和变量全部 lower_snake_case，私有成员使用尾随下划线，宏使用 UPPER_CASE。clang-tidy 开启 `modernize-*`，但排除会建议 C++14/17 设施、宏转 enum 及大范围风格改写的检查。

```bash
# 构建 + 测试（ci.toml 同时构建 benchmark）
python3 ../cxx_ci/ci/ci.py ci.toml

# 排版 + clang-tidy 检查
python3 ../cxx_ci/standardize/standardize.py --check

# cxx_audit：clang-tidy / CPD / ASan / Valgrind
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml all
```

`audit.toml` 默认启用 ASan 运行。若在受限容器中 ASan 运行时无法初始化，
把 `[asan].enabled` 改为 `false`。

## 性能基准

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTACKLOG_BUILD_BENCHMARKS=ON
cmake --build build -j --target stacklog_bench
./build/benchmarks/stacklog_bench 200000 1
./build/benchmarks/stacklog_bench 200000 4
```

同步写入仍是默认方案；当前基准没有证明需要引入异步日志。方法与样例结果见 `docs/性能基准.md`。

## 项目设计

当前项目设计快照见：

```text
docs/项目设计.md
```

## 版本规划

后续版本范围和发布条件见：

```text
docs/版本规划.md
```

运行示例：

```bash
./build/examples/cpp_demo/stacklog_cpp_demo
```

qmake 示例：

```bash
cd examples/qt_demo
qmake qt_demo.pro
make -j
./qt_demo
```

工作线程示例：

```bash
cd examples/qt_worker_demo
qmake qt_worker_demo.pro
make -j
./qt_worker_demo
```

## 当前版本

```text
0.2.0
```

当前开发分支已包含 0.4 可靠性增强（外部删除恢复、归档失败处理、写失败重试、压力测试和基准），但尚未作为 0.4.0 发布；0.3 Qt 适配器仍未开始。

当前版本支持显式跨线程追踪：

- `trace_frame` 帧快照
- `capture_trace()` 捕获
- `restore_scope` RAII 接续
- `STACKLOG_RESTORE_TRACE` 宏
- 跨线程节点 ID 唯一性

不支持自动跨线程传播；跨线程必须显式传递帧。

## 设计边界

明确不做：

- 异步日志
- 网络日志
- 多进程文件锁
- 按天轮转
- 日志压缩
- Qt 翻译
- 默认 QDebug 接管（可选适配器见分析文档）
- 跨线程自动追踪恢复
- 错误码体系

这些不属于当前轻量日志库的核心需求。

完整设计说明见：

```text
docs/项目设计.md
docs/跨线程追踪与QDebug接管分析.md
docs/版本规划.md
```

## 许可证

MIT，见 `LICENSE`。
