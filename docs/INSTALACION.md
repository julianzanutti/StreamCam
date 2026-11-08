# Instalación y uso

## Paquetes descargados

**Opción recomendada para Windows:** descargá el archivo terminado en `windows-x64-setup.exe` desde [Releases](https://github.com/julianzanutti/StreamCam/releases), ejecutalo y seguí el asistente. Incluye las dependencias de Windows, ADB y PowerShell; no necesita SDKs ni instalar Visual C++ por separado. Instala para tu usuario, agrega accesos directos y un desinstalador. Con OBS cerrado, marcá la opción de instalar la fuente si usás OBS Studio 33.0.1 de 64 bits. Las actualizaciones conservan la identidad y los teléfonos vinculados. Abrí StreamCam desde Inicio o el escritorio. Para quitarlo, usá Aplicaciones de Windows.

### Archivos disponibles en GitHub

La release pública actual ofrece el instalador `StreamCam-0.3.14-preview-windows-x64-setup.exe`, el APK `StreamCam-0.3.14-preview-android-preview.apk`, `LICENSE` y `SHA256SUMS.txt`. No hay un ZIP portable en esta publicación: descargá el instalador de Windows y el APK desde **Assets**. ADB y las dependencias necesarias ya vienen con la instalación de Windows. La identidad de esta PC se crea al conectar y se guarda en los datos locales de Windows; nunca viene dentro de las descargas.

### Android

1. En **Assets** de la release, descargá el archivo que termina en `android-preview.apk`, transferilo al teléfono y abrilo. Android puede pedir habilitar **Instalar apps desconocidas** para la aplicación usada para abrirlo; autorizala solo para esta instalación.
2. Abrí StreamCam. No hace falta conceder cámara o micrófono al inicio: se solicitan al tocar Iniciar, según el modo seleccionado.
3. Si Android Play Protect muestra un aviso por instalación directa, verificá que el APK provenga de la release oficial de este repositorio antes de continuar.
4. Para comprobar una actualización disponible, abrí StreamCam y tocá **Verificar actualizaciones** al final de la pantalla. La descarga se valida con el SHA-256 publicado en GitHub; Android muestra una confirmación del sistema para instalarla.

### Windows y OBS

1. Descargá y ejecutá el archivo que termina en `windows-x64-setup.exe`. El instalador configura las dependencias, ADB y accesos directos; no hace falta bajar un ZIP ni instalar Visual C++ por separado.
2. Si usás OBS Studio **33.0.1 de 64 bits**, cerralo y marcá la opción **Instalar la fuente StreamCam para OBS** en el asistente. Después abrí StreamCam desde Inicio o el escritorio.
3. En OBS crea una escena o selecciona una existente. Agregá **Fuentes → + → StreamCam**. En el mezclador habilitá y subí el volumen de StreamCam.
4. Para Google Meet, Zoom u otra app, inicia **Iniciar cámara virtual** desde OBS y selecciona **OBS Virtual Camera** como cámara. Para pasar el audio de StreamCam a Discord u otra app como micrófono de Windows, seguí la [guía de micrófono virtual](AUDIO_VIRTUAL_WINDOWS.md); requiere instalar un controlador virtual aparte.

## Conectar por USB

1. Activá Opciones de desarrollador y **Depuración USB** en Android.
2. Conectá un cable USB con datos, desbloqueá el teléfono y acepta el diálogo **¿Permitir depuración USB?**. Marca “Permitir siempre desde esta computadora” únicamente en tu PC personal.
3. En StreamCam para Windows selecciona **USB · cable de datos** y pulsa **Conectar por USB**. Confirma la vinculación en Android la primera vez.
4. En el teléfono elige el modo. Para cámara selecciona lente, resolución y bitrate; para audio esas opciones quedan desactivadas. Pulsa **Iniciar** y acepta el permiso solicitado.

USB transporta H.264 y PCM de 48 kHz por el cable usando ADB reverse. No necesita que ambos equipos compartan Wi‑Fi ni que el teléfono comparta Internet.

## Conectar por Wi‑Fi

1. Conecta teléfono y PC a la misma red local. En Windows elige la interfaz correcta y genera el QR.
2. En Android toca **Conectar por Wi‑Fi**, escanea el QR y confirma la vinculación en ambos dispositivos. El QR vence a los dos minutos; si no conecta, genera uno nuevo.
3. Selecciona **Solo video**, **Solo audio** o **Audio + video**, acepta los permisos necesarios y pulsa **Iniciar**.

## Permisos y controles

- **Cámara**: solo para Solo video o Audio + video.
- **Micrófono**: solo para Solo audio o Audio + video. Android muestra su indicador de micrófono mientras captura.
- **Notificación**: permite ver y detener la transmisión desde la pantalla bloqueada. Si la rechazas, se puede detener desde StreamCam.
- Para detener, usa **Detener transmisión** en la app o **Detener** en la notificación. Para desconectar totalmente, usa **Desconectar** en Windows o Android.

StreamCam solo captura mientras la sesión fue iniciada explícitamente y la notificación está activa. No graba ni guarda audio/video.

## Compilar el código fuente

Lee [BUILDING.md](../BUILDING.md) para configurar JDK, Android SDK, Qt, WebRTC, OBS SDK y generar los paquetes localmente. El código no incluye binarios ni herramientas de terceros.
