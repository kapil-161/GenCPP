cmake_minimum_required(VERSION 3.16)
project(GeneticsEditor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime linking for MinGW
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
endif()

# Find required Qt6 components (no Charts needed for this app)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Enable automatic processing of Qt MOC/UIC/RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/DssatProParser.cpp
    src/DetailCdeParser.cpp
    src/CulParser.cpp
    src/EcoParser.cpp
    src/SpeEditor.cpp
    src/SpeSyntaxHighlighter.cpp
    src/CulTableModel.cpp
    src/EcoTableModel.cpp
    src/BackupManager.cpp
)

# Header files
set(HEADERS
    include/Config.h
    include/MainWindow.h
    include/DssatProParser.h
    include/DetailCdeParser.h
    include/CulParser.h
    include/EcoParser.h
    include/SpeEditor.h
    include/SpeSyntaxHighlighter.h
    include/CulTableModel.h
    include/EcoTableModel.h
    include/BackupManager.h
)

# Add version resource for Windows
if(WIN32)
    set(RESOURCE_FILES resources/version.rc)
endif()

# Create executable
add_executable(GeneticsEditor ${SOURCES} ${HEADERS} ${RESOURCE_FILES})

# Link Qt6 libraries
target_link_libraries(GeneticsEditor Qt6::Core Qt6::Widgets)

# Platform-specific plugin imports and libraries
if(WIN32)
    qt_import_plugins(GeneticsEditor
        INCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QICOPlugin Qt6::QJpegPlugin Qt6::QGifPlugin
        EXCLUDE Qt6::QXcbIntegrationPlugin Qt6::QCocoaIntegrationPlugin
    )
    target_link_libraries(GeneticsEditor
        ole32 oleaut32 imm32 winmm ws2_32 uuid
        opengl32 gdi32 user32 shell32 advapi32
    )
elseif(APPLE)
    qt_import_plugins(GeneticsEditor
        INCLUDE Qt6::QCocoaIntegrationPlugin Qt6::QICOPlugin Qt6::QJpegPlugin Qt6::QGifPlugin
        EXCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QXcbIntegrationPlugin
    )
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(OPENGL_FRAMEWORK OpenGL)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    target_link_libraries(GeneticsEditor
        ${COCOA_FRAMEWORK} ${OPENGL_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK} ${FOUNDATION_FRAMEWORK}
    )
else()
    qt_import_plugins(GeneticsEditor
        INCLUDE Qt6::QXcbIntegrationPlugin Qt6::QICOPlugin Qt6::QJpegPlugin Qt6::QGifPlugin
        EXCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QCocoaIntegrationPlugin
    )
endif()

# Strip release binary
if(WIN32)
    set_target_properties(GeneticsEditor PROPERTIES
        LINK_FLAGS "-Wl,--gc-sections -Wl,--strip-all"
    )
endif()

# Debug definitions
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(GeneticsEditor PRIVATE ENABLE_DEBUG_OUTPUT)
endif()

# Output directory
set_target_properties(GeneticsEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy resources after build
add_custom_command(TARGET GeneticsEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/bin/resources
)

# Windows: no console window
if(WIN32)
    set_target_properties(GeneticsEditor PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(GeneticsEditor PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "DSSAT Genetics Editor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.dssat.genetics"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
endif()
