# StreamCam

Usá la cámara y el micrófono de tu teléfono Android en OBS Studio para Windows, por cable USB o por la red Wi‑Fi local.

## Descargar e instalar

> **¿Solo querés instalar StreamCam? No pulses el botón verde `Code`: descarga el código para desarrolladores.** Elegí qué necesitás:

[1. Instalador para Windows](https://github.com/julianzanutti/StreamCam/releases/download/v0.2.2-preview/StreamCam-0.2.2-preview-windows-x64-setup.exe) · [2. APK para Android](https://github.com/julianzanutti/StreamCam/releases/download/v0.2.2-preview/StreamCam-0.2.2-preview-android-preview.apk) · [3. Instrucciones paso a paso](EMPEZAR.md)

Es una **versión preliminar**. Android muestra una advertencia porque el APK es de prueba y no está publicado en Play Store. Windows también puede mostrar un aviso por falta de firma del editor. No desactives Play Protect ni Defender. [Ver los detalles de seguridad](SECURITY.md).

## Así se ve

### Conectar el teléfono por USB o Wi‑Fi

![StreamCam para Windows, pantalla de conexión USB](docs/screenshots/windows-conexion.jpg)

### Ajustar la voz antes de enviarla a OBS

![StreamCam para Windows, ganancia, compresor y puerta de ruido](docs/screenshots/windows-voz.jpg)

La pantalla de voz muestra el trabajo de V2 en desarrollo; esos controles todavía no están en los instaladores de la preview 0.2.2 que se descargan arriba.

## Empezar en tres pasos

1. Instalá StreamCam en Windows con el botón de arriba y abrila.
2. Instalá el APK en Android. Con **USB**, conectá el cable de datos y aceptá la vinculación que muestran ambos equipos; **no hay que escanear un QR**. Con **Wi‑Fi**, elegí esa conexión en Windows y escaneá el QR desde Android.
3. Elegí **Solo video**, **Solo audio** o **Audio + video** y tocá **Iniciar**. En OBS agregá **Fuentes → + → StreamCam**.

La [guía rápida con más detalles](EMPEZAR.md) incluye OBS y solución de errores. Para videollamadas, iniciá **OBS Virtual Camera** desde OBS y elegí esa cámara en la aplicación.

## Qué incluye

- Cámara frontal o trasera, perfiles SD, HD y Full HD.
- H.264 por hardware; audio USB y Wi‑Fi.
- En la V2 en desarrollo: controles Windows de ganancia, compresor y puerta de ruido; selección de entradas de micrófono y cancelación de eco en Android cuando el teléfono las admite. Aún falta validar estos cambios con micrófonos reales.
- Conexión local autenticada. No usa cuentas, no guarda audio o video y no transmite antes de la confirmación visible.

El procesamiento depende de las funciones que expone cada teléfono. La puerta de ruido no reemplaza un supresor de ruido; las pruebas con micrófono real, sincronía y sesiones largas siguen pendientes. Consultá el [estado de pruebas](docs/V1_STATUS.md).

## Para desarrolladores

El código fuente está disponible en el botón **Code** de GitHub o para descarga directa en [ZIP](https://github.com/julianzanutti/StreamCam/archive/refs/heads/main.zip). Para compilar, seguí [BUILDING.md](BUILDING.md). La licencia es [GPL-3.0-or-later](LICENSE).
