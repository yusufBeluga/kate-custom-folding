cmake_minimum_required(VERSION 3.20)
project(customfoldingplugin VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Librerie VERE di Kate (KDE Frameworks 6), non uno stub:
# il plugin deve chiamare il codice di KTextEditor che Kate ha già caricato.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6 REQUIRED COMPONENTS TextEditor CoreAddons XmlGui Config)

add_library(customfoldingplugin MODULE
    customfoldingplugin.cpp
    customfoldingplugin.h
    plugin.qrc
)

target_link_libraries(customfoldingplugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KF6::TextEditor
    KF6::CoreAddons
    KF6::XmlGui
    KF6::ConfigCore
)

# Kate cerca i plugin in <percorso plugin Qt>/kf6/ktexteditor
install(TARGETS customfoldingplugin DESTINATION plugins/kf6/ktexteditor)
