# Cliente Windows

La aplicación Qt para Windows maneja emparejamiento USB/ADB y Wi‑Fi, recibe WebRTC, decodifica H.264 con Media Foundation y publica audio/video por IPC local a la fuente de OBS. Consulta [BUILDING.md](../BUILDING.md) para requisitos y compilación, y [Instalación](../docs/INSTALACION.md) para uso.

El receptor admite pista de audio Opus por Wi‑Fi y paquetes PCM mono 48 kHz por USB. El micrófono se abre solo cuando el usuario lo selecciona en Android. La salida a OBS es local y transitoria; StreamCam no escribe los medios a disco.

La compilación requiere Visual Studio 2022 C++ x64, Qt 6, la distribución WebRTC indicada en `BUILDING.md`, OBS SDK e import library compatibles con el OBS objetivo.
