#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <cstdint>


#include <string>
#include <vector>

namespace streamcam {
class WindowsAudioOutput final {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Apartment {
        HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
        bool valid() const{return SUCCEEDED(result)||result==RPC_E_CHANGED_MODE;}
    };
public:
    struct Device {unsigned id;std::wstring name;std::wstring endpointId;};
    static std::vector<Device> devices() {
        Apartment apartment;
        std::vector<Device> result;
        if(!apartment.valid())return result;
        Ptr<IMMDeviceEnumerator> enumerator;
        if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator))))return result;
        Ptr<IMMDeviceCollection> collection;
        if(FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&collection)))return result;
        UINT count=0;collection->GetCount(&count);
        for(UINT index=0;index<count;++index) {
            Ptr<IMMDevice> device;Ptr<IPropertyStore> properties;
            if(FAILED(collection->Item(index,&device))||FAILED(device->OpenPropertyStore(STGM_READ,&properties)))continue;
            PROPVARIANT name;PropVariantInit(&name);
            LPWSTR endpoint=nullptr;
            if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&name)) && name.vt==VT_LPWSTR && SUCCEEDED(device->GetId(&endpoint)))
                result.push_back({index,name.pwszVal,endpoint});
            CoTaskMemFree(endpoint);
            PropVariantClear(&name);
        }
        return result;
    }
    WindowsAudioOutput()=default;
    ~WindowsAudioOutput(){close();}
    WindowsAudioOutput(const WindowsAudioOutput&)=delete;
    WindowsAudioOutput& operator=(const WindowsAudioOutput&)=delete;
    bool open(unsigned id,const std::wstring &expectedEndpoint={}) {
        close();error_=0;
        const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return fail(initialized);
        ownsApartment_=SUCCEEDED(initialized);
        Ptr<IMMDeviceEnumerator> enumerator;Ptr<IMMDeviceCollection> collection;Ptr<IMMDevice> device;
        auto hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator));
        if(FAILED(hr))return fail(hr);
        hr=enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&collection);
        if(FAILED(hr))return fail(hr);
        UINT count=0;hr=collection->GetCount(&count);
        if(FAILED(hr))return fail(hr);
        if(id>=count){close();error_=MMSYSERR_BADDEVICEID;return false;}
        hr=collection->Item(id,&device);if(FAILED(hr))return fail(hr);
        if(!expectedEndpoint.empty()) {
            LPWSTR endpoint=nullptr;hr=device->GetId(&endpoint);
            if(FAILED(hr))return fail(hr);
            const bool matches=expectedEndpoint==endpoint;CoTaskMemFree(endpoint);
            if(!matches){close();error_=MMSYSERR_BADDEVICEID;return false;}
        }
        hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client_.GetAddressOf()));
        if(FAILED(hr))return fail(hr);
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;
        format.nSamplesPerSec=48000;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
        hr=client_->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                              1200000,0,&format,nullptr);
        if(FAILED(hr))return fail(hr);
        hr=client_->GetBufferSize(&capacity_);if(FAILED(hr))return fail(hr);
        hr=client_->GetService(IID_PPV_ARGS(&render_));if(FAILED(hr))return fail(hr);
        return true;
    }
    void clear(){if(client_){client_->Stop();client_->Reset();started_=false;}}
    void close(){clear();render_.Reset();client_.Reset();if(ownsApartment_)CoUninitialize();ownsApartment_=false;capacity_=0;}
    bool write(const std::int16_t *pcm,unsigned frames,unsigned channels) {
        if(!client_||!pcm||frames==0||frames>960||(channels!=1&&channels!=2))return false;
        UINT32 padding=0;auto hr=client_->GetCurrentPadding(&padding);
        if(FAILED(hr))return fail(hr);
        if(padding>capacity_ || frames>capacity_-padding || padding+frames>5760){++dropped_;return false;}
        BYTE *bytes=nullptr;hr=render_->GetBuffer(frames,&bytes);if(FAILED(hr))return fail(hr);
        auto *samples=reinterpret_cast<std::int16_t*>(bytes);
        for(unsigned frame=0;frame<frames;++frame){samples[frame*2]=pcm[frame*channels];samples[frame*2+1]=pcm[frame*channels+(channels==2?1:0)];}
        hr=render_->ReleaseBuffer(frames,0);if(FAILED(hr))return fail(hr);
        if(!started_){hr=client_->Start();if(FAILED(hr))return fail(hr);started_=true;}
        error_=0;return true;
    }
    unsigned error()const{return error_;}
    std::uint64_t dropped()const{return dropped_;}
private:
    bool fail(HRESULT hr){error_=static_cast<unsigned>(hr)&0x7fffffffU;if(!error_)error_=1;close();return false;}
    Ptr<IAudioClient> client_;Ptr<IAudioRenderClient> render_;
    UINT32 capacity_=0;bool started_=false,ownsApartment_=false;
    unsigned error_=0;std::uint64_t dropped_=0;
};
}
