#include <QApplication>
#include <QComboBox>
#include <QProcess>
#include <QFileDialog>
#include <QSettings>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QScrollArea>
#include <QElapsedTimer>
#include <QThread>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedLayout>
#include <optional>
#include <functional>
#include <mutex>
#include "PairingServer.h"
#include "PcIdentity.h"
#include "QrImage.h"
#include <QNetworkInterface>
#include <QMessageBox>
#include <QCoreApplication>
#include "../media/WebRtcReceiver.h"
#include <QPointer>
#include <cstdio>
#include "../ipc/FramePublisher.h"
#include "../ipc/AudioPublisher.h"

namespace {

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("StreamCam"));
        resize(1120, 720);
        setMinimumSize(860, 560);

        auto *root = new QWidget(this);
        root->setObjectName(QStringLiteral("centralRoot"));
        auto *columns = new QGridLayout(root);
        columns->setContentsMargins(28, 24, 28, 20);
        columns->setHorizontalSpacing(22);
        columns->setVerticalSpacing(18);
        columns->setColumnStretch(0, 0);
        columns->setColumnStretch(1, 1);

        auto *brand = new QLabel(QStringLiteral("StreamCam"), root);
        brand->setObjectName(QStringLiteral("brand"));
        auto *subtitle = new QLabel(QStringLiteral("Cámara Android para OBS"), root);
        subtitle->setObjectName(QStringLiteral("muted"));
        columns->addWidget(brand, 0, 0);
        columns->addWidget(subtitle, 1, 0);

        auto *connection = new QGroupBox(QStringLiteral("Conexión"), root);
        connection->setMinimumWidth(300);
        auto *connectionLayout = new QVBoxLayout(connection);
        connectionLayout->setSpacing(12);

        auto *state = stateLabel_ = new QLabel(QStringLiteral("Listo para conectar"), connection);
        state->setWordWrap(true);
        state->setObjectName(QStringLiteral("stateBadge"));
        auto *transportLabel = new QLabel(QStringLiteral("Cómo querés conectar"), connection);
        transportLabel->setObjectName(QStringLiteral("muted"));
        auto *transport = transport_ = new QComboBox(connection);
        transport->setAccessibleName(QStringLiteral("Método de conexión"));
        refreshTransports();

        auto *pairButton = pairButton_ = new QPushButton(QStringLiteral("Conectar por USB"), connection);
        pairButton->setObjectName(QStringLiteral("primaryAction"));
        pairButton->setEnabled(transport->count() > 0);

        auto *connectionHint = hintLabel_ = new QLabel(
            QStringLiteral("Conectá el teléfono con un cable de datos y activá la depuración USB. No hace falta escanear un QR."),
            connection);
        connectionHint->setObjectName(QStringLiteral("muted"));
        connectionHint->setWordWrap(true);

        connectionLayout->addWidget(state);
        connectionLayout->addSpacing(4);
        connectionLayout->addWidget(transportLabel);
        connectionLayout->addWidget(transport);
        auto *refresh = refreshButton_ = new QPushButton(QStringLiteral("Actualizar conexiones"), connection);
        refresh->setObjectName(QStringLiteral("textAction"));
        connectionLayout->addWidget(refresh);
        connect(refresh,&QPushButton::clicked,this,[this] { refreshTransports(); updateControls(); });
        connect(transport,&QComboBox::currentIndexChanged,this,[this] { updateControls(); });
        connectionLayout->addWidget(pairButton);
        connectionLayout->addWidget(connectionHint);
        auto *qrLabel = qrLabel_ = new QLabel(connection);
        qrLabel->setVisible(false);
        qrLabel->setAlignment(Qt::AlignCenter);
        connectionLayout->addWidget(qrLabel);
        auto *cancelPairing = cancelButton_ = new QPushButton(QStringLiteral("Cancelar conexión"), connection);
        cancelPairing->setEnabled(false);
        connectionLayout->addWidget(cancelPairing);
        auto *savedToggle=new QPushButton(QStringLiteral("Teléfonos vinculados ▸"),connection);
        savedToggle->setObjectName(QStringLiteral("textAction"));savedToggle->setCheckable(true);
        connectionLayout->addWidget(savedToggle);
        auto *savedPanel=new QWidget(connection);
        auto *savedLayout=new QVBoxLayout(savedPanel);savedLayout->setContentsMargins(0,0,0,0);
        connectionLayout->addWidget(savedPanel);savedPanel->hide();
        connect(savedToggle,&QPushButton::toggled,savedPanel,[savedPanel,savedToggle](bool open) {
            savedPanel->setVisible(open);savedToggle->setText(open ? QStringLiteral("Teléfonos vinculados ▾") : QStringLiteral("Teléfonos vinculados ▸"));
        });
        auto *savedDevices=new QComboBox(connection);
        savedDevices->setAccessibleName(QStringLiteral("Teléfono vinculado"));
        auto *revoke = revokeButton_ = new QPushButton(QStringLiteral("Revocar acceso"),connection);
        auto refreshDevices=[this,savedDevices,revoke] {
            savedDevices->clear();
            try { for(const auto &id:pairing_.trustedDeviceIds()) savedDevices->addItem(QStringLiteral("Teléfono guardado %1").arg(savedDevices->count()+1),id); }
            catch(...) { savedDevices->addItem(QStringLiteral("No se pudieron leer los teléfonos")); }
            if(savedDevices->count()==0)savedDevices->addItem(QStringLiteral("Todavía no hay teléfonos vinculados"));
            savedDevices->setEnabled(!savedDevices->currentData().toString().isEmpty());
            revoke->setEnabled(!savedDevices->currentData().toString().isEmpty());
        };
        refreshDevices();
        savedLayout->addWidget(savedDevices);
        savedLayout->addWidget(revoke);
        connect(revoke,&QPushButton::clicked,this,[this,state,savedDevices,refreshDevices] {
            const auto id=savedDevices->currentData().toString();
            if(id.isEmpty()) { state->setText(QStringLiteral("La sesión no tiene identidad guardada")); return; }
            try { pairing_.revokeDevice(id); }
            catch(...) { state->setText(QStringLiteral("No se pudo guardar la revocación")); return; }
            if(activeSocket_ && activeSocket_->property("deviceId").toString()==id) {
                activeSocket_->close(); activeSocket_.clear(); receiver_.reset(); publisher_.clear(); audioPublisher_.clear();
                pending_=false; updateControls();
            }
            refreshDevices();
            state->setText(QStringLiteral("Teléfono revocado y desconectado"));
        });
        connect(cancelPairing, &QPushButton::clicked, this, [this] {
            cancelConnection();
            stateLabel_->setText(QStringLiteral("Desconectado · la cámara ya no se recibe"));
        });
        connect(&pairing_, &PairingServer::statusChanged, this, [this](const QString &text) {
            pending_=false; qrLabel_->clear(); qrLabel_->hide();
            stateLabel_->setText(text); updateControls();
        });
        connect(&pairing_, &PairingServer::confirmationRequested, this, [this, qrLabel,state] {
            qrLabel->clear(); qrLabel->hide();
            state->setText(QStringLiteral("Confirmá la vinculación del teléfono"));
            auto *dialog=new QMessageBox(QMessageBox::Question,QStringLiteral("Vincular teléfono"),
                QStringLiteral("El teléfono verificó esta PC. ¿Querés autorizarlo para esta sesión?"),QMessageBox::Yes|QMessageBox::No,this);
            dialog->setButtonText(QMessageBox::Yes,QStringLiteral("Vincular"));
            dialog->setButtonText(QMessageBox::No,QStringLiteral("Cancelar"));
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            consentDialog_=dialog;
            connect(dialog,&QDialog::finished,this,[this](int result) {
                pairing_.confirm(result==QMessageBox::Yes);
                if(result!=QMessageBox::Yes) {pending_=false;stateLabel_->setText(QStringLiteral("Vinculación cancelada"));updateControls();}
            });
            if(isMinimized())showNormal();
            raise(); activateWindow(); dialog->open(); QApplication::alert(this,0);
        });
        connect(&pairing_, &PairingServer::paired, this, [this, state, transport,refreshDevices](QWebSocket *socket) {
            refreshDevices();
            state->setText(QStringLiteral("Teléfono autorizado · iniciá video en Android"));
            socket->setParent(this);
            if (activeSocket_) activeSocket_->close();
            activeSocket_ = socket; pending_=false; preparing_=false; updateControls();
            try { receiver_ = std::make_unique<streamcam::WebRtcReceiver>(nullptr, pairingAddress_); }
            catch (const std::exception &) { socket->close(); activeSocket_.clear(); updateControls(); state->setText(QStringLiteral("No se pudo preparar el receptor de video. Volvé a conectar.")); return; }
            socket->setMaxAllowedIncomingMessageSize(pairingAddress_==QStringLiteral("127.0.0.1") ? 2*1024*1024+20 : 131072);
            socket->setMaxAllowedIncomingFrameSize(pairingAddress_==QStringLiteral("127.0.0.1") ? 2*1024*1024+20 : 131072);
            connect(receiver_.get(), &streamcam::WebRtcReceiver::signalReady, socket, [socket](const QJsonObject &message) {
                socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
            });
            connect(receiver_.get(), &streamcam::WebRtcReceiver::statusChanged, state, &QLabel::setText);
            connect(socket,&QWebSocket::binaryMessageReceived,this,[this,socket](const QByteArray &packet) {
                if(activeSocket_!=socket || !receiver_ || pairingAddress_!=QStringLiteral("127.0.0.1")) {socket->close();return;}
                try {receiver_->receiveUsb(packet);} catch(...) {socket->close();}
            });
            connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
                if (activeSocket_ != socket || !receiver_) return;
                const auto message = QJsonDocument::fromJson(text.toUtf8());
                if (!message.isObject()) { socket->close(); return; }
                try {
                    const auto object=message.object();
                    const auto type=object.value(QStringLiteral("type")).toString();
                    if(type==QStringLiteral("video.stop") || type==QStringLiteral("media.stop")) {
                        if(object.size()!=1) {socket->close();return;}
                        receiver_->reset(pairingAddress_);publisher_.clear();audioPublisher_.clear();
                        stateLabel_->setText(QStringLiteral("Transmisión detenida · podés volver a iniciarla desde Android"));
                    } else {
                        if(pairingAddress_==QStringLiteral("127.0.0.1") || (type!=QStringLiteral("rtc.offer") && type!=QStringLiteral("rtc.ice"))) {socket->close();return;}
                        receiver_->receive(object);
                    }
                } catch (...) { socket->close(); }
            });
            connect(socket, &QWebSocket::disconnected, this, [this, socket, state] {
                if (activeSocket_ == socket) { receiver_.reset(); publisher_.clear(); audioPublisher_.clear(); activeSocket_.clear(); pending_=false; updateControls(); state->setText(QStringLiteral("Teléfono desconectado · volvé a conectar cuando quieras")); }
            });
        });
        connect(pairButton, &QPushButton::clicked, this, [this] { beginConnection(); });
        auto *virtualCamera=new QPushButton(QStringLiteral("Usar en OBS o videollamadas"),connection);
        connectionLayout->addWidget(virtualCamera);
        connect(virtualCamera,&QPushButton::clicked,this,[this] {
            QMessageBox box(QMessageBox::Information,QStringLiteral("Usar StreamCam como cámara virtual"),
                QStringLiteral("1. Abrí OBS Studio y agregá Fuentes → + → StreamCam. Si no aparece, instalá el plugin incluido en la carpeta obs-plugin y reiniciá OBS.\n\n2. Para usar el video en Meet, Discord o Zoom, pulsá Iniciar cámara virtual en OBS.\n\n3. En la videollamada elegí OBS Virtual Camera. La cámara virtual muestra la escena de OBS."),QMessageBox::Ok,this);
            auto *open=box.addButton(QStringLiteral("Abrir OBS"),QMessageBox::ActionRole);
            box.exec();
            if(box.clickedButton()==open) {
                QSettings preferences;
                auto path=preferences.value(QStringLiteral("obsExecutable")).toString();
                if(!QFileInfo::exists(path))path=QFileDialog::getOpenFileName(this,QStringLiteral("Elegí obs64.exe"),{},QStringLiteral("OBS Studio (obs64.exe)"));
                if(path.isEmpty())return;
                preferences.setValue(QStringLiteral("obsExecutable"),path);
                if(QFileInfo::exists(path) && !QProcess::startDetached(path,{},QFileInfo(path).absolutePath()))
                    QMessageBox::warning(this,QStringLiteral("No se pudo abrir OBS"),QStringLiteral("Abrí OBS desde su acceso directo y agregá la fuente StreamCam."));
                else QMessageBox::information(this,QStringLiteral("Abrir OBS"),QStringLiteral("Abrí OBS Studio desde tu acceso directo y agregá la fuente StreamCam."));
            }
        });
        connectionLayout->addStretch();

        auto *preview = new QFrame(root);
        preview->setObjectName(QStringLiteral("previewSurface"));
        auto *previewStack = new QStackedLayout(preview);
        previewStack->setContentsMargins(20,20,20,20);
        auto *emptyPreview = new QWidget(preview);
        auto *previewLayout = new QVBoxLayout(emptyPreview);
        previewLayout->setAlignment(Qt::AlignCenter);
        auto *previewMessage = new QLabel(QStringLiteral("Esperando una cámara"), preview);
        previewMessage->setObjectName(QStringLiteral("previewTitle"));
        previewMessage->setAlignment(Qt::AlignCenter);
        auto *previewDetail = new QLabel(
            QStringLiteral("El video aparecerá aquí después del emparejamiento seguro del teléfono."), preview);
        previewDetail->setObjectName(QStringLiteral("muted"));
        previewDetail->setAlignment(Qt::AlignCenter);
        previewDetail->setWordWrap(true);
        previewLayout->addWidget(previewMessage);
        previewLayout->addWidget(previewDetail);
        auto *videoImage = new QLabel(preview);
        videoImage->setAlignment(Qt::AlignCenter);
        videoImage->setMinimumSize(1,1);
        videoImage->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        previewStack->addWidget(emptyPreview);
        previewStack->addWidget(videoImage);
        previewStack->setCurrentWidget(emptyPreview);
        auto *frameTimer = new QTimer(this);
        if (!publisher_.start()) statusBar()->showMessage(QStringLiteral("No se pudo abrir la salida OBS: otra instancia puede estar activa."));
        if (!audioPublisher_.start()) statusBar()->showMessage(QStringLiteral("No se pudo abrir la salida OBS de audio."));
        connect(frameTimer, &QTimer::timeout, this, [this, videoImage, previewStack, emptyPreview] {
            const auto frame = receiver_ ? receiver_->latestFrame() : nullptr;
            if (!frame) {
                videoImage->clear(); previewStack->setCurrentWidget(emptyPreview);
                for(auto *label:metricLabels_)label->setText(QStringLiteral("—"));
                frameMeasure_.invalidate(); lastReceivedCount_=0; lastPreviewTimestamp_=-1; return;
            }
            metricLabels_[0]->setText(QStringLiteral("%1 × %2").arg(frame->width).arg(frame->height));
            metricLabels_[2]->setText(pairingAddress_==QStringLiteral("127.0.0.1") ? QStringLiteral("USB · ADB") : QStringLiteral("Red local"));
            metricLabels_[3]->setText(QStringLiteral("Media Foundation"));
            if(!frameMeasure_.isValid()) { frameMeasure_.start(); lastReceivedCount_=receiver_->receivedFrameCount(); }
            else if(frameMeasure_.elapsed()>=1000) {
                const auto count=receiver_->receivedFrameCount();
                metricLabels_[1]->setText(QString::number((count-lastReceivedCount_)*1000.0/frameMeasure_.elapsed(),'f',1));
                lastReceivedCount_=count; frameMeasure_.restart();
            }
            previewStack->setCurrentWidget(videoImage);
            if(lastPreviewTimestamp_==frame->timestampUs && lastPreviewSize_==videoImage->size())return;
            lastPreviewTimestamp_=frame->timestampUs; lastPreviewSize_=videoImage->size();
            publisher_.publish(frame);
            const QImage image(frame->bgra.data(), frame->width, frame->height, frame->stride, QImage::Format_ARGB32);
            videoImage->setPixmap(QPixmap::fromImage(image).scaled(videoImage->size().expandedTo(QSize(1,1)), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            previewStack->setCurrentWidget(videoImage);
        });
        frameTimer->start(33);
        auto *audioTimer=new QTimer(this);
        connect(audioTimer,&QTimer::timeout,this,[this] { if(receiver_)audioPublisher_.publish(receiver_->latestAudio()); });
        audioTimer->start(10);

        auto *metrics = new QFrame(root);
        auto *metricsLayout = new QGridLayout(metrics);
        metricsLayout->setContentsMargins(0, 0, 0, 0);
        metricLabels_[0]=addMetric(metricsLayout, 0, QStringLiteral("Resolución"), QStringLiteral("—"));
        metricLabels_[1]=addMetric(metricsLayout, 1, QStringLiteral("FPS"), QStringLiteral("—"));
        metricLabels_[2]=addMetric(metricsLayout, 2, QStringLiteral("Conexión"), QStringLiteral("—"));
        metricLabels_[3]=addMetric(metricsLayout, 3, QStringLiteral("Decoder"), QStringLiteral("—"));

        auto *connectionScroll=new QScrollArea(root);
        connectionScroll->setWidgetResizable(true);
        connectionScroll->setWidget(connection);
        connectionScroll->setMinimumWidth(340);
        connectionScroll->setMaximumWidth(430);
        connectionScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        columns->addWidget(connectionScroll, 2, 0, 3, 1);
        columns->addWidget(preview, 2, 1, 2, 1);
        columns->addWidget(metrics, 4, 1);
        columns->setRowStretch(2, 1);
        updateControls();
        setCentralWidget(root);

        auto *status = new QLabel(QStringLiteral("H.264 · calidad configurable desde Android · audio opcional"), this);
        statusBar()->addWidget(status, 1);

        setStyleSheet(QStringLiteral(R"(
            QWidget { color: #1f1f1f; font: 15px "Segoe UI"; }
            QMainWindow, QWidget#centralRoot { background: #f8fafd; }
            QLabel { background: transparent; }
            QLabel#brand { font-size: 30px; font-weight: 600; color: #0b57d0; }
            QLabel#muted { color: #5f6368; }
            QGroupBox { background: #ffffff; border: 1px solid #e1e5eb; border-radius: 20px; margin-top: 12px; padding: 20px; font-weight: 600; }
            QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; }
            QLabel#stateBadge { background: #e8f0fe; border-radius: 16px; padding: 12px; color: #174ea6; font-weight: 600; }
            QComboBox { background: #ffffff; border: 1px solid #c7cbd1; border-radius: 14px; padding: 12px; }
            QComboBox::drop-down { border: none; width: 26px; }
            QPushButton { background: #e8f0fe; color: #0b57d0; border: none; border-radius: 22px; padding: 12px 16px; min-height: 20px; }
            QPushButton:hover { background: #d3e3fd; }
            QPushButton:focus, QComboBox:focus { border: 2px solid #0b57d0; }
            QPushButton#textAction { background: transparent; padding: 4px 8px; min-height: 16px; text-align: left; }
            QLabel#sectionLabel { color: #5f6368; font-weight: 600; margin-top: 12px; }
            QPushButton#primaryAction { background: #0b57d0; color: #ffffff; }
            QPushButton#primaryAction:hover { background: #0849b0; }
            QPushButton:disabled { color: #92979d; background: #e6e8eb; }
            QFrame#previewSurface { background: #eaf1fb; border: none; border-radius: 24px; }
            QLabel#previewTitle { font-size: 22px; font-weight: 600; }
            QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
            QMessageBox { background: #f8fafd; }
            QStatusBar { background: #f8fafd; color: #5f6368; }
        )"));
    }

    ~MainWindow() override {
        ++connectionGeneration_;
        if(adbProcess_) {adbProcess_->disconnect(this);adbProcess_->kill();}
        if(activeSocket_) {QObject::disconnect(activeSocket_,nullptr,this,nullptr);activeSocket_->abort();activeSocket_.clear();}
        receiver_.reset();pairing_.stop();
    }

private:
    QLabel *stateLabel_=nullptr, *hintLabel_=nullptr, *qrLabel_=nullptr;
    QComboBox *transport_=nullptr;
    QPushButton *pairButton_=nullptr, *cancelButton_=nullptr, *refreshButton_=nullptr, *revokeButton_=nullptr;
    QPointer<QProcess> adbProcess_;
    QPointer<QMessageBox> consentDialog_;
    bool preparing_=false, pending_=false;
    unsigned connectionGeneration_=0;
    std::int64_t lastPreviewTimestamp_=-1;
    QSize lastPreviewSize_;
    void refreshTransports() {
        const auto selected=transport_->currentData().toString();
        QSignalBlocker blocker(transport_);
        transport_->clear();
        transport_->addItem(QStringLiteral("USB · cable de datos"),QStringLiteral("127.0.0.1"));
        for(const auto &interface:QNetworkInterface::allInterfaces()) {
            if(!(interface.flags()&QNetworkInterface::IsUp) || interface.flags()&QNetworkInterface::IsLoopBack)continue;
            for(const auto &entry:interface.addressEntries()) {
                const auto ip=entry.ip().toIPv4Address();
                if((ip>>24)==10 || (ip>>20)==0xac1 || (ip>>16)==0xc0a8)
                    transport_->addItem(QStringLiteral("Wi-Fi / red local · ")+entry.ip().toString(),entry.ip().toString());
            }
        }
        const auto index=transport_->findData(selected);
        if(index>=0)transport_->setCurrentIndex(index);
    }
    void updateControls() {
        const bool usb=transport_->currentData().toString()==QStringLiteral("127.0.0.1");
        const bool busy=preparing_ || pending_ || activeSocket_;
        transport_->setEnabled(!busy); refreshButton_->setEnabled(!busy);
        pairButton_->setEnabled(!preparing_ && !activeSocket_);
        pairButton_->setText(preparing_ ? QStringLiteral("Conectando…") : usb ? QStringLiteral("Conectar por USB") : pending_ ? QStringLiteral("Generar otro QR") : QStringLiteral("Conectar por Wi-Fi"));
        cancelButton_->setEnabled(busy);
        cancelButton_->setText(activeSocket_ ? QStringLiteral("Desconectar teléfono") : QStringLiteral("Cancelar conexión"));
        hintLabel_->setText(usb ? QStringLiteral("Conectá el teléfono con un cable de datos y activá la depuración USB. No hace falta escanear un QR.")
            : QStringLiteral("Conectá ambos equipos a la misma red. Escaneá el QR desde StreamCam en Android y confirmá la vinculación."));
    }
    void cancelConnection() {
        ++connectionGeneration_; preparing_=false; pending_=false;
        if(consentDialog_)consentDialog_->reject();
        if(adbProcess_) { adbProcess_->disconnect(this); adbProcess_->kill(); adbProcess_->deleteLater(); adbProcess_.clear(); }
        if(activeSocket_)activeSocket_->close();
        activeSocket_.clear();receiver_.reset();publisher_.clear();audioPublisher_.clear();pairing_.stop();qrLabel_->clear();qrLabel_->hide();updateControls();
    }
    void connectionFailed(const QString &message) {
        cancelConnection();stateLabel_->setText(message);
    }
    void runAdb(const QString &executable,const QStringList &arguments,unsigned generation,std::function<void()> next) {
        auto *process=new QProcess(this);adbProcess_=process;
        auto settled=std::make_shared<bool>(false);
        auto complete=[this,process,generation,settled,next](bool success) {
            if(*settled)return;*settled=true;
            if(adbProcess_==process)adbProcess_.clear();
            const auto output=process->readAllStandardOutput();
            process->deleteLater();
            if(generation!=connectionGeneration_)return;
            if(!success || output.contains("Error:") || output.contains("Exception")) {
                connectionFailed(QStringLiteral("No se pudo conectar por USB. Verificá el cable, conectá un solo teléfono y aceptá la depuración USB en Android."));return;
            }
            next();
        };
        connect(process,&QProcess::finished,this,[complete](int code,QProcess::ExitStatus status) {complete(code==0 && status==QProcess::NormalExit);});
        connect(process,&QProcess::errorOccurred,this,[complete](QProcess::ProcessError) {complete(false);});
        QTimer::singleShot(10000,process,[process,complete] {if(process->state()!=QProcess::NotRunning) {process->kill();complete(false);}});
        process->start(executable,arguments);
    }
    void beginConnection() {
        if(preparing_ || activeSocket_)return;
        cancelConnection();preparing_=true;updateControls();
        stateLabel_->setText(QStringLiteral("Preparando conexión segura…"));
        pairingAddress_=transport_->currentData().toString();
        const auto generation=connectionGeneration_;
        struct IdentityResult { std::optional<PcIdentity> identity; QString error; };
        auto result=std::make_shared<IdentityResult>();
        const auto script=QCoreApplication::applicationDirPath()+QStringLiteral("/create-identity.ps1");
        auto *task=QThread::create([result,script] {
            try { static std::mutex identityMutex; std::lock_guard lock(identityMutex); result->identity=PcIdentity::load(script); }
            catch(const std::exception &) {result->error=QStringLiteral("No se pudo preparar la identidad de esta PC. Volvé a extraer el paquete completo y reintentá.");}
        });
        connect(task,&QThread::finished,task,&QObject::deleteLater);
        connect(task,&QThread::finished,this,[this,result,generation] {
            if(generation!=connectionGeneration_)return;
            if(!result->identity) {connectionFailed(result->error);return;}
            const auto identity=*result->identity;
            try {
                auto payload=pairing_.start(QHostAddress(pairingAddress_),identity.tls,identity.id,identity.pin);
                if(pairingAddress_!=QStringLiteral("127.0.0.1")) {
                    qrLabel_->setPixmap(QPixmap::fromImage(pairingQrImage(payload)).scaled(300,300,Qt::KeepAspectRatio,Qt::FastTransformation));
                    qrLabel_->show();payload.fill('\0');preparing_=false;pending_=true;updateControls();
                    stateLabel_->setText(QStringLiteral("Escaneá el QR desde Android · válido durante 2 minutos"));return;
                }
                payload.fill('\0');
                auto adb=QCoreApplication::applicationDirPath()+QStringLiteral("/runtime/adb/adb.exe");
                if(!QFileInfo::exists(adb))adb=QStandardPaths::findExecutable(QStringLiteral("adb"));
                if(adb.isEmpty()) {connectionFailed(QStringLiteral("Falta Android Platform Tools. Volvé a extraer el paquete completo de StreamCam."));return;}
                stateLabel_->setText(QStringLiteral("Buscando el teléfono por USB…"));
                runAdb(adb,{QStringLiteral("reverse"),QStringLiteral("tcp:47831"),QStringLiteral("tcp:47831")},generation,[this,adb,identity,generation] {
                    runAdb(adb,{QStringLiteral("shell"),QStringLiteral("am"),QStringLiteral("start"),QStringLiteral("-n"),
                        QStringLiteral("com.streamcam.diagnostics/.PairingActivity"),QStringLiteral("-a"),QStringLiteral("com.streamcam.USB_CONNECT"),
                        QStringLiteral("--es"),QStringLiteral("pc_id"),identity.id,QStringLiteral("--es"),QStringLiteral("pc_pin"),QString::fromLatin1(identity.pin.toHex())},generation,[this] {
                            preparing_=false;pending_=true;updateControls();stateLabel_->setText(QStringLiteral("Teléfono detectado · confirmá la conexión en Android"));
                        });
                });
            } catch(const std::exception &) {connectionFailed(QStringLiteral("No se pudo abrir la conexión. Cerrá otras instancias de StreamCam y reintentá."));}
        });
        task->start();
    }
    PairingServer pairing_;
    QPointer<QWebSocket> activeSocket_;
    std::unique_ptr<streamcam::WebRtcReceiver> receiver_;
    streamcam::FramePublisher publisher_;
    streamcam::AudioPublisher audioPublisher_;
    QString pairingAddress_;
    QLabel *metricLabels_[4]{};
    QElapsedTimer frameMeasure_;
    std::uint64_t lastReceivedCount_=0;
    static QLabel *addMetric(QGridLayout *layout, int column, const QString &name, const QString &value) {
        auto *card = new QGroupBox(name);
        auto *cardLayout = new QVBoxLayout(card);
        auto *label = new QLabel(value, card);
        label->setObjectName(QStringLiteral("muted"));
        cardLayout->addWidget(label);
        layout->addWidget(card, 0, column);
        return label;
    }
};

} // namespace

int main(int argc, char *argv[]) {
    std::fprintf(stderr, "desktop: QApplication\n"); std::fflush(stderr);
    QApplication app(argc, argv);
    std::fprintf(stderr, "desktop: MainWindow\n"); std::fflush(stderr);
    app.setApplicationName(QStringLiteral("StreamCam"));
    app.setOrganizationName(QStringLiteral("StreamCam"));
    MainWindow window;
    std::fprintf(stderr, "desktop: show\n"); std::fflush(stderr);
    window.show();
    return app.exec();
}
