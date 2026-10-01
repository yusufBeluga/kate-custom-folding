cmake_minimum_required(VERSION 3.20)

project(customfoldingplugin VERSION 1.0)

# Cerca solo Qt6 di sistema (già presente nelle VM di GitHub Actions)
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

# Definizioni per simulare l'ambiente di build nativo di Kate
target_compile_definitions(customfoldingplugin PRIVATE 
    -DQT_NO_KEYWORDS
    -DKCOREADDONS_STATICPLUGIN
)

# Percorsi di installazione utente per il Microsoft Store
install(TARGETS customfoldingplugin DESTINATION "$ENV{LOCALAPPDATA}/Packages/KDEe.V.Kate_7vt06qxq7ptv8/LocalCache/Local/ktexteditor6/plugins")
install(FILES ui.rc DESTINATION "$ENV{LOCALAPPDATA}/Packages/KDEe.V.Kate_7vt06qxq7ptv8/LocalCache/Local/kxmlgui6/customfoldingplugin")
