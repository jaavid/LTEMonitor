cmake_minimum_required(VERSION 3.2)
project(LTEMonitor)

set (lte_monitor_VERSION_MAJOR 0)
set (lte_monitor_VERSION_MINOR 18)
 
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#set(CMAKE_BUILD_TYPE Release)

if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wsign-conversion)
else()
    add_compile_options(/W1)
endif()
SET(Boost_USE_STATIC_LIBS ON)
add_subdirectory(Simple-Web-Server)

find_package(Boost COMPONENTS log program_options filesystem REQUIRED)
include_directories( ${Boost_INCLUDE_DIRS} ${PROJECT_BINARY_DIR})
if(APPLE)
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
		find_package(OpenSSL)
		if(OPENSSL_FOUND)
			include_directories(${OPENSSL_INCLUDE_DIR})
		endif()
endif()

configure_file (
  "${PROJECT_SOURCE_DIR}/Version.h.in"
  "${PROJECT_BINARY_DIR}/Version.h"
)

set(LTE_MONITOR_RESOURCE_DIR ${PROJECT_BINARY_DIR}/resources)
if(EXISTS "${PROJECT_SOURCE_DIR}/lmgui/resources")
    set(LTE_MONITOR_UI_SOURCE_DIR "${PROJECT_SOURCE_DIR}/lmgui/resources")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/ui_resources")
    set(LTE_MONITOR_UI_SOURCE_DIR "${PROJECT_SOURCE_DIR}/ui_resources")
else()
    message(FATAL_ERROR "No UI resources directory found.")
endif()

add_custom_target(angular_ui_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LTE_MONITOR_RESOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${LTE_MONITOR_UI_SOURCE_DIR}
            ${LTE_MONITOR_RESOURCE_DIR}
    COMMENT "Copying Angular UI assets"
)

add_library(LTEClasses ProgramOptions.cpp HuaweiClient.cpp AngularServer.cpp )
add_dependencies(LTEClasses angular_ui_assets)
target_compile_definitions(LTEClasses PUBLIC ANGULAR_RESOURCE_DIR="${LTE_MONITOR_RESOURCE_DIR}")
target_link_libraries(LTEClasses INTERFACE simple-web-server ${Boost_LIBRARIES})
add_executable(LTEMonitor main.cpp)

target_link_libraries(LTEMonitor LTEClasses)

enable_testing()
add_subdirectory(tests)

install(DIRECTORY ${LTE_MONITOR_RESOURCE_DIR}/ DESTINATION resources)
