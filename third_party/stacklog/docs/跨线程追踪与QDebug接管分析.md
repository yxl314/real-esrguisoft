# 跨线程追踪与 QDebug 接管分析

当前版本：`0.2.0`

## 1. 结论

- **自动跨线程追踪：不建议实现，也不建议把它作为默认行为。**
- 跨线程追踪仍有价值，但应采用**显式 capture/restore**，而不是隐式自动传播。
- **Qt QDebug 接管：技术上可行，适合做可选适配器，不进入核心库。**
- QDebug 适配器默认不安装，避免与 Qt 项目自己的消息处理器冲突。

### 当前实现状态

- 显式跨线程 `trace_frame` / `capture_trace()` / `restore_scope` / `STACKLOG_RESTORE_TRACE` 已实现
- 跨线程节点 ID 唯一性已修复
- 自动跨线程传播仍未实现，且不建议实现
- QDebug 接管仍处于方案分析阶段，未进入核心库

## 2. 当前行为

核心库的 trace_scope 使用 `thread_local` 栈：

```text
主线程：trace=t-main, node=main-1, parent=-
工作线程：trace=t-worker, node=worker-1, parent=-
```

工作线程不会自动继承主线程的 trace。`examples/qt_worker_demo` 的真实输出已经验证：

```text
主线程   t-...-1  t-...-1-1  parent -
工作线程 t-...-2  t-...-2-1  parent -
子作用域 t-...-2  t-...-2-2  parent t-...-2-1
```

这是当前设计的有意结果，不是遗漏。

## 3. 自动跨线程追踪的可行性

### 3.1 方案 A：依赖 TLS 自动继承

不可行。

- C++11 的 `thread_local` 不会从创建线程继承
- pthread TLS 没有标准的“继承父线程 TLS”语义
- 平台相关实现会把核心库拖入不可移植代码
- 线程池线程复用时会读到上一个任务的陈旧上下文，风险高于收益

### 3.2 方案 B：全局线程父节点表

不可行或不可靠。

- 无法可靠知道任意线程的“创建者”和“创建时刻”
- `std::thread`、`QThread`、线程池创建的线程没有统一入口
- 线程 ID 会复用，需要生命周期管理
- 并发 start/stop 时容易产生悬挂父节点

### 3.3 方案 C：Qt 事件/线程信号自动传播

只能做到“通过指定入口创建”的有限自动，不是全局自动。

可以设计一个 Qt 辅助入口：

```cpp
QThread* thread = stacklog::qt::createThread("worker");
```

或：

```cpp
const stacklog::trace_frame frame = stacklog::capture_trace();
QThreadPool::globalInstance()->start([frame] {
    STACKLOG_RESTORE_TRACE(frame, "task");
});
```

问题：

- 无法拦截用户直接创建的 `QThread`
- 无法拦截 Qt 内部线程、QtConcurrent 线程池、第三方线程
- 线程池任务如果没有显式 restore，日志会落回“无线索”的根节点
- 自动安装全局事件过滤器会改变 Qt 程序行为，不适合作为库的默认动作

### 3.4 方案 D：QDebug 消息上下文传播

不可行。

- `qInstallMessageHandler` 能拿到日志消息，但不能恢复业务逻辑的 trace_scope
- 它只适合把 QDebug 内容转成 stacklog 记录，不适合做追踪传播

### 3.5 结论

全局自动跨线程追踪没有一个可靠、可移植、无副作用的实现方式。

如果强行做“自动”，实际效果通常是：

- 覆盖不到用户自己创建的线程
- 在线程池复用场景下产生错误父子关系
- 给使用方造成“已经自动追踪”的错觉

因此推荐显式 API。

## 4. 已实现的显式跨线程 API

当前已实现：

```cpp
struct trace_frame
{
    std::string trace_id;
    std::string node_id;
    std::string parent_id;
    std::string scope;

    bool valid() const noexcept;
};

trace_frame capture_trace();

class restore_scope
{
public:
    explicit restore_scope(const trace_frame& frame);
    ~restore_scope() noexcept;
};
```

使用方式：

```cpp
// 主线程：捕获上下文
const stacklog::trace_frame frame = stacklog::capture_trace();

// 把 frame 带到工作线程
QThreadPool::globalInstance()->start([frame] {
    STACKLOG_RESTORE_TRACE(frame, "worker-task");
    STACKLOG_INFO() << "worker task";
});
```

线程池场景必须每次任务捕获、每次任务恢复，不能把上下文缓存在线程上。

## 5. QDebug 接管可行性

### 5.1 核心机制

Qt 提供：

```cpp
QtMessageHandler qInstallMessageHandler(QtMessageHandler handler);
```

处理器签名：

```cpp
void handler(QtMsgType type,
             const QMessageLogContext& context,
             const QString& message);
```

可以在处理器中：

1. 映射 Qt 消息等级
2. 读取 `context.file`、`context.line`、`context.category`
3. 将 `QString` 转成 UTF-8 的 `std::string`
4. 调用 stacklog 的内部写入接口
5. 保留或转发旧处理器

### 5.2 等级映射

```text
QtDebugMsg     -> Debug
QtInfoMsg      -> Info
QtWarningMsg   -> Warn
QtCriticalMsg  -> Error
QtFatalMsg     -> Error，然后保持 qFatal 的终止语义
```

`qFatal` 不能简单吞掉后继续运行。

### 5.3 文件行号

需要编译时定义：

```qmake
DEFINES += QT_MESSAGELOGCONTEXT
```

CMake 对应：

```cmake
target_compile_definitions(app PRIVATE QT_MESSAGELOGCONTEXT)
```

否则 `QMessageLogContext` 的 file/line 可能为空，日志只能使用 `-` 占位。

### 5.4 必须处理的边界

- **递归**：处理器内部不能调用 `qDebug`，只能调用 stacklog 或 `std::fprintf`
- **旧处理器**：安装时保存旧处理器；卸载时恢复
- **重复安装**：安装函数必须幂等
- **多线程**：Qt 处理器可能被任意线程调用；stacklog 写入本身线程安全
- **消息换行**：QDebug 消息通常带尾随换行，需要去掉；内部换行由 stacklog 转义
- **空消息**：仍应产生一条记录，不能丢失
- **初始化失败**：处理器要能降级到 stderr，不能递归崩溃
- **QDebug 上下文**：`context.file` 可能为 null，`context.line` 可能为 0
- **qFatal**：记录后调用旧处理器或 `abort()`，保持 Qt 语义

### 5.5 推荐形态

不作为核心库默认功能，单独提供可选 Qt 适配器：

```text
include/stacklog/qt_message_handler.h
src/qt_message_handler.cpp
stacklog_qt.pri
```

API：

```cpp
namespace stacklog
{
namespace qt
{

void installMessageHandler();
void uninstallMessageHandler();

} // namespace qt
} // namespace stacklog
```

要求：

- 核心库仍然零 Qt 依赖
- 只有显式包含 Qt 适配器的项目才引入 Qt Core
- 默认不接管 QDebug
- 与项目已有 handler 的冲突在文档中明确说明

### 5.6 可行性结论

QDebug 接管可行，且实现成本可控，但建议作为后续的独立适配器，不改变当前核心 API 和日志格式。

## 6. 下一步建议

1. 保持显式 capture/restore，不实现自动跨线程传播。
2. 若项目确实依赖 `qDebug/qInfo/qWarning`，再增加独立的 Qt 消息处理器适配器。
3. QDebug 适配器必须通过真实 Qt 示例验证，而不是只写单元测试。
