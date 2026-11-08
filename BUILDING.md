# Compilar StreamCam

## Android

JDK 17, Android SDK / Build Tools 35 y Gradle 8.11.1. Las dependencias están fijadas en los archivos Gradle y verificadas por SHA-256. Consultar [android/README.md](android/README.md).

```powershell
gradle -p android --no-daemon assembleDebug lintDebug
```

## Windows

Visual Studio 2022 Build Tools con C++ x64 y CMake, Qt 6.8.3 MSVC2022 x64 (Widgets, Network y WebSockets) y libwebrtc crow-misia 152.7977.0.0 con MSVC STL. No mezclar con un SDK compilado con libc++.

Descargar el asset `libwebrtc-win-x64.7z` de la [release fijada de libwebrtc](https://github.com/crow-misia/libwebrtc-bin/releases/tag/152.7977.0.0) y extraerlo en `.tooling/webrtc-msvc`, o pasar `STREAMCAM_WEBRTC_ROOT` a CMake. La versión, el asset y su SHA-256 aprobado están en [webrtc-sdk.json](desktop/media/webrtc-sdk.json). Se necesitan `include/`, `release/webrtc.lib` y los avisos de licencia.

```powershell
./desktop/build.ps1 -QtPath 'C:/Qt/6.8.3/msvc2022_64'
./desktop/package.ps1
```

El empaquetado deja `dist/StreamCam-preview.apk` y `dist/StreamCam-Windows-x64-preview.zip`. También crea `dist/StreamCam-preview/` descomprimido. `StreamCam.exe` debe ejecutarse desde esa carpeta porque usa las DLL y el runtime que están a su lado; para compartirlo, usa el ZIP completo.

Para la identidad TLS, el paquete usa PowerShell 7 portable; un build de desarrollo puede usar `pwsh` desde PATH. Para USB se necesita Android Platform Tools (ADB). No se guardan SDKs ni runtimes en Git.

## Plugin OBS

Usar las cabeceras de OBS 33.0.1 y una biblioteca de importación correspondiente a `obs.dll`. El workflow de Windows fija cabeceras y runtime OBS 33.0.1 en `desktop/media/obs-sdk.json`, verifica SHA-256, extrae solamente `obs.dll` del ZIP portable y genera la biblioteca con `scripts/create-obs-import-lib.ps1`. Para repetirlo localmente, tener las herramientas MSVC (`dumpbin.exe` y `lib.exe`) disponibles en `PATH` o Visual Studio instalado, y ejecutar el script indicando el DLL y rutas de salida. Configurar `OBS_SOURCE_ROOT` y `OBS_IMPORT_LIBRARY` en CMake para `obs-plugin/`. El plugin se instala con OBS cerrado mediante `obs-plugin/install.ps1`.

## Pruebas

Los probes C++ se generan junto con Windows. `desktop/check-auth.py` necesita websockets y cryptography; `desktop/check-media.py` necesita aiortc y PyAV; `desktop/check-qr.py` usa zxing-cpp y Pillow. Instalarlos en un entorno Python separado.

```powershell
python desktop/check-auth.py
$env:STREAMCAM_AUTH_USB='1'
python desktop/check-auth.py
```

Para pruebas OBS, configurar `OBS_STUDIO_DIR` con la carpeta de instalación. Las pruebas de medios usan imágenes sintéticas; no sustituyen la aceptación física en un teléfono.

### Loopback audiovisual con OBS portable

Con OBS 33.0.1 portable extraído dentro de `.tooling`, construir el target `streamcam-source-loopback` y `StreamCamObsSyntheticMediaProbe`. El harness `scripts/check-obs-loopback.ps1` modifica únicamente el perfil portable indicado, crea una fuente vía WebSocket y envía audio/cuadros sintéticos por named pipes QA separados de producción. No inicia stream ni grabación. El test desactiva la autenticación WebSocket solo en ese perfil temporal; no apuntarlo a una instalación de OBS de uso diario.

```powershell
./scripts/check-obs-loopback.ps1 `
  -ObsRoot '.tooling/OBS-Studio-33.0.1-portable-test' `
  -PluginDll 'desktop/out-v2/obs-plugin-v2/Release/streamcam-source-loopback.dll' `
  -SyntheticMediaProbe 'desktop/probes/out/Release/StreamCamObsSyntheticMediaProbe.exe'
```

En Windows, después de compilar, ejecutá `./scripts/check-identity-policy.ps1` para reproducir el bloqueo de un script descargado bajo AllSigned y comprobar que StreamCam crea y recarga su identidad sin cambiar la política permanente del equipo. La prueba usa una identidad temporal, sin tocar los teléfonos vinculados.

## Preparar una publicación

El instalador Windows se verificó con Inno Setup 7.1.1: `./desktop/package.ps1 -SkipArchive` genera la carpeta del paquete; `./desktop/installer/build.ps1 -Version '0.2.0-preview' -Compiler 'ruta/ISCC.exe'` valida su arranque y genera `windows-x64-setup.exe`. El instalador incluye las DLL redistribuibles MSVC junto a la app, accesos directos, desinstalación y la integración opcional con OBS para el usuario actual. `./scripts/prepare-release.ps1 -Version '0.2.0-preview' -Installer -SkipPortable` prepara instalador, APK y hashes (busca el compilador en `.tooling/innosetup`). Antes de publicar, ejecutá `./scripts/check-installer.ps1`: verifica instalación limpia, reinstalación, `StreamCam.exe --check-install` y arranque con PATH mínimo y AllSigned en una carpeta de prueba. Inspeccioná la app y ejecutá después el desinstalador de esa carpeta.

Ejecutar `python scripts/audit-public.py` (o `scripts/audit-public.ps1` en Windows) antes de generar la fuente pública. `scripts/prepare-release.ps1` ejecuta la auditoría PowerShell, valida los hashes y los archivos necesarios y genera un ZIP Windows completo más el APK de prueba. Los diagnósticos y las notas locales están ignorados. Las releases estables deben incluir licencia y avisos de todos los componentes, APK firmado por el mantenedor, binarios Windows, plugin y hashes SHA-256. Nunca empaquetar identidades, dispositivos confiables, claves de firma o logs. Publicar únicamente después de completar la matriz de aceptación física.
