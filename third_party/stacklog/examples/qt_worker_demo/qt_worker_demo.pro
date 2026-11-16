QT += core
QT -= gui

CONFIG += console c++11
CONFIG -= app_bundle

TEMPLATE = app
TARGET = qt_worker_demo
SOURCES += main.cpp worker.cpp
HEADERS += worker.h

include(../../stacklog.pri)
