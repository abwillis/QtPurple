cmake_minimum_required(VERSION 3.16)

project(qtpurple VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 6 is preferred when both are installed; -DQTPURPLE_QT5=ON forces Qt 5.
option(QTPURPLE_QT5 "Build against Qt 5 even if Qt 6 is available" OFF)
if(QTPURPLE_QT5)
    find_package(QT NAMES Qt5 REQUIRED COMPONENTS Widgets)
else()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

if(QT_VERSION_MAJOR EQUAL 5 AND Qt5Widgets_VERSION VERSION_LESS 5.10)
    message(FATAL_ERROR "Qt 5.10 or newer is required (found ${Qt5Widgets_VERSION})")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PURPLE REQUIRED IMPORTED_TARGET purple>=2.10.0 glib-2.0 gobject-2.0)

set(QTPURPLE_SOURCES
    src/main.cpp
    src/common.cpp          src/common.h
    src/core.cpp            src/core.h
    src/eventloop.cpp       src/eventloop.h
    src/blistwindow.cpp     src/blistwindow.h
    src/blistdialogs.cpp    src/blistdialogs.h
    src/accountsdialog.cpp  src/accountsdialog.h
    src/conversationwindow.cpp src/conversationwindow.h
    src/requestui.cpp       src/requestui.h
    src/notifyui.cpp        src/notifyui.h
    src/transferswindow.cpp src/transferswindow.h
    src/pluginsdialog.cpp   src/pluginsdialog.h
    src/debugwindow.cpp     src/debugwindow.h
)

add_executable(qtpurple ${QTPURPLE_SOURCES})

# GLib uses "signals" as an identifier; keep Qt's keywords out of the way.
target_compile_definitions(qtpurple PRIVATE QT_NO_KEYWORDS)

target_link_libraries(qtpurple PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    PkgConfig::PURPLE
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qtpurple PRIVATE -Wall -Wextra -Wno-unused-parameter
                                            -Wno-missing-field-initializers)
endif()

include(GNUInstallDirs)
install(TARGETS qtpurple RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
