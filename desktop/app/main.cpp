#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QDoubleSpinBox>
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
#include <QProgressBar>
#include <algorithm>
#include <cmath>
#include <QElapsedTimer>
#include <QThread>
#include <QMetaObject>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedLayout>
#include <optional>
#include <functional>
#include <mutex>
#include <atomic>
#include "PairingServer.h"
#include "AudioEffectsAckTracker.h"
#include "ControlNegotiationState.h"
#include "PcIdentity.h"
#include "QrImage.h"
#include <QNetworkInterface>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QTimer>
#include "UpdateRelease.h"
#include <QMessageBox>
#include <QCoreApplication>
#include "../media/WebRtcReceiver.h"
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QLockFile>
#include <QDir>
#include <cstdio>
#include "../ipc/FramePublisher.h"
#include "../ipc/FrameOutputWorker.h"
#include "../ipc/AudioPublisher.h"
#include "../ipc/AudioOutputWorker.h"

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
        errorDetailsButton_ = new QPushButton(QStringLiteral("Ver detalle del error"), connection);
        errorDetailsButton_->setVisible(false);
        connect(errorDetailsButton_, &QPushButton::clicked, this, [this] {
            QMessageBox box(QMessageBox::Warning, QStringLiteral("StreamCam · Detalle de conexión"),
                QStringLiteral("La conexión no pudo completarse. Revisá el detalle y volvé a intentar."), QMessageBox::Ok, this);
            box.setDetailedText(lastError_);
            box.exec();
        });
        connectionLayout->addWidget(errorDetailsButton_);
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
        auto *voiceToggle=new QPushButton(QStringLiteral("Controles de audio ▾"),connection);
        voiceToggle->setObjectName(QStringLiteral("textAction"));voiceToggle->setCheckable(true);
        connectionLayout->addWidget(voiceToggle);
        auto *voicePanel=new QWidget(connection);
        auto *voiceLayout=new QVBoxLayout(voicePanel);voiceLayout->setContentsMargins(0,4,0,4);
        auto *muteAudio=new QCheckBox(QStringLiteral("Silenciar audio hacia OBS"),voicePanel);
        muteAudio->setAccessibleName(QStringLiteral("Silenciar la salida de micrófono hacia OBS"));
        auto *inputGainLabel=new QLabel(voicePanel);
        auto *inputGain=new QSlider(Qt::Horizontal,voicePanel);inputGain->setRange(-18,18);inputGain->setSingleStep(1);
        auto *outputGainLabel=new QLabel(voicePanel);
        auto *outputGain=new QSlider(Qt::Horizontal,voicePanel);outputGain->setRange(-18,12);outputGain->setSingleStep(1);
        auto *compressor=new QCheckBox(QStringLiteral("Compresor suave · 3:1"),voicePanel);
        auto *compressorThresholdLabel=new QLabel(voicePanel);
        auto *compressorThreshold=new QSlider(Qt::Horizontal,voicePanel);compressorThreshold->setRange(-36,-6);compressorThreshold->setSingleStep(1);
        auto *compressorRatioLabel=new QLabel(voicePanel);
        auto *compressorRatio=new QSlider(Qt::Horizontal,voicePanel);compressorRatio->setRange(1,10);compressorRatio->setSingleStep(1);
        auto *noiseGate=new QCheckBox(QStringLiteral("Puerta de ruido · silenciar silencios"),voicePanel);
        auto *gateThresholdLabel=new QLabel(voicePanel);
        auto *gateThreshold=new QSlider(Qt::Horizontal,voicePanel);gateThreshold->setRange(-72,-24);gateThreshold->setSingleStep(1);
        auto *echoEffect=new QCheckBox(QStringLiteral("Eco creativo · repeticiones hacia OBS"),voicePanel);
        auto *echoDelayLabel=new QLabel(voicePanel);
        auto *echoDelay=new QSlider(Qt::Horizontal,voicePanel);echoDelay->setRange(30,500);echoDelay->setSingleStep(10);
        auto *echoFeedbackLabel=new QLabel(voicePanel);
        auto *echoFeedback=new QSlider(Qt::Horizontal,voicePanel);echoFeedback->setRange(0,70);echoFeedback->setSingleStep(5);
        auto *echoMixLabel=new QLabel(voicePanel);
        auto *echoMix=new QSlider(Qt::Horizontal,voicePanel);echoMix->setRange(0,75);echoMix->setSingleStep(5);
        auto *echoControl=new QCheckBox(QStringLiteral("Cancelar eco en Android · al iniciar"),voicePanel);
        echoControl->setToolTip(QStringLiteral("La cancelación de eco se configura al iniciar la captura y depende del modelo y la entrada seleccionada."));
        auto *voiceInfo=new QLabel(QStringLiteral("El eco creativo agrega repeticiones; la cancelación de eco en Android reduce el retorno del parlante al micrófono. Ganancia, compresor, puerta y eco creativo se aplican antes de OBS por USB y Wi-Fi. La puerta no reemplaza la supresión de ruido."),voicePanel);
        voiceInfo->setObjectName(QStringLiteral("muted"));voiceInfo->setWordWrap(true);
        voiceLayout->addWidget(muteAudio);voiceLayout->addWidget(inputGainLabel);voiceLayout->addWidget(inputGain);
        voiceLayout->addWidget(outputGainLabel);voiceLayout->addWidget(outputGain);
        voiceLayout->addWidget(compressor);voiceLayout->addWidget(compressorThresholdLabel);voiceLayout->addWidget(compressorThreshold);
        voiceLayout->addWidget(compressorRatioLabel);voiceLayout->addWidget(compressorRatio);
        voiceLayout->addWidget(noiseGate);voiceLayout->addWidget(gateThresholdLabel);voiceLayout->addWidget(gateThreshold);
        voiceLayout->addWidget(echoEffect);voiceLayout->addWidget(echoDelayLabel);voiceLayout->addWidget(echoDelay);
        voiceLayout->addWidget(echoFeedbackLabel);voiceLayout->addWidget(echoFeedback);
        voiceLayout->addWidget(echoMixLabel);voiceLayout->addWidget(echoMix);
        voiceLayout->addWidget(echoControl);voiceLayout->addWidget(voiceInfo);
        // Keep the existing processing controls discoverable when the app opens.
        // The connection column is scrollable, so users can still reach pairing
        // and saved-device actions on shorter screens.
        connectionLayout->addWidget(voicePanel);voicePanel->show();voiceToggle->setChecked(true);
        connect(voiceToggle,&QPushButton::toggled,voicePanel,[voicePanel,voiceToggle](bool open) {
            voicePanel->setVisible(open);voiceToggle->setText(open ? QStringLiteral("Controles de audio ▾") : QStringLiteral("Controles de audio ▸"));
        });
        QSettings voicePreferences;
        muteAudio->setChecked(voicePreferences.value(QStringLiteral("voice/muted"),false).toBool());
        inputGain->setValue(voicePreferences.value(QStringLiteral("voice/inputGainDb"),0).toInt());
        outputGain->setValue(voicePreferences.value(QStringLiteral("voice/outputGainDb"),0).toInt());
        compressor->setChecked(voicePreferences.value(QStringLiteral("voice/compressor"),false).toBool());
        compressorThreshold->setValue(voicePreferences.value(QStringLiteral("voice/compressorThresholdDb"),-18).toInt());
        compressorRatio->setValue(voicePreferences.value(QStringLiteral("voice/compressorRatio"),3).toInt());
        noiseGate->setChecked(voicePreferences.value(QStringLiteral("voice/noiseGate"),false).toBool());
        gateThreshold->setValue(voicePreferences.value(QStringLiteral("voice/gateThresholdDb"),-48).toInt());
        echoEffect->setChecked(voicePreferences.value(QStringLiteral("voice/echoEffect"),false).toBool());
        echoDelay->setEnabled(echoEffect->isChecked());echoFeedback->setEnabled(echoEffect->isChecked());echoMix->setEnabled(echoEffect->isChecked());
        echoDelay->setValue(voicePreferences.value(QStringLiteral("voice/echoDelayMs"),180).toInt());
        echoFeedback->setValue(voicePreferences.value(QStringLiteral("voice/echoFeedback"),30).toInt());
        echoMix->setValue(voicePreferences.value(QStringLiteral("voice/echoMix"),25).toInt());
        echoControl->setChecked(voicePreferences.value(QStringLiteral("voice/echoCancellation"),true).toBool());
        inputGainLabel->setText(QStringLiteral("Ganancia de entrada · %1 dB").arg(inputGain->value()));
        outputGainLabel->setText(QStringLiteral("Nivel de salida · %1 dB").arg(outputGain->value()));
        compressorThresholdLabel->setText(QStringLiteral("Umbral del compresor · %1 dB").arg(compressorThreshold->value()));
        compressorRatioLabel->setText(QStringLiteral("Relación · %1:1").arg(compressorRatio->value()));
        gateThresholdLabel->setText(QStringLiteral("Umbral de puerta · %1 dB").arg(gateThreshold->value()));
        echoDelayLabel->setText(QStringLiteral("Demora del eco · %1 ms").arg(echoDelay->value()));
        echoFeedbackLabel->setText(QStringLiteral("Repeticiones · %1 %").arg(echoFeedback->value()));
        echoMixLabel->setText(QStringLiteral("Mezcla de eco · %1 %").arg(echoMix->value()));
        connect(inputGain,&QSlider::valueChanged,this,[this,inputGainLabel](int value) {
            inputGainLabel->setText(QStringLiteral("Ganancia de entrada · %1 dB").arg(value));
            QSettings().setValue(QStringLiteral("voice/inputGainDb"),value);applyVoiceSettings();
        });
        connect(muteAudio,&QCheckBox::toggled,this,[this](bool muted) {
            QSettings().setValue(QStringLiteral("voice/muted"),muted);applyVoiceSettings();
        });
        connect(outputGain,&QSlider::valueChanged,this,[this,outputGainLabel](int value) {
            outputGainLabel->setText(QStringLiteral("Nivel de salida · %1 dB").arg(value));
            QSettings().setValue(QStringLiteral("voice/outputGainDb"),value);applyVoiceSettings();
        });
        connect(compressor,&QCheckBox::toggled,this,[this](bool enabled) {
            QSettings().setValue(QStringLiteral("voice/compressor"),enabled);applyVoiceSettings();
        });
        connect(compressorThreshold,&QSlider::valueChanged,this,[this,compressorThresholdLabel](int value) {
            compressorThresholdLabel->setText(QStringLiteral("Umbral del compresor · %1 dB").arg(value));
            QSettings().setValue(QStringLiteral("voice/compressorThresholdDb"),value);applyVoiceSettings();
        });
        connect(compressorRatio,&QSlider::valueChanged,this,[this,compressorRatioLabel](int value) {
            compressorRatioLabel->setText(QStringLiteral("Relación · %1:1").arg(value));
            QSettings().setValue(QStringLiteral("voice/compressorRatio"),value);applyVoiceSettings();
        });
        connect(noiseGate,&QCheckBox::toggled,this,[this](bool enabled) {
            QSettings().setValue(QStringLiteral("voice/noiseGate"),enabled);applyVoiceSettings();
        });
        connect(gateThreshold,&QSlider::valueChanged,this,[this,gateThresholdLabel](int value) {
            gateThresholdLabel->setText(QStringLiteral("Umbral de puerta · %1 dB").arg(value));
            QSettings().setValue(QStringLiteral("voice/gateThresholdDb"),value);applyVoiceSettings();
        });
        connect(echoEffect,&QCheckBox::toggled,this,[this,echoDelay,echoFeedback,echoMix](bool enabled) {
            echoDelay->setEnabled(enabled);echoFeedback->setEnabled(enabled);echoMix->setEnabled(enabled);
            QSettings().setValue(QStringLiteral("voice/echoEffect"),enabled);applyVoiceSettings();
        });
        connect(echoDelay,&QSlider::valueChanged,this,[this,echoDelayLabel](int value) {
            echoDelayLabel->setText(QStringLiteral("Demora del eco · %1 ms").arg(value));
            QSettings().setValue(QStringLiteral("voice/echoDelayMs"),value);applyVoiceSettings();
        });
        connect(echoFeedback,&QSlider::valueChanged,this,[this,echoFeedbackLabel](int value) {
            echoFeedbackLabel->setText(QStringLiteral("Repeticiones · %1 %").arg(value));
            QSettings().setValue(QStringLiteral("voice/echoFeedback"),value);applyVoiceSettings();
        });
        connect(echoMix,&QSlider::valueChanged,this,[this,echoMixLabel](int value) {
            echoMixLabel->setText(QStringLiteral("Mezcla de eco · %1 %").arg(value));
            QSettings().setValue(QStringLiteral("voice/echoMix"),value);applyVoiceSettings();
        });
        connect(echoControl,&QCheckBox::toggled,this,[this](bool enabled) {
            QSettings().setValue(QStringLiteral("voice/echoCancellation"),enabled);
            sendAndroidAudioEffects();
        });
        applyVoiceSettings();
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
                activeSocket_->close(); activeSocket_.clear(); receiver_.reset(); frameOutput_.setSource(nullptr); audioOutput_.setSource(nullptr);
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
        connect(&pairing_, &PairingServer::confirmationInvalidated, this, [this](quint64 attemptId) {
            if (consentAttemptId_ != attemptId) return;
            consentAttemptId_ = 0;
            if (consentDialog_) consentDialog_->reject();
        });
        connect(&pairing_, &PairingServer::confirmationRequested, this, [this, qrLabel,state](quint64 attemptId, const QString &deviceId, bool reconnect) {
            qrLabel->clear(); qrLabel->hide();
            const bool usb=pairingAddress_==QStringLiteral("127.0.0.1");
            state->setText(usb ? QStringLiteral("Código USB de esta sesión: %1").arg(pairing_.usbVerificationCode()) : QStringLiteral("Confirmá la vinculación del teléfono"));
            QString fingerprint = QStringLiteral("Identidad nueva sin verificar");
            if (!deviceId.isEmpty() && deviceId.size() >= 24)
                fingerprint = deviceId.left(12) + QStringLiteral("…") + deviceId.right(8);
            auto *dialog=new QMessageBox(QMessageBox::Question,
                reconnect ? QStringLiteral("Autorizar reconexión") : QStringLiteral("Vincular teléfono"),
                QStringLiteral("%1 solicita %2.%4\nHuella del dispositivo: %3\n¿Querés autorizar esta identidad para la sesión?")
                    .arg(reconnect ? QStringLiteral("Un teléfono conocido") : QStringLiteral("Un teléfono"),
                         reconnect ? QStringLiteral("reconectarse") : QStringLiteral("vincularse"), fingerprint,
                         usb ? QStringLiteral("\nCódigo de verificación USB: %1\nComparalo con el que aparece en Android; autorizá solo si coincide.").arg(pairing_.usbVerificationCode()) : QString{}),
                QMessageBox::Yes|QMessageBox::No,this);
            dialog->setButtonText(QMessageBox::Yes,reconnect ? QStringLiteral("Reconectar") : QStringLiteral("Vincular"));
            dialog->setButtonText(QMessageBox::No,QStringLiteral("Cancelar"));
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            consentAttemptId_ = attemptId;
            consentDialog_=dialog;
            connect(dialog,&QDialog::finished,this,[this,attemptId](int result) {
                if (consentAttemptId_ != attemptId) return;
                consentAttemptId_ = 0;
                const bool current = pairing_.confirm(attemptId,result==QMessageBox::Yes);
                if (!current || result!=QMessageBox::Yes) {pending_=false;stateLabel_->setText(QStringLiteral("Vinculación cancelada o vencida"));updateControls();}
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
            voiceAckTracker_.reset();pendingVoiceRequestId_.clear();audioEffectsDirty_=false;
            const auto controlV2=std::make_shared<streamcam::ControlNegotiationState>();
            const bool wifiControl=pairingAddress_!=QStringLiteral("127.0.0.1");
            if(wifiControl)controlV2->markPaired({"audio.effects.ack"});
            try { receiver_ = std::make_unique<streamcam::WebRtcReceiver>(nullptr, pairingAddress_); }
            catch (const std::exception &) { socket->close(); activeSocket_.clear(); updateControls(); state->setText(QStringLiteral("No se pudo preparar el receptor de video. Volvé a conectar.")); return; }
            frameOutput_.setSource(receiver_->frameSource());
            audioOutput_.setSource(receiver_->audioSource());
            socket->setMaxAllowedIncomingMessageSize(pairingAddress_==QStringLiteral("127.0.0.1") ? 2*1024*1024+20 : 131072);
            socket->setMaxAllowedIncomingFrameSize(pairingAddress_==QStringLiteral("127.0.0.1") ? 2*1024*1024+20 : 131072);
            connect(receiver_.get(), &streamcam::WebRtcReceiver::signalReady, socket, [socket](const QJsonObject &message) {
                socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
            });
            applyVoiceSettings();
            connect(receiver_.get(), &streamcam::WebRtcReceiver::statusChanged, state, &QLabel::setText);
            connect(socket,&QWebSocket::binaryMessageReceived,this,[this,socket](const QByteArray &packet) {
                if(activeSocket_!=socket || !receiver_ || pairingAddress_!=QStringLiteral("127.0.0.1")) {socket->close();return;}
                try {receiver_->receiveUsb(packet);} catch(...) {socket->close();}
            });
            connect(socket, &QWebSocket::textMessageReceived, this, [this, socket, controlV2, wifiControl](const QString &text) {
                if (activeSocket_ != socket || !receiver_) return;
                const auto message = QJsonDocument::fromJson(text.toUtf8());
                if (!message.isObject()) { socket->close(); return; }
                try {
                    const auto object=message.object();
                    const auto type=object.value(QStringLiteral("type")).toString();
                    const auto parseCapabilities=[](const QJsonValue &value) {
                        if(!value.isArray())throw std::invalid_argument("capabilities must be an array");
                        const auto array=value.toArray();
                        if(array.size()>16)throw std::invalid_argument("too many capabilities");
                        streamcam::ControlNegotiationState::Capabilities capabilities;
                        for(const auto &entry:array) {
                            if(!entry.isString() || entry.toString().isEmpty() || entry.toString().size()>64 ||
                                !capabilities.insert(entry.toString().toStdString()).second)
                                throw std::invalid_argument("invalid or duplicate capability");
                        }
                        return capabilities;
                    };
                    const auto updateControlProperties=[socket,controlV2] {
                        socket->setProperty("controlV2Ready",controlV2->ready());
                        socket->setProperty("controlV2AudioAck",controlV2->supports("audio.effects.ack"));
                    };
                    if(type==QStringLiteral("control.ready")) {
                        if(object.size()!=3 || object.value(QStringLiteral("version")).toInt()!=2 || !wifiControl) {socket->close();return;}
                        controlV2->receiveReady(parseCapabilities(object.value(QStringLiteral("capabilities"))));
                        updateControlProperties();
                        if(controlV2->ready() && controlV2->supports("audio.effects.ack"))sendAndroidAudioEffects();
                        return;
                    }
                    if(type==QStringLiteral("control.hello")) {
                        if(object.size()!=3 || object.value(QStringLiteral("version")).toInt()!=2 || !wifiControl) {socket->close();return;}
                        const auto negotiated=controlV2->receiveHello(parseCapabilities(object.value(QStringLiteral("capabilities"))));
                        QJsonArray negotiatedJson;
                        for(const auto &capability:negotiated)negotiatedJson.append(QString::fromStdString(capability));
                        if(socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                            {QStringLiteral("type"),QStringLiteral("control.ready")},
                            {QStringLiteral("version"),2}, {QStringLiteral("capabilities"),negotiatedJson}
                        }).toJson(QJsonDocument::Compact)))<0) {socket->close();return;}
                        controlV2->markLocalReadySent(negotiated);
                        updateControlProperties();
                        if(controlV2->ready() && controlV2->supports("audio.effects.ack"))sendAndroidAudioEffects();
                    } else if(type==QStringLiteral("audio.effects")) {
                        if(object.size()!=4 || object.value(QStringLiteral("version")).toInt()!=2 ||
                            !controlV2->ready() || !controlV2->supports("audio.effects.ack") ||
                            !object.value(QStringLiteral("echoCancellation")).isBool()) {socket->close();return;}
                        const auto requestId=object.value(QStringLiteral("request_id")).toString();
                        if(requestId.size()!=32 || requestId.contains(QRegularExpression(QStringLiteral("[^0-9a-fA-F]")))) {socket->close();return;}
                        socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                            {QStringLiteral("type"),QStringLiteral("audio.effects.ack")},
                            {QStringLiteral("version"),2},{QStringLiteral("request_id"),requestId},{QStringLiteral("ok"),true}
                        }).toJson(QJsonDocument::Compact)));
                    } else if(type==QStringLiteral("audio.effects.ack")) {
                        const auto requestId=object.value(QStringLiteral("request_id")).toString();
                        if(!socket->property("controlV2AudioAck").toBool() || object.size()!=4 ||
                            object.value(QStringLiteral("version")).toInt()!=2 || requestId.size()!=32 ||
                            requestId.contains(QRegularExpression(QStringLiteral("[^0-9a-fA-F]"))) ||
                            !object.value(QStringLiteral("ok")).isBool()) {socket->close();return;}
                        const auto ack=voiceAckTracker_.acknowledge(requestId.toStdString());
                        if(ack==streamcam::AudioEffectsAckTracker::AckAction::Unexpected) {socket->close();return;}
                        if(ack==streamcam::AudioEffectsAckTracker::AckAction::Duplicate)return;
                        pendingVoiceRequestId_.clear();
                        stateLabel_->setText(object.value(QStringLiteral("ok")).toBool()
                            ? QStringLiteral("Android guardó la preferencia de cancelación de eco")
                            : QStringLiteral("Android no pudo guardar la preferencia de audio"));
                        if(audioEffectsDirty_) {audioEffectsDirty_=false;sendAndroidAudioEffects();}
                    } else if(type==QStringLiteral("video.stop") || type==QStringLiteral("media.stop")) {
                        if(object.size()!=1) {socket->close();return;}
                        receiver_->reset(pairingAddress_);frameOutput_.clear();audioOutput_.clear();
                        stateLabel_->setText(QStringLiteral("Transmisión detenida · podés volver a iniciarla desde Android"));
                    } else {
                        if(type==QStringLiteral("control.hello") || type==QStringLiteral("control.ready") || type==QStringLiteral("audio.effects") ||
                            pairingAddress_==QStringLiteral("127.0.0.1") || (type!=QStringLiteral("rtc.offer") && type!=QStringLiteral("rtc.ice"))) {socket->close();return;}
                        receiver_->receive(object);
                    }
                } catch (...) { socket->close(); }
            });
            if(wifiControl) {
                controlV2->startLocalHello();
                socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"),QStringLiteral("control.hello")},
                    {QStringLiteral("version"),2},{QStringLiteral("capabilities"),QJsonArray{QStringLiteral("audio.effects.ack")}}
                }).toJson(QJsonDocument::Compact)));
            }
            connect(socket, &QWebSocket::disconnected, this, [this, socket, state] {
                if (activeSocket_ == socket) { receiver_.reset(); frameOutput_.setSource(nullptr); audioOutput_.setSource(nullptr); activeSocket_.clear(); pending_=false; voiceAckTracker_.reset();pendingVoiceRequestId_.clear();audioEffectsDirty_=false;updateControls(); state->setText(QStringLiteral("Teléfono desconectado · volvé a conectar cuando quieras")); }
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
        auto *virtualMicrophone=new QPushButton(QStringLiteral("Usar audio en Discord u otras apps"),connection);
        connectionLayout->addWidget(virtualMicrophone);
        connect(virtualMicrophone,&QPushButton::clicked,this,[this] {
            QMessageBox box(QMessageBox::Information,QStringLiteral("Micrófono virtual para otras aplicaciones"),
                QStringLiteral("StreamCam ya entrega el micrófono del teléfono a la fuente StreamCam de OBS. Para una transmisión hecha en OBS, agregá esa fuente y asegurate de que esté habilitada en el mezclador.\n\n"
                    "Discord y otras aplicaciones necesitan un dispositivo de entrada de Windows. La cámara virtual de OBS solo lleva video. Para el audio hace falta un controlador de cable virtual, como VB-CABLE, y configurar OBS para enviar el audio de StreamCam a ese cable.\n\n"
                    "1. Instalá VB-CABLE desde el sitio oficial y reiniciá Windows si el instalador lo solicita.\n"
                    "2. En OBS: Archivo → Configuración → Audio → Avanzado → Dispositivo de monitorización: CABLE Input (VB-Audio Virtual Cable).\n"
                    "3. En OBS: Editar → Propiedades de audio avanzadas → fila StreamCam → Monitorización de audio: Monitorizar y salida.\n"
                    "4. En Discord: Ajustes → Voz y vídeo → Dispositivo de entrada: CABLE Output (VB-Audio Virtual Cable).\n\n"
                    "Para enviar el micrófono solo a Discord, usá Monitorizar solamente en el paso 3. Para incluirlo en tu directo de OBS, usá Monitorizar y salida. Evitá capturar el retorno del cable en OBS para prevenir eco."),
                QMessageBox::Ok,this);
            auto *download=box.addButton(QStringLiteral("Descargar VB-CABLE (sitio oficial)"),QMessageBox::ActionRole);
            box.exec();
            if(box.clickedButton()==download)
                QDesktopServices::openUrl(QUrl(QStringLiteral("https://vb-audio.com/Cable/")));
        });
        auto *updates = new QPushButton(QStringLiteral("Verificar actualizaciones"), root);
        updates->setObjectName(QStringLiteral("textAction"));
        columns->addWidget(updates, 0, 1, 1, 1, Qt::AlignRight);
        connect(updates, &QPushButton::clicked, this, [this, updates] { checkForUpdates(updates); });
        auto *version = new QLabel(QStringLiteral("Versión ") + QCoreApplication::applicationVersion(), root);
        version->setObjectName(QStringLiteral("muted"));
        columns->addWidget(version, 1, 1, 1, 1, Qt::AlignRight);
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
        const QPointer<MainWindow> windowGuard(this);
        frameOutput_.start([windowGuard](const QString &) { if(windowGuard)windowGuard->statusBar()->showMessage(QStringLiteral("No se pudo abrir la salida OBS: otra instancia puede estar activa.")); });
        audioOutput_.start([windowGuard](const QString &) { if(windowGuard)windowGuard->statusBar()->showMessage(QStringLiteral("No se pudo abrir la salida OBS de audio.")); });
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
            const QImage image(frame->bgra.data(), frame->width, frame->height, frame->stride, QImage::Format_ARGB32);
            videoImage->setPixmap(QPixmap::fromImage(image).scaled(videoImage->size().expandedTo(QSize(1,1)), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            previewStack->setCurrentWidget(videoImage);
        });
        frameTimer->start(33);
        auto *audioQueueStatus=new QLabel(QStringLiteral("Audio · cola: 0/100 · perdidos: 0"),this);
        audioQueueStatus->setAccessibleName(QStringLiteral("Cola de audio y paquetes perdidos"));
        audioQueueStatus->setToolTip(QStringLiteral("Cola recibida y descartes acumulados en recepción y envío a clientes OBS locales. El mismo paquete puede contar por cada cliente lento. Los descartes se reinician al reabrir StreamCam."));
        statusBar()->addPermanentWidget(audioQueueStatus);
        auto *audioTimer=new QTimer(this);audioTimer->setInterval(250);
        connect(audioTimer,&QTimer::timeout,this,[this,audioQueueStatus] {
            const auto queued=receiver_ ? receiver_->droppedAudioFrames() : 0;
            const auto backlog=receiver_ ? receiver_->queuedAudioFrames() : 0;
            const auto underflow=receiver_ && receiver_->audioUnderflow();
            audioQueueStatus->setText(QStringLiteral("Audio%1 · cola: %2/%3 · perdidos: %4")
                .arg(underflow ? QStringLiteral(" · SIN SEÑAL") : QString())
                .arg(backlog).arg(streamcam::LatestAudio::MaxQueuedFrames).arg(queued+audioOutput_.droppedFrames()));
            audioQueueStatus->setStyleSheet(underflow ? QStringLiteral("QLabel { color: #c62828; font-weight: bold; }") : QString());
        });
        audioTimer->start();

        auto *audioMeter=new QProgressBar(this);
        audioMeter->setRange(0,60);audioMeter->setValue(0);audioMeter->setTextVisible(false);
        audioMeter->setMaximumWidth(120);audioMeter->setAccessibleName(QStringLiteral("Nivel de salida de audio hacia OBS"));
        audioMeter->setToolTip(QStringLiteral("Nivel pico de la señal procesada que se envía a OBS, en dBFS."));
        auto *audioMeterLabel=new QLabel(QStringLiteral("Nivel OBS: −∞ dBFS"),this);
        statusBar()->addPermanentWidget(audioMeterLabel);
        statusBar()->addPermanentWidget(audioMeter);
        auto *audioMeterTimer=new QTimer(this);audioMeterTimer->setInterval(50);
        connect(audioMeterTimer,&QTimer::timeout,this,[this,audioMeter,audioMeterLabel] {
            const auto dbfs=audioOutput_.outputLevelDbfs();
            const auto level=std::clamp(static_cast<int>(std::lround(dbfs+60.0F)),0,60);
            audioMeter->setValue(level);
            audioMeterLabel->setText(dbfs<=-90.0F ? QStringLiteral("Nivel OBS: −∞ dBFS")
                : QStringLiteral("Nivel OBS: %1 dBFS").arg(dbfs,0,'f',1));
        });
        audioMeterTimer->start();

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
            QSlider::groove:horizontal { height: 5px; background: #d6dae1; border-radius: 2px; }
            QSlider::handle:horizontal { width: 16px; margin: -6px 0; background: #0b57d0; border-radius: 8px; }
            QCheckBox { spacing: 8px; }
            QProgressBar { border: 0; border-radius: 3px; background: #e6e8eb; }
            QProgressBar::chunk { border-radius: 3px; background: #188038; }
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
        frameOutput_.stop();audioOutput_.stop();receiver_.reset();pairing_.stop();
    }

private:
    void checkForUpdates(QPushButton *button) {
        button->setEnabled(false);
        button->setText(QStringLiteral("Buscando actualizaciones…"));
        auto *manager = new QNetworkAccessManager(this);
        QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/julianzanutti/StreamCam/releases?per_page=20")));
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setRawHeader("User-Agent", "StreamCam/" + QCoreApplication::applicationVersion().toUtf8());
        request.setTransferTimeout(15000);
        auto *reply = manager->get(request);
        auto *timeout = new QTimer(reply);
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
        timeout->start(20000);
        connect(reply, &QIODevice::readyRead, reply, [reply] {
            if (reply->bytesAvailable() > 1024 * 1024) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, manager, button] {
            button->setText(QStringLiteral("Verificar actualizaciones"));
            button->setEnabled(true);
            const bool ok = reply->error() == QNetworkReply::NoError &&
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
            const auto release = ok ? streamcam::newestWindowsRelease(reply->readAll()) : std::nullopt;
            manager->deleteLater();
            if (!release) {
                QMessageBox::warning(this, QStringLiteral("Actualizaciones"),
                    QStringLiteral("No pudimos consultar las actualizaciones. Revisá tu conexión e intentá de nuevo más tarde."));
            } else if (!streamcam::isNewerRelease(*release, QCoreApplication::applicationVersion())) {
                QMessageBox::information(this, QStringLiteral("Actualizaciones"),
                    QStringLiteral("Tenés la última versión de StreamCam (%1).").arg(QCoreApplication::applicationVersion()));
            } else {
                QMessageBox box(QMessageBox::Information, QStringLiteral("Actualización disponible"),
                    QStringLiteral("Está disponible StreamCam %1. Tu versión es %2.")
                        .arg(release->tag, QCoreApplication::applicationVersion()), QMessageBox::Cancel, this);
                auto *download = box.addButton(QStringLiteral("Abrir descarga"), QMessageBox::AcceptRole);
                box.setButtonText(QMessageBox::Cancel, QStringLiteral("Más tarde"));
                box.exec();
                if (box.clickedButton() == download && !QDesktopServices::openUrl(release->url))
                    QMessageBox::warning(this, QStringLiteral("Actualizaciones"),
                        QStringLiteral("No se pudo abrir el navegador. Buscá las publicaciones de julianzanutti/StreamCam en GitHub."));
            }
        });
    }
    QLabel *stateLabel_=nullptr, *hintLabel_=nullptr, *qrLabel_=nullptr;
    QComboBox *transport_=nullptr;
    QPushButton *pairButton_=nullptr, *cancelButton_=nullptr, *refreshButton_=nullptr, *revokeButton_=nullptr;
    QPushButton *errorDetailsButton_=nullptr;
    QString lastError_;
    QPointer<QProcess> adbProcess_;
    QPointer<QMessageBox> consentDialog_;
    quint64 consentAttemptId_=0;
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
        voiceAckTracker_.reset();pendingVoiceRequestId_.clear();audioEffectsDirty_=false;
        if(consentDialog_)consentDialog_->reject();
        if(adbProcess_) { adbProcess_->disconnect(this); adbProcess_->kill(); adbProcess_->deleteLater(); adbProcess_.clear(); }
        if(activeSocket_)activeSocket_->close();
        activeSocket_.clear();receiver_.reset();frameOutput_.setSource(nullptr);audioOutput_.setSource(nullptr);pairing_.stop();qrLabel_->clear();qrLabel_->hide();updateControls();
    }
    void connectionFailed(const QString &message) {
        cancelConnection();
        lastError_ = message;
        stateLabel_->setText(message.size() > 180 ? message.left(177)+QStringLiteral("…") : message);
        errorDetailsButton_->show();
    }
    void runAdb(const QString &executable,const QStringList &arguments,unsigned generation,std::function<void()> next) {
        auto *process=new QProcess(this);adbProcess_=process;
        auto settled=std::make_shared<bool>(false);
        auto complete=[this,process,generation,settled,next](bool success) {
            if(*settled)return;*settled=true;
            if(adbProcess_==process)adbProcess_.clear();
            const auto output=process->readAllStandardOutput()+process->readAllStandardError();
            process->deleteLater();
            if(generation!=connectionGeneration_)return;
            if(!success || output.contains("Error:") || output.contains("Exception")) {
                if (output.contains("unauthorized")) {
                    connectionFailed(QStringLiteral("El teléfono no autorizó la depuración USB. Desbloquealo y aceptá el permiso de esta PC."));return;
                }
                if (output.contains("more than one device")) {
                    connectionFailed(QStringLiteral("Hay más de un dispositivo ADB. Desconectá los otros teléfonos o emuladores y reintentá."));return;
                }
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
        errorDetailsButton_->hide();lastError_.clear();
        cancelConnection();preparing_=true;updateControls();
        stateLabel_->setText(QStringLiteral("Preparando conexión segura…"));
        pairingAddress_=transport_->currentData().toString();
        const auto generation=connectionGeneration_;
        struct IdentityResult { std::optional<PcIdentity> identity; QString error; };
        auto result=std::make_shared<IdentityResult>();
        const auto script=QCoreApplication::applicationDirPath()+QStringLiteral("/create-identity.ps1");
        auto *task=QThread::create([result,script] {
            try { static std::mutex identityMutex; std::lock_guard lock(identityMutex); result->identity=PcIdentity::load(script); }
            catch(const std::exception &error) {result->error=QString::fromUtf8(error.what());}
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
                if(!QFileInfo::exists(adb)) {
#ifdef STREAMCAM_DEV_SYSTEM_TOOLS
                    adb=QStandardPaths::findExecutable(QStringLiteral("adb"));
#else
                    adb.clear();
#endif
                }
                if(adb.isEmpty()) {connectionFailed(QStringLiteral("Falta Android Platform Tools. Volvé a extraer el paquete completo de StreamCam."));return;}
                stateLabel_->setText(QStringLiteral("Buscando el teléfono por USB…"));
                runAdb(adb,{QStringLiteral("reverse"),QStringLiteral("tcp:47831"),QStringLiteral("tcp:47831")},generation,[this,adb,identity,generation] {
                    runAdb(adb,{QStringLiteral("shell"),QStringLiteral("am"),QStringLiteral("start"),QStringLiteral("-n"),
                        QStringLiteral("com.streamcam.diagnostics/.PairingActivity"),QStringLiteral("-a"),QStringLiteral("com.streamcam.USB_CONNECT"),
                        QStringLiteral("--es"),QStringLiteral("pc_id"),identity.id,QStringLiteral("--es"),QStringLiteral("pc_pin"),QString::fromLatin1(identity.pin.toHex())},generation,[this] {
                            preparing_=false;pending_=true;updateControls();stateLabel_->setText(QStringLiteral("Teléfono detectado · código USB de esta sesión: %1").arg(pairing_.usbVerificationCode()));
                        });
                });
            } catch(const std::exception &) {connectionFailed(QStringLiteral("No se pudo abrir la conexión. Cerrá otras instancias de StreamCam y reintentá."));}
        });
        task->start();
    }
    PairingServer pairing_;
    QPointer<QWebSocket> activeSocket_;
    std::unique_ptr<streamcam::WebRtcReceiver> receiver_;
    streamcam::FrameOutputWorker frameOutput_;
    streamcam::AudioOutputWorker audioOutput_;
    QString pairingAddress_;
    QLabel *metricLabels_[4]{};
    QElapsedTimer frameMeasure_;
    std::uint64_t lastReceivedCount_=0;
    streamcam::AudioEffectsAckTracker voiceAckTracker_;
    QString pendingVoiceRequestId_;
    bool pendingVoiceEchoCancellation_=true;
    bool audioEffectsDirty_=false;
    void applyVoiceSettings() {
        QSettings settings;
        streamcam::VoiceSettings voice;
        voice.inputGainDb=settings.value(QStringLiteral("voice/inputGainDb"),0).toFloat();
        voice.outputGainDb=settings.value(QStringLiteral("voice/outputGainDb"),0).toFloat();
        voice.muted=settings.value(QStringLiteral("voice/muted"),false).toBool();
        voice.compressor=settings.value(QStringLiteral("voice/compressor"),false).toBool();
        voice.thresholdDb=settings.value(QStringLiteral("voice/compressorThresholdDb"),-18).toFloat();
        voice.ratio=settings.value(QStringLiteral("voice/compressorRatio"),3).toFloat();
        voice.noiseGate=settings.value(QStringLiteral("voice/noiseGate"),false).toBool();
        voice.gateDb=settings.value(QStringLiteral("voice/gateThresholdDb"),-48).toFloat();
        voice.echoEffect=settings.value(QStringLiteral("voice/echoEffect"),false).toBool();
        voice.echoDelayMs=settings.value(QStringLiteral("voice/echoDelayMs"),180).toFloat();
        voice.echoFeedback=settings.value(QStringLiteral("voice/echoFeedback"),30).toFloat()/100.0F;
        voice.echoMix=settings.value(QStringLiteral("voice/echoMix"),25).toFloat()/100.0F;
        audioOutput_.setVoiceSettings(voice);
    }
    void sendAndroidAudioEffects() {
        if(!receiver_ || !activeSocket_ || !activeSocket_->property("controlV2AudioAck").toBool() ||
            !activeSocket_->property("controlV2Ready").toBool())return;
        QSettings settings;
        const bool desired=settings.value(QStringLiteral("voice/echoCancellation"),true).toBool();
        if(voiceAckTracker_.hasPending()) {
            audioEffectsDirty_=desired!=pendingVoiceEchoCancellation_;
            return;
        }
        audioEffectsDirty_=false;
        const auto requestId=QUuid::createUuid().toString(QUuid::Id128);
        if(!voiceAckTracker_.begin(requestId.toStdString()))return;
        pendingVoiceRequestId_=requestId;
        pendingVoiceEchoCancellation_=desired;
        const QJsonObject message{{QStringLiteral("type"),QStringLiteral("audio.effects")},
            {QStringLiteral("echoCancellation"),desired},{QStringLiteral("version"),2},
            {QStringLiteral("request_id"),requestId}};
        if(activeSocket_->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)))<0) {
            voiceAckTracker_.abandon(requestId.toStdString());pendingVoiceRequestId_.clear();
            stateLabel_->setText(QStringLiteral("No se pudo enviar la preferencia de audio a Android."));
            return;
        }
        stateLabel_->setText(QStringLiteral("Esperando confirmación de la preferencia de audio en Android…"));
        scheduleAudioEffectsAckTimeout(activeSocket_,requestId);
    }
    void scheduleAudioEffectsAckTimeout(QPointer<QWebSocket> socket,const QString &requestId) {
        QTimer::singleShot(2000,this,[this,socket,requestId] {
            if(!socket || activeSocket_!=socket || pendingVoiceRequestId_!=requestId)return;
            const auto action=voiceAckTracker_.timeout(requestId.toStdString());
            if(action==streamcam::AudioEffectsAckTracker::TimeoutAction::Stale)return;
            if(action==streamcam::AudioEffectsAckTracker::TimeoutAction::Expired) {
                pendingVoiceRequestId_.clear();
                stateLabel_->setText(QStringLiteral("Android no confirmó el ajuste de cancelación de eco."));
                if(audioEffectsDirty_)sendAndroidAudioEffects();
                return;
            }
            const QJsonObject retry{{QStringLiteral("type"),QStringLiteral("audio.effects")},
                {QStringLiteral("echoCancellation"),pendingVoiceEchoCancellation_},{QStringLiteral("version"),2},
                {QStringLiteral("request_id"),requestId}};
            if(socket->sendTextMessage(QString::fromUtf8(QJsonDocument(retry).toJson(QJsonDocument::Compact)))<0) {
                voiceAckTracker_.abandon(requestId.toStdString());pendingVoiceRequestId_.clear();
                stateLabel_->setText(QStringLiteral("No se pudo reintentar el ajuste de audio en Android."));
                return;
            }
            stateLabel_->setText(QStringLiteral("Android aún no confirmó el ajuste de audio · reintentando…"));
            scheduleAudioEffectsAckTimeout(socket,requestId);
        });
    }
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
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("StreamCam"));
    app.setOrganizationName(QStringLiteral("StreamCam"));
    app.setApplicationVersion(QStringLiteral(STREAMCAM_APP_VERSION));
    const auto directory = QCoreApplication::applicationDirPath();
    const bool check = app.arguments().contains(QStringLiteral("--check-install"));
    const auto report = [check](const QString &message) {
        if (check) { std::fputs(message.toUtf8().constData(), stderr); std::fputs("\n", stderr); }
        else QMessageBox::critical(nullptr, QStringLiteral("StreamCam · No se pudo iniciar"), message);
        return 1;
    };
    if (check || QDir(directory+QStringLiteral("/runtime")).exists()) {
        for (const auto &relative : {"create-identity.ps1", "runtime/powershell/pwsh.exe",
                "runtime/powershell/System.Management.Automation.dll", "runtime/adb/adb.exe",
                "runtime/adb/AdbWinApi.dll", "runtime/adb/AdbWinUsbApi.dll", "platforms/qwindows.dll",
                "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"}) {
            if (!QFileInfo::exists(directory+QStringLiteral("/")+QString::fromLatin1(relative)))
                return report(QStringLiteral("Falta un componente: %1. Volvé a ejecutar el instalador de StreamCam.").arg(QString::fromLatin1(relative)));
        }
    }
    if (check) {
        try {
            QTemporaryDir temporary;
            if (!temporary.isValid()) return report(QStringLiteral("No se pudo crear la carpeta temporal de prueba."));
            const auto script = directory+QStringLiteral("/create-identity.ps1");
            const auto first = PcIdentity::load(script, temporary.path());
            if (PcIdentity::load(script, temporary.path()).pin != first.pin)
                return report(QStringLiteral("Falló la recarga de identidad."));
            QProcess adb;
            adb.start(directory+QStringLiteral("/runtime/adb/adb.exe"), {QStringLiteral("version")});
            if (!adb.waitForFinished(10000) || adb.exitStatus()!=QProcess::NormalExit || adb.exitCode()!=0)
                return report(QStringLiteral("No se pudo iniciar el componente USB."));
            std::puts("PASS: installed components, protected identity reload and ADB startup.");
            return 0;
        } catch (const std::exception &) { return report(QStringLiteral("Falló la comprobación de identidad del paquete.")); }
    }
    const auto dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDirectory.isEmpty() || !QDir().mkpath(dataDirectory))
        return report(QStringLiteral("No se pudo abrir la carpeta local de datos de StreamCam."));
    QLockFile instance(dataDirectory+QStringLiteral("/instance.lock"));
    if (!instance.tryLock(0)) {
        QMessageBox::information(nullptr, QStringLiteral("StreamCam"), QStringLiteral("StreamCam ya está abierto o su carpeta de datos está ocupada. Revisá la barra de tareas y usá una sola ventana."));
        return 0;
    }
    try {
        MainWindow window;
        window.show();
        return app.exec();
    } catch (const std::exception &) {
        return report(QStringLiteral("No se pudo iniciar StreamCam. Cerrá la aplicación y volvé a ejecutar el instalador. Si persiste, reportá el problema en GitHub."));
    }
}
