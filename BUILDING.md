# Compilar StreamCam

## Android

JDK 17, Android SDK / Build Tools 35 y Gradle 8.11.1. Las dependencias están fijadas en los archivos Gradle y verificadas por SHA-256. Consultar [android/README.md](android/README.md).

```powershell
gradle -p android --no-daemon assembleDebug lintDebug
```

## Windows

Visual Studio 2022 Build Tools con C++ x64 y CMake, Qt 6.8.3 MSVC2022 x64 (Widgets, Network y WebSockets) y libwebrtc crow-misia 152.7977.0.0 con MSVC STL. No mezclar con un SDK compilado con libc++.

Descargar el SDK nativo de la [release fijada de libwebrtc](https://github.com/crow-misia/libwebrtc-bin/releases/tag/152.7977.0.0) en `.tooling/webrtc-msvc`, o pasar `STREAMCAM_WEBRTC_ROOT` a CMake. Se necesitan `include/`, `release/webrtc.lib` y los avisos de licencia.

```powershell
./desktop/build.ps1 -QtPath 'C:/Qt/6.8.3/msvc2022_64'
./desktop/package.ps1
```

El empaquetado deja `dist/StreamCam-preview.apk` y `dist/StreamCam-Windows-x64-preview.zip`. También crea `dist/StreamCam-preview/` descomprimido. `StreamCam.exe` debe ejecutarse desde esa carpeta porque usa las DLL y el runtime que están a su lado; para compartirlo, usa el ZIP completo.

Para la identidad TLS, el paquete usa PowerShell 7 portable; un build de desarrollo puede usar `pwsh` desde PATH. Para USB se necesita Android Platform Tools (ADB). No se guardan SDKs ni runtimes en Git.

## Plugin OBS

Usar las cabeceras de OBS 33.0.1 y una biblioteca de importación correspondiente al `obs.dll` instalado. Configurar `OBS_SOURCE_ROOT` y `OBS_IMPORT_LIBRARY` en CMake para `obs-plugin/`. El plugin se instala con OBS cerrado mediante `obs-plugin/install.ps1`.

## Pruebas

Los probes C++ se generan junto con Windows. `desktop/check-auth.py` necesita websockets y cryptography; `desktop/check-media.py` necesita aiortc y PyAV; `desktop/check-qr.py` usa zxing-cpp y Pillow. Instalarlos en un entorno Python separado.

```powershell
python desktop/check-auth.py
$env:STREAMCAM_AUTH_USB='1'
python desktop/check-auth.py
```

Para pruebas OBS, configurar `OBS_STUDIO_DIR` con la carpeta de instalación. Las pruebas de medios usan imágenes sintéticas; no sustituyen la aceptación física en un teléfono.

En Windows, después de compilar, ejecutá `./scripts/check-identity-policy.ps1` para reproducir el bloqueo de un script descargado bajo AllSigned y comprobar que StreamCam crea y recarga su identidad sin cambiar la política permanente del equipo. La prueba usa una identidad temporal, sin tocar los teléfonos vinculados.

## Preparar una publicación

Ejecutar `python scripts/audit-public.py` (o `scripts/audit-public.ps1` en Windows) antes de generar la fuente pública. `scripts/prepare-release.ps1` ejecuta la auditoría PowerShell, valida los hashes y los archivos necesarios y genera un ZIP Windows completo más el APK de prueba. Los diagnósticos y las notas locales están ignorados. Las releases estables deben incluir licencia y avisos de todos los componentes, APK firmado por el mantenedor, binarios Windows, plugin y hashes SHA-256. Nunca empaquetar identidades, dispositivos confiables, claves de firma o logs. Publicar únicamente después de completar la matriz de aceptación física.
