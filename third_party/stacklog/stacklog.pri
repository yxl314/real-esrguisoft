# stacklog qmake 集成文件
#
# 可选接入方式（CMake 是默认构建）：在 .pro 里
#   include(third_party/stacklog/stacklog.pri)
# 之后即可 #include <stacklog/stacklog.hpp>。
#
# 库源码兼容 C++11；调用方工程开启 C++17 时会自动启用 std::filesystem，
# 从而正确写出 UTF-8（中文）日志路径。

INCLUDEPATH += $$PWD/include
HEADERS += $$PWD/include/stacklog/stacklog.hpp $$PWD/include/stacklog/version.hpp
SOURCES += $$PWD/src/stacklog.cpp
unix:LIBS += -lpthread
