# StreamCam

StreamCam conecta el micrófono y la cámara de un teléfono Android con OBS Studio en Windows. La transmisión puede ser **solo video**, **solo audio** o **audio y video**. La conexión es directa por USB/ADB o por Wi‑Fi en la red local; no usa cuentas ni servidores de StreamCam.

## Requisitos

- Android 10 o posterior, con cámara y/o micrófono según el modo elegido.
- Windows de 64 bits y OBS Studio.
- Para USB: cable que transfiera datos y depuración USB activada y autorizada.
- Para Wi‑Fi: ambos equipos en la misma red local. Algunas redes de invitados aíslan los dispositivos.

## Empezar

Consulta [Instalación y uso](docs/INSTALACION.md) para instalar el APK, preparar Windows/OBS, conectar por USB o Wi‑Fi y configurar audio. Si compilás desde el código, seguí [BUILDING.md](BUILDING.md).

1. Abrí StreamCam en Android y StreamCam para Windows.
2. Elegí USB o Wi‑Fi en Windows y conectá el teléfono.
3. En Android elegí **Solo video**, **Solo audio** o **Audio + video**. La app pide permiso de micrófono solo si el modo lo necesita.
4. Aceptá los permisos y tocá **Iniciar**. En OBS agrega **Fuentes → + → StreamCam**. Para otras aplicaciones, iniciá **OBS Virtual Camera**.

## Qué incluye

- Cámara frontal/trasera, perfiles SD/HD/Full HD y bitrate configurable.
- Video H.264 por hardware. Audio WebRTC por Wi‑Fi y PCM 48 kHz por USB.
- Emparejamiento protegido: QR efímero y TLS fijado a la identidad de Windows por Wi‑Fi; autenticación del dispositivo por USB.
- Servicio Android en primer plano con notificación para detener la captura.
- Fuente OBS nativa con video y audio.

## Ayuda

Los problemas habituales y sus soluciones están en [Solución de problemas](docs/SOLUCION_DE_PROBLEMAS.md). Revisa también [prueba de integración](docs/PRUEBA_INTEGRACION.md), [conexión USB](docs/USB_ADB.md) y [seguridad](SECURITY.md).

## Compilar desde código

Consulta [BUILDING.md](BUILDING.md), [Android](android/README.md) y [Windows](desktop/README.md). No se incluyen SDKs de terceros, claves privadas, datos del dispositivo ni APKs en el historial Git.

## Privacidad

El audio y el video se envían únicamente a la PC vinculada mientras la transmisión está activa. El audio está desactivado en el modo Solo video. No hay grabación ni almacenamiento de medios en StreamCam. Las claves privadas se generan localmente; los QR caducan.

Licencia GPL-3.0-or-later. Consulta [avisos de terceros](docs/THIRD_PARTY.md), [seguridad](SECURITY.md) y [contribución](CONTRIBUTING.md).
