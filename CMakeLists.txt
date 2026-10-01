cmake_minimum_required(VERSION 3.20)

project(customfoldingplugin VERSION 1.0)

find_package(ECM 6.22.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Qt6 6.10.1 CONFIG REQUIRED COMPONENTS Core Widgets)
find_package(KF6 6.22.0 REQUIRED COMPONENTS TextEditor XmlGui CoreAddons)

add_library(customfoldingplugin MODULE)

target_sources(customfoldingplugin PRIVATE
    customfoldingplugin.cpp
    customfoldingplugin.h
)

kcoreaddons_target_static_plugins(customfoldingplugin)

target_link_libraries(customfoldingplugin PRIVATE
    Qt6::Core
    Qt6::Widgets
    KF6::TextEditor
    KF6::XmlGui
    KF6::CoreAddons
)

# DESTINAZIONE PER MICROSOFT STORE (LOCAL USER APPDATA)
# I plugin e le interfacce KXMLGui utente vengono salvati in AppData\Local
install(TARGETS customfoldingplugin DESTINATION "$ENV{LOCALAPPDATA}/Packages/KDEe.V.Kate_7vt06qxq7ptv8/LocalCache/Local/ktexteditor6/plugins")
install(FILES ui.rc DESTINATION "$ENV{LOCALAPPDATA}/Packages/KDEe.V.Kate_7vt06qxq7ptv8/LocalCache/Local/kxmlgui6/customfoldingplugin")
