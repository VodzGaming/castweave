cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Network REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/streamdock.cpp)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CASTWEAVE_VERSION="${PROJECT_VERSION}")

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

option(STREAMDOCK_BUILD_PREVIEW "Build the offscreen UI preview tool" OFF)
if(STREAMDOCK_BUILD_PREVIEW)
  add_executable(streamdock-preview src/streamdock.cpp)
  target_compile_definitions(
    streamdock-preview
    PRIVATE STREAMDOCK_PREVIEW CASTWEAVE_VERSION="${PROJECT_VERSION}"
  )
  target_link_libraries(streamdock-preview PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
endif()

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Advapi32)
  if(STREAMDOCK_BUILD_PREVIEW)
    target_link_libraries(streamdock-preview PRIVATE Advapi32)
  endif()
endif()
