// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <util/platform.h>
#include <windows.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>
#include "AvSessionClock.h"

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void) { return "StreamCam local authenticated receiver frames"; }

#ifdef STREAMCAM_OBS_LOOPBACK_TEST
constexpr wchar_t StreamCamAudioPipe[] = L"\\\\.\\pipe\\StreamCamAudio-Loopback-QA";
constexpr wchar_t StreamCamVideoPipe[] = L"\\\\.\\pipe\\StreamCamFrames-Loopback-QA";
#else
constexpr wchar_t StreamCamAudioPipe[] = L"\\\\.\\pipe\\StreamCamAudio-v1";
constexpr wchar_t StreamCamVideoPipe[] = L"\\\\.\\pipe\\StreamCamFrames-v1";
#endif

namespace {
struct CameraSource {
    obs_source_t *source;
    std::atomic<bool> stopping{false};
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread worker;
    std::thread audioWorker;
    streamcam::AvSessionClock timestamps;
    explicit CameraSource(obs_source_t *value) : source(value), worker([this] { run(); }), audioWorker([this] { runAudio(); }) {}
    ~CameraSource() {
        stopping = true; SetEvent(cancel);
        if (worker.joinable()) worker.join();
        if (audioWorker.joinable()) audioWorker.join();
        CloseHandle(cancel);
    }
    bool read(HANDLE pipe, void *buffer, DWORD bytes) {
        auto *position = static_cast<std::uint8_t *>(buffer);
        while (bytes && !stopping) {
            OVERLAPPED operation{};
            operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            DWORD received = 0;
            const BOOL ready = ReadFile(pipe, position, bytes, &received, &operation);
            if (!ready && GetLastError() == ERROR_IO_PENDING) {
                HANDLE events[] = {cancel, operation.hEvent};
                if (WaitForMultipleObjects(2, events, FALSE, 2000) != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(pipe, &operation);
                    GetOverlappedResult(pipe, &operation, &received, TRUE);
                    CloseHandle(operation.hEvent); return false;
                }
                if (!GetOverlappedResult(pipe, &operation, &received, FALSE)) received = 0;
            } else if (!ready) received = 0;
            CloseHandle(operation.hEvent);
            if (!received) return false;
            position += received; bytes -= received;
        }
        return bytes == 0;
    }
    static std::uint32_t u32(const std::uint8_t *p) { return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24); }
    static std::uint64_t u64(const std::uint8_t *p) { return std::uint64_t(u32(p))|(std::uint64_t(u32(p+4))<<32); }
    void runAudio() {
        bool reportedPipeWait=false;
        while(!stopping) {
            HANDLE pipe=CreateFileW(StreamCamAudioPipe,GENERIC_READ,0,nullptr,OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
            if(pipe==INVALID_HANDLE_VALUE) {
                if(!reportedPipeWait) { blog(LOG_INFO,"[StreamCam] Waiting for audio pipe (%lu)",GetLastError()); reportedPipeWait=true; }
                WaitForSingleObject(cancel,500);continue;
            }
            blog(LOG_INFO,"[StreamCam] Connected to audio pipe");
            reportedPipeWait=false;
            timestamps.setConnected(streamcam::AvSessionClock::Stream::Audio,true);
            bool reportedFirstPacket=false;
            while(!stopping) {
                std::uint8_t header[32]{}; if(!read(pipe,header,sizeof header))break;
                const auto rate=u32(header+8),channels=u32(header+12),frames=u32(header+16);
                const auto timestamp=u64(header+20);
                if(u32(header)!=0x31414353 || u32(header+4)!=1 || rate!=48000 || (channels!=1&&channels!=2) ||
                    frames==0 || frames>960 || u32(header+28)!=0)break;
                std::vector<std::uint8_t> pcm(std::size_t(frames)*channels*2); if(!read(pipe,pcm.data(),static_cast<DWORD>(pcm.size())))break;
                std::uint64_t outputTimestamp=0;
                if(!timestamps.map(streamcam::AvTimestampMapper::Stream::Audio,timestamp,os_gettime_ns(),outputTimestamp))continue;
                if(!reportedFirstPacket) {
                    blog(LOG_INFO,"[StreamCam] First audio packet received: %u Hz, %u channel(s), %u frames",rate,channels,frames);
                    reportedFirstPacket=true;
                }
                obs_source_audio audio{}; audio.data[0]=pcm.data(); audio.frames=frames;
                audio.speakers=channels==1?SPEAKERS_MONO:SPEAKERS_STEREO; audio.format=AUDIO_FORMAT_16BIT;
                audio.samples_per_sec=rate; audio.timestamp=outputTimestamp; obs_source_output_audio(source,&audio);
            }
            CloseHandle(pipe);
            timestamps.setConnected(streamcam::AvSessionClock::Stream::Audio,false);
        }
    }
    void run() {
        bool reportedPipeWait=false;
        while (!stopping) {
            HANDLE pipe = CreateFileW(StreamCamVideoPipe, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                if(!reportedPipeWait) { blog(LOG_INFO,"[StreamCam] Waiting for video pipe (%lu)",GetLastError()); reportedPipeWait=true; }
                WaitForSingleObject(cancel,500);continue;
            }
            blog(LOG_INFO,"[StreamCam] Connected to video pipe");
            reportedPipeWait=false;
            timestamps.setConnected(streamcam::AvSessionClock::Stream::Video,true);
            bool reportedFirstFrame=false;
            while (!stopping) {
                std::uint32_t header[10]{};
                if (!read(pipe, header, sizeof header)) break;
                const auto width = header[2], height = header[3], stride = header[4], size = header[5];
                if (header[0] != 0x314d4353 || header[1] != 1 || !width || width > 1920 || !height || height > 1080 ||
                    stride < width * 4 || stride > 7680 || size != stride * height || size > 8294400 || header[8] || header[9]) break;
                std::vector<std::uint8_t> pixels(size);
                if (!read(pipe, pixels.data(), size)) break;
                obs_source_frame frame{};
                frame.data[0] = pixels.data(); frame.linesize[0] = stride;
                frame.width = width; frame.height = height; frame.format = VIDEO_FORMAT_BGRA;
                std::uint64_t sender=0; memcpy(&sender,&header[6],sizeof sender);
                if(!timestamps.map(streamcam::AvTimestampMapper::Stream::Video,sender,os_gettime_ns(),frame.timestamp))continue;
                if(!reportedFirstFrame) {
                    blog(LOG_INFO,"[StreamCam] First video frame received: %u x %u",width,height);
                    reportedFirstFrame=true;
                }
                frame.full_range = true;
                obs_source_output_video(source, &frame);
            }
            CloseHandle(pipe);
            timestamps.setConnected(streamcam::AvSessionClock::Stream::Video,false);
            obs_source_output_video(source, nullptr);
        }
    }
};
const char *sourceName(void *) { return "StreamCam"; }
void *sourceCreate(obs_data_t *, obs_source_t *source) {
    try { return new CameraSource(source); } catch (...) { return nullptr; }
}
void sourceDestroy(void *data) { delete static_cast<CameraSource *>(data); }
}

bool obs_module_load(void) {
    obs_source_info info{};
    info.id = "streamcam_source_v1"; info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
    info.get_name = sourceName; info.create = sourceCreate; info.destroy = sourceDestroy;
    obs_register_source(&info);
    return true;
}
