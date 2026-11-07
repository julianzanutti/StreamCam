#include "../media/WebRtcReceiver.h"
#include <QCoreApplication>
#include <QFile>
#include <QtEndian>
#include <cstdio>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2)return 2;
    QFile input(QString::fromLocal8Bit(argv[1]));
    if(!input.open(QIODevice::ReadOnly))return 3;
    streamcam::WebRtcReceiver receiver(nullptr,QStringLiteral("127.0.0.1"));
    unsigned packets=0;
    while(!input.atEnd()) {
        const auto header=input.read(4);
        if(header.size()!=4)return 4;
        const auto length=qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(header.constData()));
        if(length<21 || length>2*1024*1024+20)return 5;
        const auto packet=input.read(length);
        if(packet.size()!=length)return 6;
        receiver.receiveUsb(packet);++packets;
    }
    auto frame=receiver.latestFrame();
    if(!frame || !frame->valid() || receiver.receivedFrameCount()<20)return 7;
    if(frame->bgra[0]<50 || frame->bgra[0]>100 || frame->bgra[3]!=255)return 8;
    std::printf("USB H264 decoder: %u packets, %llu frames, %dx%d BGRA passed\n",packets,
        static_cast<unsigned long long>(receiver.receivedFrameCount()),frame->width,frame->height);
    return 0;
}
