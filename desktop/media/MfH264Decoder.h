#pragma once
#include <api/video_codecs/video_decoder.h>
#include <api/video/i420_buffer.h>
#include <api/video/encoded_image.h>
#include <api/video/video_frame.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <d3d10.h>
#include <codecapi.h>
#include <libyuv.h>
#include <cstdio>

namespace streamcam {
// System H.264 MFT. RAM output first; GPU path must be explicitly configured/measured.
class MfH264Decoder final : public webrtc::VideoDecoder {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<IMFTransform> decoder_;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> context_;
    Ptr<IMFDXGIDeviceManager> manager_;
    Ptr<ID3D11Texture2D> staging_;
    bool gpuOutputSeen_=false;
    webrtc::DecodedImageCallback *callback_ = nullptr;
    bool com_ = false, mf_ = false;
    UINT32 width_ = 1280, height_ = 720;
    UINT32 codedHeight_ = 720;
    LONG stride_ = 1280;
    bool selectOutput() {
        for (DWORD index=0;;++index) {
            Ptr<IMFMediaType> type;
            if (FAILED(decoder_->GetOutputAvailableType(0,index,&type))) return false;
            GUID subtype{}; type->GetGUID(MF_MT_SUBTYPE,&subtype);
            if (subtype != MFVideoFormat_NV12) continue;
            if (FAILED(decoder_->SetOutputType(0,type.Get(),0))) continue;
            if (FAILED(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&width_,&height_))) return false;
            codedHeight_=height_;
            MFVideoArea aperture{}; UINT32 apertureBytes=0;
            if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,reinterpret_cast<UINT8 *>(&aperture),sizeof aperture,&apertureBytes)) &&
                apertureBytes==sizeof aperture && aperture.Area.cx>0 && aperture.Area.cy>0 &&
                static_cast<UINT32>(aperture.Area.cx)<=width_ && static_cast<UINT32>(aperture.Area.cy)<=height_) {
                width_=aperture.Area.cx; height_=aperture.Area.cy;
            }
            UINT32 stride = width_; type->GetUINT32(MF_MT_DEFAULT_STRIDE,&stride); stride_=static_cast<LONG>(stride);
            return width_ > 0 && height_ > 0 && width_ <= 1920 && height_ <= 1080 && stride_ >= static_cast<LONG>(width_);
        }
    }
public:
    ~MfH264Decoder() override { Release(); }
    bool Configure(const Settings &) override {
        Release();
        const auto result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        com_=SUCCEEDED(result);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) return false;
        if (FAILED(MFStartup(MF_VERSION,MFSTARTUP_LITE))) return false;
        mf_=true;
        if (FAILED(CoCreateInstance(CLSID_CMSH264DecoderMFT,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&decoder_)))) return false;
        Ptr<IMFAttributes> attributes;
        if (SUCCEEDED(decoder_->GetAttributes(&attributes))) attributes->SetUINT32(MF_LOW_LATENCY,TRUE);
        Ptr<ICodecAPI> codec;
        if (SUCCEEDED(decoder_.As(&codec))) {
            // H.264 decoder specifically requires VT_UI4 (Microsoft codecapi documentation).
            VARIANT latency; VariantInit(&latency); latency.vt=VT_UI4; latency.ulVal=1;
            codec->SetValue(&CODECAPI_AVLowLatencyMode,&latency);
            VariantClear(&latency);
        }
        UINT32 aware=0;
        if (SUCCEEDED(decoder_->GetAttributes(&attributes)) && SUCCEEDED(attributes->GetUINT32(MF_SA_D3D11_AWARE,&aware)) && aware) {
            D3D_FEATURE_LEVEL level{}; UINT token=0;
            if (SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
                D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device_,&level,&context_)) &&
                SUCCEEDED(MFCreateDXGIDeviceManager(&token,&manager_)) && SUCCEEDED(manager_->ResetDevice(device_.Get(),token))) {
                Ptr<ID3D10Multithread> multithread;
                if (SUCCEEDED(device_.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
                if (FAILED(decoder_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,reinterpret_cast<ULONG_PTR>(manager_.Get())))) {
                    manager_.Reset(); context_.Reset(); device_.Reset();
                } else {
                    Ptr<IDXGIDevice> dxgi; Ptr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC description{};
                    if (SUCCEEDED(device_.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&description))) {
                        char name[256]{}; WideCharToMultiByte(CP_UTF8,0,description.Description,-1,name,sizeof name,nullptr,nullptr);
                        std::fprintf(stderr,"Media Foundation D3D11 adapter: %s\n",name); std::fflush(stderr);
                    }
                }
            }
        }
        Ptr<IMFMediaType> input;
        if (FAILED(MFCreateMediaType(&input))) return false;
        input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video); input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);
        MFSetAttributeSize(input.Get(),MF_MT_FRAME_SIZE,1280,720);
        MFSetAttributeRatio(input.Get(),MF_MT_FRAME_RATE,30,1);
        input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        if (FAILED(decoder_->SetInputType(0,input.Get(),0)) || !selectOutput()) return false;
        decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0);
        decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0);
        return true;
    }
    int32_t RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback *callback) override { callback_=callback; return 0; }
    int32_t Decode(const webrtc::EncodedImage &image, bool, int64_t) override {
        if (!decoder_ || !callback_ || !image.size() || image.size()>16*1024*1024) return -1;
        Ptr<IMFSample> sample; Ptr<IMFMediaBuffer> buffer;
        if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(image.size()),&buffer))) return -1;
        BYTE *data=nullptr; if (FAILED(buffer->Lock(&data,nullptr,nullptr))) return -1;
        memcpy(data,image.data(),image.size()); buffer->Unlock(); buffer->SetCurrentLength(static_cast<DWORD>(image.size()));
        sample->AddBuffer(buffer.Get()); sample->SetSampleTime(static_cast<LONGLONG>(image.RtpTimestamp())*10000000/90000);
        sample->SetSampleDuration(333333);
        if (FAILED(decoder_->ProcessInput(0,sample.Get(),0))) return -1;
        for (unsigned attempt=0;attempt<8;++attempt) {
            MFT_OUTPUT_STREAM_INFO info{}; if(FAILED(decoder_->GetOutputStreamInfo(0,&info))) return -1;
            Ptr<IMFSample> output; Ptr<IMFMediaBuffer> pixels;
            if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                if(FAILED(MFCreateSample(&output)) || FAILED(MFCreateMemoryBuffer(info.cbSize,&pixels))) return -1;
                output->AddBuffer(pixels.Get());
            }
            MFT_OUTPUT_DATA_BUFFER result{}; result.pSample=output.Get(); DWORD status=0;
            auto hr=decoder_->ProcessOutput(0,1,&result,&status);
            if(result.pEvents) result.pEvents->Release();
            if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT) return 0;
            if(hr==MF_E_TRANSFORM_STREAM_CHANGE) { if(!selectOutput())return -1; continue; }
            if(FAILED(hr)) return -1;
            if(!output) output.Attach(result.pSample);
            Ptr<IMFMediaBuffer> surfaceBuffer;
            Ptr<IMFDXGIBuffer> dxgiBuffer;
            if (SUCCEEDED(output->GetBufferByIndex(0,&surfaceBuffer)) && SUCCEEDED(surfaceBuffer.As(&dxgiBuffer))) {
                Ptr<ID3D11Texture2D> texture; UINT subresource=0;
                if (FAILED(dxgiBuffer->GetResource(IID_PPV_ARGS(&texture))) || FAILED(dxgiBuffer->GetSubresourceIndex(&subresource))) return -1;
                D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
                if (!device_ || desc.Format!=DXGI_FORMAT_NV12 || desc.Width>4096 || desc.Height>2160) return -1;
                D3D11_TEXTURE2D_DESC existing{}; if(staging_)staging_->GetDesc(&existing);
                if (!staging_ || existing.Width!=desc.Width || existing.Height!=desc.Height) {
                    staging_.Reset(); desc.ArraySize=1; desc.MipLevels=1; desc.BindFlags=0; desc.MiscFlags=0;
                    desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                    if(FAILED(device_->CreateTexture2D(&desc,nullptr,&staging_)))return -1;
                }
                context_->CopySubresourceRegion(staging_.Get(),0,0,0,0,texture.Get(),subresource,nullptr);
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if(FAILED(context_->Map(staging_.Get(),0,D3D11_MAP_READ,0,&mapped)))return -1;
                auto i420=webrtc::I420Buffer::Create(width_,height_);
                auto *base=static_cast<const uint8_t *>(mapped.pData);
                libyuv::NV12ToI420(base,mapped.RowPitch,base+mapped.RowPitch*desc.Height,mapped.RowPitch,
                    i420->MutableDataY(),i420->StrideY(),i420->MutableDataU(),i420->StrideU(),i420->MutableDataV(),i420->StrideV(),width_,height_);
                context_->Unmap(staging_.Get(),0);
                if(!gpuOutputSeen_) { std::fprintf(stderr,"Media Foundation delivered a D3D11 NV12 surface\n"); std::fflush(stderr); gpuOutputSeen_=true; }
                auto frame=webrtc::VideoFrame::Builder().set_video_frame_buffer(i420).set_rtp_timestamp(image.RtpTimestamp()).build();
                callback_->Decoded(frame); continue;
            }
            if(FAILED(output->ConvertToContiguousBuffer(&pixels))) return -1;
            DWORD length=0; if(FAILED(pixels->Lock(&data,nullptr,&length)))return -1;
            const auto needed=static_cast<UINT64>(stride_)*(codedHeight_+(codedHeight_+1)/2);
            if(length<needed) { pixels->Unlock(); return -1; }
            auto i420=webrtc::I420Buffer::Create(width_,height_);
            libyuv::NV12ToI420(data,stride_,data+stride_*codedHeight_,stride_,i420->MutableDataY(),i420->StrideY(),
                i420->MutableDataU(),i420->StrideU(),i420->MutableDataV(),i420->StrideV(),width_,height_);
            pixels->Unlock();
            auto frame=webrtc::VideoFrame::Builder().set_video_frame_buffer(i420).set_rtp_timestamp(image.RtpTimestamp()).build();
            callback_->Decoded(frame);
        }
        return 0;
    }
    int32_t Release() override {
        if(decoder_) decoder_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH,0);
        decoder_.Reset();
        staging_.Reset(); manager_.Reset(); context_.Reset(); device_.Reset(); gpuOutputSeen_=false;
        if(mf_) { MFShutdown(); mf_=false; }
        if(com_) { CoUninitialize(); com_=false; }
        return 0;
    }
    DecoderInfo GetDecoderInfo() const override { DecoderInfo info; info.implementation_name=gpuOutputSeen_ ? "Media Foundation H264 (D3D11 surface, RAM output)" : "Media Foundation H264 (RAM)"; info.is_hardware_accelerated=gpuOutputSeen_; return info; }
};
}
