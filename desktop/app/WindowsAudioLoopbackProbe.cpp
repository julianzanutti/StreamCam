#include "../ipc/AudioOutputWorker.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>
#include <QLocalSocket>
#include <QtEndian>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    struct ProducerScheduling {
        DWORD index=0;HANDLE handle=AvSetMmThreadCharacteristicsW(L"Audio",&index);
        ~ProducerScheduling(){if(handle)AvRevertMmThreadCharacteristics(handle);}
    } producerScheduling;
    const unsigned seconds=argc>1?static_cast<unsigned>(std::stoi(argv[1])):2;
    if(seconds<2 || seconds>300)return 1;
    const bool phone=argc>2 && std::string(argv[2])=="--phone";
    QLocalSocket phoneSocket;
    int outputId=-1,inputId=-1;std::wstring outputEndpoint;
    for(const auto &device:streamcam::WindowsAudioOutput::devices())
        if(device.name.find(L"CABLE Input")!=std::wstring::npos) {
            if(outputId>=0) {std::cerr<<"Ambiguous CABLE Input\n";return 1;}
            outputId=static_cast<int>(device.id);outputEndpoint=device.endpointId;
        }
    for(UINT id=0;id<waveInGetNumDevs();++id) {
        WAVEINCAPSW caps{};
        if(waveInGetDevCapsW(id,&caps,sizeof(caps))==MMSYSERR_NOERROR &&
           std::wstring(caps.szPname).find(L"CABLE Output")!=std::wstring::npos) {
            if(inputId>=0) {std::cerr<<"Ambiguous CABLE Output\n";return 1;}
            inputId=static_cast<int>(id);
        }
    }
    if(outputId<0 || inputId<0) {std::cerr<<"Install VB-CABLE before running this hardware probe\n";return 1;}
    streamcam::WindowsAudioOutput invalidOutput;
    if(invalidOutput.open(0xffffffffu)) {std::cerr<<"Invalid device unexpectedly opened\n";return 1;}
    if(invalidOutput.open(static_cast<unsigned>(outputId),L"wrong-endpoint")){std::cerr<<"Mismatched endpoint opened\\n";return 1;}
    streamcam::AudioOutputWorker output;
    output.start({},QStringLiteral("StreamCamCableHardwareProbe"));
    auto source=std::make_shared<streamcam::LatestAudio>();
    output.setSource(source);output.setOutputDevice(outputId,QString::fromStdWString(outputEndpoint));
    for(int attempt=0;attempt<100 && output.directOutputStatus()!=0;++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(output.directOutputStatus()!=0){std::cerr<<"Worker could not open cable\n";return 1;}
    WAVEFORMATEX format{};
    format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=48000;
    format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
    HWAVEIN input=nullptr;
    auto result=waveInOpen(&input,static_cast<UINT>(inputId),&format,0,0,CALLBACK_NULL);
    if(result) {std::cerr<<"Input open error "<<result<<"\n";return 1;}
    std::vector<std::int16_t> recorded(48000*(seconds+2)*2);
    WAVEHDR header{};header.lpData=reinterpret_cast<char*>(recorded.data());
    header.dwBufferLength=static_cast<DWORD>(recorded.size()*sizeof(std::int16_t));
    result=waveInPrepareHeader(input,&header,sizeof(header));
    if(result) {waveInClose(input);return 1;}
    result=waveInAddBuffer(input,&header,sizeof(header));
    if(!result)result=waveInStart(input);
    if(result) {waveInReset(input);waveInUnprepareHeader(input,&header,sizeof(header));waveInClose(input);return 1;}
    // Avoid accumulating old packets while output and capture initialize.
    if(phone){phoneSocket.connectToServer(QStringLiteral("StreamCamAudio-v1"));
        if(!phoneSocket.waitForConnected(2000)){
            waveInStop(input);waveInReset(input);waveInUnprepareHeader(input,&header,sizeof(header));waveInClose(input);
            std::cerr<<"Live StreamCam audio pipe unavailable\n";return 1;}}
    constexpr double pi=3.14159265358979323846;
    std::vector<std::int16_t> block(1920);
    const auto start=std::chrono::steady_clock::now();
    bool sent=true;double maxLate=0;
    auto lastProduced=start;
    for(unsigned packet=0;packet<seconds*50;++packet) {
        const auto now=std::chrono::steady_clock::now();
        const auto late=std::chrono::duration<double,std::milli>(now-(start+std::chrono::milliseconds(packet*20))).count();
        maxLate=std::max(maxLate,late);
        lastProduced=now;
        for(unsigned frame=0;frame<960;++frame) {
            const auto sample=packet*960+frame;
            block[frame*2]=static_cast<std::int16_t>(6000*std::sin(2*pi*997*sample/48000));
            block[frame*2+1]=static_cast<std::int16_t>(6000*std::sin(2*pi*1733*sample/48000));
        }
        if(phone) {
            auto readExact=[&](qsizetype count) {
                QByteArray data;
                while(data.size()<count) {
                    const auto chunk=phoneSocket.read(count-data.size());
                    if(!chunk.isEmpty())data+=chunk;
                    else if(!phoneSocket.waitForReadyRead(2000))break;
                }
                return data;
            };
            const auto header=readExact(32);
            if(header.size()!=32){sent=false;break;}
            const auto *bytes=reinterpret_cast<const uchar*>(header.constData());
            const auto channels=qFromLittleEndian<quint32>(bytes+12);
            if(qFromLittleEndian<quint32>(bytes)!=0x31414353 || qFromLittleEndian<quint32>(bytes+8)!=48000 ||
               qFromLittleEndian<quint32>(bytes+16)!=960 || (channels!=1 && channels!=2)){sent=false;break;}
            const auto payload=readExact(960*channels*2);
            if(payload.size()!=960*channels*2){sent=false;break;}
            for(unsigned frame=0;frame<960;++frame)for(unsigned channel=0;channel<2;++channel)
                block[frame*2+channel]=qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(payload.constData())+(frame*channels+(channels==1?0:channel))*2);
        }
        if(packet==40 || packet==60) {
            streamcam::VoiceSettings settings;settings.muted=packet==40;
            output.setVoiceSettings(settings);
        }
        auto audio=std::make_shared<streamcam::AudioFrame>();
        audio->sampleRate=48000;audio->channels=2;audio->frames=960;
        audio->timestampUs=(packet+1)*20000;audio->pcm=block;
        source->publish(audio);
        if(!phone)std::this_thread::sleep_until(start+std::chrono::milliseconds((packet+1)*20));
    }
    const auto producingSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"Input audio duration="<<seconds<<" wall seconds="<<producingSeconds<<"\\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    output.clear();waveInStop(input);waveInReset(input);
    const auto frames=header.dwBytesRecorded/4;
    waveInUnprepareHeader(input,&header,sizeof(header));waveInClose(input);
    auto amplitude=[&](unsigned channel,double frequency) {
        double sine=0,cosine=0;
        for(unsigned frame=(seconds>2 && frames>48000?frames-48000:0);frame<frames;++frame) {
            const auto phase=2*pi*frequency*frame/48000;
            sine+=recorded[frame*2+channel]*std::sin(phase);
            cosine+=recorded[frame*2+channel]*std::cos(phase);
        }
        return frames?2*std::hypot(sine,cosine)/(seconds>2?48000:frames):0;
    };
    const auto left=amplitude(0,997),right=amplitude(1,1733);
    const auto wrongLeft=amplitude(0,1733),wrongRight=amplitude(1,997);
    std::cout<<"Producer max lateness ms="<<maxLate<<"\n";
    std::cout<<"Captured frames="<<frames<<" left997="<<left<<" right1733="<<right
             <<" crossLeft="<<wrongLeft<<" crossRight="<<wrongRight<<" dropped="<<output.directDroppedFrames()<<"\n";
    double mutePeak=0;
    if(frames>52800)for(unsigned frame=48000;frame<52800;++frame)
        mutePeak=std::max(mutePeak,std::abs(static_cast<double>(recorded[frame*2])));
    std::cout<<"Muted window peak="<<mutePeak<<"\n";
    if((!phone && mutePeak>50) || output.directOutputStatus()!=0 || output.directDroppedFrames()!=0) return 1;
    if(!sent || frames<48000)return 1;
    if(!phone && (left<1000 || right<1000 || wrongLeft>left*0.1 || wrongRight>right*0.1))return 1;
    if(phone){
        double energy=0;for(unsigned frame=0;frame<frames;++frame){const double sample=recorded[frame*2];energy+=sample*sample;}
        const auto rms=std::sqrt(energy/frames);std::cout<<"Live phone capture RMS="<<rms<<"\n";
        if(rms<2){std::cerr<<"Phone capture is silent; speak during the test\n";return 1;}
    }
    auto waitStatus=[&](int expected) {
        for(int attempt=0;attempt<100;++attempt) {
            if(output.directOutputStatus()==expected)return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    output.setOutputDevice(0x7fffffff);
    if(!waitStatus(MMSYSERR_BADDEVICEID)){std::cerr<<"Missing invalid-device error\n";return 1;}
    output.setOutputDevice(-1);
    if(!waitStatus(-1)){std::cerr<<"Disable failed\n";return 1;}
    output.setOutputDevice(outputId,QString::fromStdWString(outputEndpoint));
    if(!waitStatus(0)){std::cerr<<"Reopen failed\n";return 1;}
    output.setSource(nullptr);output.clear();output.stop();
    std::cout<<(phone?"PASS: live phone audio reached Windows capture endpoint\n":"PASS: StreamCam output reached Windows capture endpoint with separate stereo tones, mute and no OBS consumer\n");
    return 0;
}
