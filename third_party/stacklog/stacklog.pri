# stacklog qmake 集成文件
INCLUDEPATH += $$PWD/include
HEADERS += $$PWD/include/stacklog/stacklog.hpp $$PWD/include/stacklog/version.hpp
HEADERS += $$PWD/src/platform.hpp
SOURCES += $$PWD/src/stacklog.cpp
win32:SOURCES += $$PWD/src/platform_win32.cpp
else:SOURCES += $$PWD/src/platform_posix.cpp
CONFIG += c++11
unix:LIBS += -lpthread
