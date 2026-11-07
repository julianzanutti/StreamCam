// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <util/platform.h>
#include <windows.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void) { return "StreamCam local authenticated receiver frames"; }

namespace {
struct CameraSource {
    obs_source_t *source;
    std::atomic<bool> stopping{false};
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread worker;
    std::thread audioWorker;
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
        while(!stopping) {
            HANDLE pipe=CreateFileW(L"\\\\.\\pipe\\StreamCamAudio-v1",GENERIC_READ,0,nullptr,OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
            if(pipe==INVALID_HANDLE_VALUE) {WaitForSingleObject(cancel,500);continue;}
            std::uint64_t origin=0,base=0,previous=0;
            while(!stopping) {
                std::uint8_t header[32]{}; if(!read(pipe,header,sizeof header))break;
                const auto rate=u32(header+8),channels=u32(header+12),frames=u32(header+16);
                const auto timestamp=u64(header+20);
                if(u32(header)!=0x31414353 || u32(header+4)!=1 || rate!=48000 || (channels!=1&&channels!=2) ||
                    frames==0 || frames>960 || u32(header+28)!=0)break;
                std::vector<std::uint8_t> pcm(std::size_t(frames)*channels*2); if(!read(pipe,pcm.data(),static_cast<DWORD>(pcm.size())))break;
                if(!origin || timestamp<origin) {origin=timestamp;base=os_gettime_ns();}
                const auto relative=timestamp-origin; if(relative>3600000000ULL)break;
                const auto outputTimestamp=base+relative*1000; if(outputTimestamp<=previous)continue; previous=outputTimestamp;
                obs_source_audio audio{}; audio.data[0]=pcm.data(); audio.frames=frames;
                audio.speakers=channels==1?SPEAKERS_MONO:SPEAKERS_STEREO; audio.format=AUDIO_FORMAT_16BIT;
                audio.samples_per_sec=rate; audio.timestamp=outputTimestamp; obs_source_output_audio(source,&audio);
            }
            CloseHandle(pipe);
        }
    }
    void run() {
        while (!stopping) {
            HANDLE pipe = CreateFileW(L"\\\\.\\pipe\\StreamCamFrames-v1", GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) { WaitForSingleObject(cancel, 500); continue; }
            std::uint64_t origin=0, base=0, previous=0;
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
                if (!base || sender<origin) { origin=sender; base=os_gettime_ns(); }
                const auto relative=sender-origin;
                if (relative>3600000000ULL) break; // reject impossible one-hour jump
                frame.timestamp = base+relative*1000;
                if(frame.timestamp<=previous) continue;
                previous=frame.timestamp; frame.full_range = true;
                obs_source_output_video(source, &frame);
            }
            CloseHandle(pipe);
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
