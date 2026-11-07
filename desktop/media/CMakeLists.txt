set(STREAMCAM_WEBRTC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.tooling/webrtc-msvc" CACHE PATH "Pinned native WebRTC SDK")
if(NOT EXISTS "${STREAMCAM_WEBRTC_ROOT}/release/webrtc.lib")
    message(FATAL_ERROR "Native WebRTC SDK is missing at ${STREAMCAM_WEBRTC_ROOT}")
endif()
add_library(streamcam_webrtc_sdk INTERFACE)
target_include_directories(streamcam_webrtc_sdk INTERFACE
    "${STREAMCAM_WEBRTC_ROOT}/include"
    "${STREAMCAM_WEBRTC_ROOT}/include/third_party/abseil-cpp"
    "${STREAMCAM_WEBRTC_ROOT}/include/third_party/libyuv/include")
target_compile_definitions(streamcam_webrtc_sdk INTERFACE WEBRTC_WIN NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(streamcam_webrtc_sdk INTERFACE "${STREAMCAM_WEBRTC_ROOT}/release/webrtc.lib"
    winmm ws2_32 secur32 crypt32 iphlpapi dmoguids wmcodecdspuuid msdmo strmiids
    amstrmid ole32 oleaut32 shell32 shlwapi advapi32 user32 gdi32 d3d11 dxgi mf mfplat mfuuid bcrypt)
add_library(streamcam_media STATIC WebRtcReceiver.cpp WebRtcReceiver.h)
target_link_libraries(streamcam_media PUBLIC Qt6::Core PRIVATE streamcam_webrtc_sdk)
