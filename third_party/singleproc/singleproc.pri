# singleproc qmake 集成文件
#
# 可选接入方式（CMake 是默认构建）：在 .pro 里
#   include(third_party/singleproc/singleproc.pri)
# 之后即可 #include <singleproc/singleproc.hpp>。

INCLUDEPATH += $$PWD/include
HEADERS += $$PWD/include/singleproc/singleproc.hpp

SOURCES += $$PWD/src/singleproc.cpp
win32:SOURCES += $$PWD/src/singleproc_win32.cpp
else:SOURCES += $$PWD/src/singleproc_posix.cpp

CONFIG += c++11
