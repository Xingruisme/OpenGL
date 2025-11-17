cmake_minimum_required(VERSION 3.14)
project(FabricSimQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Try Qt6, fallback to Qt5
find_package(Qt6 COMPONENTS Widgets OpenGLWidgets QUIET)
if(NOT Qt6_FOUND)
  find_package(Qt5 COMPONENTS Widgets OpenGLWidgets REQUIRED)
  set(QT_LIBS Qt5::Widgets Qt5::OpenGLWidgets)
else()
  set(QT_LIBS Qt6::Widgets Qt6::OpenGLWidgets)
endif()

find_package(OpenGL REQUIRED)

add_executable(FabricSimQt
  src/main.cpp
  src/MainWindow.h
  src/MainWindow.cpp
  src/GLWidget.h
  src/GLWidget.cpp
  src/FabricSimulation.h
  src/FabricSimulation.cpp
)

target_include_directories(FabricSimQt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(FabricSimQt PRIVATE ${QT_LIBS} OpenGL::GL)

# Put shaders and assets into install tree (optional)
install(TARGETS FabricSimQt RUNTIME DESTINATION bin)
install(DIRECTORY shaders DESTINATION share/${PROJECT_NAME})
