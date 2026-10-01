cmake_minimum_required(VERSION 3.20)

project(customfoldingplugin VERSION 1.0)

# Collega Qt6 scaricato dall'azione di setup
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Widgets)

add_library(customfoldingplugin MODULE)

target_sources(customfoldingplugin PRIVATE
    customfoldingplugin.cpp
    customfoldingplugin.h
)

target_link_libraries(customfoldingplugin PRIVATE
    Qt6::Core
    Qt6::Widgets
)

# Forza l'inclusione dei parametri di build per simulare l'ambiente KTextEditor
target_compile_definitions(customfoldingplugin PRIVATE -DKCOREADDONS_STATICPLUGIN)

# Percorsi specifici per l'installazione dal Microsoft Store
install(TARGETS customfoldingplugin DESTINATION "$ENV{LOCALAPPDATA}/Packages/KDEe.V.Kate_7vt06qxq7ptv8/LocalCache/Local/ktexteditor6/plugins")
install(FILES ui.rc DESTINATION "$ENV{LOCALAPPDATA}/Packages/KDEe.V.Kate_7vt06qxq7ptv8/LocalCache/Local/kxmlgui6/customfoldingplugin")
