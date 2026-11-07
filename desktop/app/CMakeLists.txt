cmake_minimum_required(VERSION 3.25)
project(StreamCamDesktop VERSION 0.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets Network WebSockets)
add_subdirectory(../media "${CMAKE_BINARY_DIR}/media")

qt_add_executable(StreamCamDesktop WIN32
    main.cpp
    PairingServer.h
    PairingServer.cpp
    third_party/qrcodegen.cpp
)

target_link_libraries(StreamCamDesktop PRIVATE Qt6::Widgets Qt6::Network Qt6::WebSockets streamcam_media)
target_compile_definitions(StreamCamDesktop PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    STREAMCAM_APP_VERSION="${PROJECT_VERSION}-preview")
add_custom_command(TARGET StreamCamDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/create-identity.ps1" "$<TARGET_FILE_DIR:StreamCamDesktop>/create-identity.ps1")

add_executable(StreamCamPairingProbe PairingProbe.cpp PairingServer.cpp PairingServer.h)
target_link_libraries(StreamCamPairingProbe PRIVATE Qt6::Core Qt6::Network Qt6::WebSockets streamcam_media)
target_link_options(StreamCamPairingProbe PRIVATE /MAP)
add_executable(StreamCamFramePipeProbe FramePipeProbe.cpp)
target_link_libraries(StreamCamFramePipeProbe PRIVATE Qt6::Core Qt6::Network)
target_link_options(StreamCamFramePipeProbe PRIVATE /MAP)
add_executable(StreamCamQrProbe QrProbe.cpp third_party/qrcodegen.cpp)
target_link_libraries(StreamCamQrProbe PRIVATE Qt6::Core Qt6::Gui)
add_executable(StreamCamMediaProbe MediaProbe.cpp)
target_link_libraries(StreamCamMediaProbe PRIVATE Qt6::Core Qt6::Network streamcam_media)
target_link_options(StreamCamMediaProbe PRIVATE /MAP)
add_executable(StreamCamDeviceProofProbe DeviceProofProbe.cpp)
target_link_libraries(StreamCamDeviceProofProbe PRIVATE Qt6::Core crypt32 bcrypt)
add_executable(StreamCamAuthProbe AuthProbe.cpp PairingServer.cpp PairingServer.h)
target_link_libraries(StreamCamAuthProbe PRIVATE Qt6::Core Qt6::Network Qt6::WebSockets crypt32 bcrypt)

add_executable(StreamCamUsbProbe UsbProbe.cpp)
target_link_libraries(StreamCamUsbProbe PRIVATE Qt6::Core streamcam_media)

add_executable(StreamCamIdentityProbe IdentityProbe.cpp)
target_link_libraries(StreamCamIdentityProbe PRIVATE Qt6::Core Qt6::Network)

add_executable(StreamCamUpdateProbe UpdateProbe.cpp)
target_link_libraries(StreamCamUpdateProbe PRIVATE Qt6::Core)
