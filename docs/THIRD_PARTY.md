# Componentes de terceros

StreamCam utiliza Qt 6.8.3 con enlace dinámico, libwebrtc Windows crow-misia 152.7977.0.0, WebRTC Android io.github.webrtc-sdk 150.7871.04, ZXing Android Embedded 4.3.0, OkHttp 4.12.0 y QR Code Generator de Nayuki 1.8.0. El plugin se compila contra libobs 33.0.1 instalado por el usuario.

La interfaz Android utiliza Material Components for Android 1.12.0 (Apache-2.0): https://github.com/material-components/material-components-android/tree/1.12.0

El paquete incluye PowerShell portable con sus archivos de licencia en runtime/powershell: https://github.com/PowerShell/PowerShell (MIT y avisos de componentes incluidos).

- Qt: https://www.qt.io/licensing/open-source-lgpl-obligations — conservar las DLLs separadas y reemplazables. Fuentes de la versión: https://download.qt.io/archive/qt/6.8/6.8.3/
- WebRTC: licencia BSD y avisos de dependencias incluidos en el SDK (`NOTICE`). Build Windows: https://github.com/crow-misia/libwebrtc-bin/releases/tag/152.7977.0.0
- WebRTC Android: https://github.com/webrtc-sdk/android
- ZXing Embedded: Apache-2.0, https://github.com/journeyapps/zxing-android-embedded
- OkHttp: Apache-2.0, https://github.com/square/okhttp
- Nayuki QR: MIT, encabezados de licencia conservados en `desktop/app/third_party/qrcodegen.*`; https://github.com/nayuki/QR-Code-generator/tree/v1.8.0
- OBS/libobs y el plugin que lo enlaza: GPL-2.0-or-later, https://github.com/obsproject/obs-studio/tree/33.0.1

El paquete de integración contiene binarios de desarrollo y no incluye OBS. Los SDKs y fuentes usados se fijan en el proyecto; los hashes del paquete están en SHA256SUMS.txt.
