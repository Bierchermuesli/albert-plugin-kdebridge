cmake_minimum_required(VERSION 3.16)
project(kdebridge VERSION 0.1.0)

find_package(Albert REQUIRED)

albert_plugin(QT Core DBus Gui Widgets)
