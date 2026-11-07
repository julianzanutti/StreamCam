#include <obs-module.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!obs_startup("en-US", nullptr, nullptr)) return 3;
    const auto *configured=std::getenv("OBS_STUDIO_DIR");
    const std::string root=configured ? configured : "C:/Program Files/obs-studio";
    const auto data=root+"/data/libobs/", graphics=root+"/bin/64bit/libobs-d3d11.dll";
    obs_add_data_path(data.c_str());
    obs_video_info video{};
    video.graphics_module=graphics.c_str();
    video.fps_num=30; video.fps_den=1; video.base_width=640; video.base_height=360;
    video.output_width=640; video.output_height=360; video.output_format=VIDEO_FORMAT_BGRA;
    video.colorspace=VIDEO_CS_709; video.range=VIDEO_RANGE_FULL;
    if(obs_reset_video(&video)!=OBS_VIDEO_SUCCESS) { obs_shutdown(); return 4; }
    obs_module_t *module=nullptr;
    if(obs_open_module(&module,argv[1],".")!=MODULE_SUCCESS || !obs_init_module(module)) { obs_shutdown(); return 5; }
    auto *source=obs_source_create("streamcam_source_v1","StreamCam synthetic media probe",nullptr,nullptr);
    if(!source) { obs_shutdown(); return 6; }
    obs_source_inc_active(source);
    bool found=false;
    for(unsigned attempt=0;attempt<2000;++attempt) {
        if(auto *frame=obs_source_get_frame(source)) {
            found=frame->width==1280 && frame->height==720 && frame->format==VIDEO_FORMAT_BGRA && frame->data[0];
            if(found) found=frame->data[0][0]>=50 && frame->data[0][0]<=100 && frame->data[0][1]>=50 &&
                frame->data[0][1]<=100 && frame->data[0][2]>=50 && frame->data[0][2]<=100 && frame->data[0][3]==255;
            if(found) std::printf("OBS received synthetic H264 path frame: %ux%u BGRA\n",frame->width,frame->height);
            obs_source_release_frame(source,frame);
            if(found)break;
        }
        Sleep(10);
    }
    obs_source_dec_active(source); obs_source_release(source); obs_shutdown();
    return found ? 0 : 7;
}
