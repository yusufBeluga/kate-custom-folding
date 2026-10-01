cmake_minimum_required(VERSION 3.20)
project(customfoldingplugin VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt deve avere la stessa versione di quello di Kate (6.10.1):
# Kate rifiuta i plugin compilati con un Qt più recente.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# KF6_ROOT = cartella dove sono stati estratti i pacchetti KDE Frameworks
# (stessa versione di quella di Kate). Usiamo direttamente header e .lib
# invece dei file CMake di KF6, che richiederebbero decine di altre dipendenze.
set(KF6_ROOT "" CACHE PATH "Cartella con include/KF6 e lib/KF6*.lib")
if(NOT KF6_ROOT OR NOT EXISTS "${KF6_ROOT}/include/KF6")
    message(FATAL_ERROR "Imposta -DKF6_ROOT=<cartella con include/KF6 e lib>")
endif()

file(GLOB KF6_INCLUDE_CANDIDATES LIST_DIRECTORIES true "${KF6_ROOT}/include/KF6/*")
set(KF6_INCLUDE_DIRS "${KF6_ROOT}/include/KF6")
foreach(dir ${KF6_INCLUDE_CANDIDATES})
    if(IS_DIRECTORY "${dir}")
        list(APPEND KF6_INCLUDE_DIRS "${dir}")
    endif()
endforeach()

set(KF6_LIBRARIES)
foreach(lib KF6TextEditor KF6Parts KF6XmlGui KF6ConfigCore KF6CoreAddons KF6SyntaxHighlighting)
    find_library(${lib}_LIB NAMES ${lib} PATHS "${KF6_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    list(APPEND KF6_LIBRARIES "${${lib}_LIB}")
endforeach()

add_library(customfoldingplugin MODULE
    customfoldingplugin.cpp
    customfoldingplugin.h
    plugin.qrc
)

target_include_directories(customfoldingplugin PRIVATE ${KF6_INCLUDE_DIRS})

target_link_libraries(customfoldingplugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${KF6_LIBRARIES}
)

# Kate cerca i plugin in <percorso plugin Qt>/kf6/ktexteditor
install(TARGETS customfoldingplugin DESTINATION plugins/kf6/ktexteditor)
