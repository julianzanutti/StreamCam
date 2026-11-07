# Instalación y uso

## Paquetes descargados

Descargá `StreamCam-preview.apk` para Android y el ZIP completo para Windows desde la sección **Releases**. El APK de esta compilación es de prueba. Conservá el ZIP completo: incluye `StreamCam.exe`, las DLL de Qt, PowerShell y ADB necesarios para funcionar. La identidad de esta PC se crea al conectar y se guarda en la carpeta local de datos de Windows; nunca viene dentro de la descarga. No muevas archivos individuales fuera de sus carpetas.

### Android

1. Transferí `StreamCam-preview.apk` al teléfono y abrilo. Android puede pedir habilitar **Instalar apps desconocidas** para la aplicación usada para abrirlo; autorizala solo para esta instalación.
2. Abrí StreamCam. No hace falta conceder cámara o micrófono al inicio: se solicitan al tocar Iniciar, según el modo seleccionado.
3. Si Android Play Protect muestra un aviso por instalación directa, verificá que el APK provenga de la release oficial de este repositorio antes de continuar.

### Windows y OBS

1. Extraé el ZIP entero a una carpeta donde tengas permiso de escritura, por ejemplo `Documentos\StreamCam`. No ejecutes los programas desde la vista previa del ZIP.
2. Si Windows no tiene el runtime de Visual C++, instalá `vc_redist.x64.exe`, incluido en el ZIP. Cerrá OBS Studio e iniciá `StreamCam.exe`.
3. Para instalar la fuente de OBS, cerrá OBS y ejecutá `obs-plugin\install.ps1` con PowerShell. Se instala solo para tu usuario, sin permisos de administrador. Si la directiva local bloquea el script, abre PowerShell en la carpeta extraída y ejecuta `powershell -ExecutionPolicy Bypass -File .\obs-plugin\install.ps1`. Revisá la ruta que informa y volvé a abrir OBS.
4. En OBS crea una escena o selecciona una existente. Agregá **Fuentes → + → StreamCam**. En el mezclador habilitá y subí el volumen de StreamCam.
5. Para Google Meet, Zoom u otra app, inicia **Iniciar cámara virtual** desde OBS y selecciona **OBS Virtual Camera** como cámara. StreamCam entrega audio a OBS, pero no instala un micrófono virtual de Windows; para la llamada usá el micrófono habitual de tu PC.

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
