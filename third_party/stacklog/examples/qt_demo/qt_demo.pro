QT += core
QT -= gui

CONFIG += console c++11
CONFIG -= app_bundle

TEMPLATE = app
TARGET = qt_demo
SOURCES += main.cpp

include(../../stacklog.pri)
