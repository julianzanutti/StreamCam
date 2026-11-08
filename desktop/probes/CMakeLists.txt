cmake_minimum_required(VERSION 3.25)
project(StreamCamProbes LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Network)
find_package(Qt6 6.6 REQUIRED COMPONENTS WebSockets)

add_executable(StreamCamTrustedDevicesProbe ../app/TrustedDevicesProbe.cpp)
target_link_libraries(StreamCamTrustedDevicesProbe PRIVATE Qt6::Core)
add_test(NAME TrustedDevices COMMAND StreamCamTrustedDevicesProbe)

add_executable(StreamCamUsbAudioPacketProbe ../app/UsbAudioPacketProbe.cpp)
add_test(NAME UsbAudioPacket COMMAND StreamCamUsbAudioPacketProbe)

add_executable(StreamCamUsbH264DecodeQueueProbe ../app/UsbH264DecodeQueueProbe.cpp)
add_test(NAME UsbH264DecodeQueue COMMAND StreamCamUsbH264DecodeQueueProbe)

add_executable(StreamCamVoiceProbe ../app/VoiceProcessorProbe.cpp)
add_test(NAME VoiceProcessor COMMAND StreamCamVoiceProbe)

add_executable(StreamCamAudioQueueProbe ../app/AudioQueueProbe.cpp)
add_test(NAME AudioQueue COMMAND StreamCamAudioQueueProbe)

add_executable(StreamCamAudioSignalProbe ../app/AudioSignalProbe.cpp)
add_test(NAME AudioSignal COMMAND StreamCamAudioSignalProbe)

add_executable(StreamCamFrameOutputWorkerProbe ../app/FrameOutputWorkerProbe.cpp)
target_link_libraries(StreamCamFrameOutputWorkerProbe PRIVATE Qt6::Core Qt6::Network)
add_test(NAME FrameOutputWorker COMMAND StreamCamFrameOutputWorkerProbe)

add_executable(StreamCamAudioOutputWorkerProbe ../app/AudioOutputWorkerProbe.cpp)
target_link_libraries(StreamCamAudioOutputWorkerProbe PRIVATE Qt6::Core Qt6::Network)
add_test(NAME AudioOutputWorker COMMAND StreamCamAudioOutputWorkerProbe)

add_executable(StreamCamAudioEffectsAckProbe ../app/AudioEffectsAckProbe.cpp)
add_test(NAME AudioEffectsAck COMMAND StreamCamAudioEffectsAckProbe)

add_executable(StreamCamControlNegotiationProbe ../app/ControlNegotiationProbe.cpp)
add_test(NAME ControlNegotiation COMMAND StreamCamControlNegotiationProbe)

add_executable(StreamCamObsSyntheticMediaProbe ObsSyntheticMediaProbe.cpp)
target_link_libraries(StreamCamObsSyntheticMediaProbe PRIVATE Qt6::Core Qt6::Network)

add_executable(StreamCamAuthProbe ../app/AuthProbe.cpp ../app/PairingServer.cpp ../app/PairingServer.h)
target_include_directories(StreamCamAuthProbe PRIVATE ../app)
target_link_libraries(StreamCamAuthProbe PRIVATE Qt6::Core Qt6::Network Qt6::WebSockets crypt32 bcrypt)
target_compile_definitions(StreamCamAuthProbe PRIVATE STREAMCAM_AUTH_PROBE)
set_target_properties(StreamCamAuthProbe PROPERTIES AUTOMOC ON)
add_custom_command(TARGET StreamCamAuthProbe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../app/create-identity.ps1" "$<TARGET_FILE_DIR:StreamCamAuthProbe>/create-identity.ps1")
add_test(NAME ControlWebSocket
    COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_LIST_DIR}/../../scripts/check-control-websocket.ps1"
        -ProbePath "$<TARGET_FILE:StreamCamAuthProbe>"
        -IdentityTemplate "${CMAKE_CURRENT_SOURCE_DIR}/../app/create-identity.ps1"
        -WorkRoot "${CMAKE_CURRENT_BINARY_DIR}/control-websocket-test")

# CTest launches probes from its own working directory. Give every probe and
# child process (including the PowerShell WebSocket harness) Qt's runtime DLLs.
get_property(streamcam_probe_tests DIRECTORY PROPERTY TESTS)
foreach(test_name IN LISTS streamcam_probe_tests)
    set_property(TEST "${test_name}" APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endforeach()

if(WIN32)
    target_link_libraries(StreamCamAudioOutputWorkerProbe PRIVATE winmm ole32 uuid avrt)
endif()
