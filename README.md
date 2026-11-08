# StreamCam

Usá la cámara y el micrófono de tu teléfono Android en OBS Studio para Windows, por cable USB o por la red Wi‑Fi local.

## Descargar e instalar

> **¿Solo querés instalar StreamCam? No pulses el botón verde `Code`: descarga el código para desarrolladores.** Elegí qué necesitás:

[⬇ Descargar instalador para Windows](https://github.com/julianzanutti/StreamCam/releases/download/v0.2.2-preview/StreamCam-0.2.2-preview-windows-x64-setup.exe) · [📱 Descargar APK para Android](https://github.com/julianzanutti/StreamCam/releases/download/v0.2.2-preview/StreamCam-0.2.2-preview-android-preview.apk) · [📖 Abrir instrucciones paso a paso](EMPEZAR.md) · [🖼 Ver capturas](#así-se-ve)

También podés entrar a [Descargas de StreamCam](https://github.com/julianzanutti/StreamCam/releases/tag/v0.2.2-preview) y elegir **Assets** para ver los archivos. Para instalar, no hace falta descargar el ZIP del código fuente.

Es una **versión preliminar**. Android muestra una advertencia porque el APK es de prueba y no está publicado en Play Store. Windows también puede mostrar un aviso por falta de firma del editor. No desactives Play Protect ni Defender. [Ver los detalles de seguridad](SECURITY.md).

En **Assets** de esa página, descargá el instalador terminado en `windows-x64-setup.exe` y el APK terminado en `android-preview.apk`. `LICENSE` y `SHA256SUMS.txt` son opcionales; el código fuente es para desarrollo.

## Así se ve

Las capturas muestran dónde conectar el teléfono y cómo se ajusta la voz:

### Conectar el teléfono por USB o Wi‑Fi

![StreamCam para Windows, pantalla de conexión USB](docs/screenshots/windows-conexion.jpg)

### Ajustar la voz antes de enviarla a OBS

![StreamCam para Windows, ganancia, compresor y puerta de ruido](docs/screenshots/windows-voz.jpg)

La pantalla de voz muestra el trabajo V2 local en desarrollo; esos controles todavía no están en los instaladores públicos 0.2.2 que se descargan arriba.

## Empezar en tres pasos

1. Instalá StreamCam en Windows con el botón de arriba y abrila.
2. Instalá el APK en Android. Con **USB**, conectá el cable de datos y aceptá la vinculación que muestran ambos equipos; **no hay que escanear un QR**. Con **Wi‑Fi**, elegí esa conexión en Windows y escaneá el QR desde Android.
3. Elegí **Solo video**, **Solo audio** o **Audio + video** y tocá **Iniciar**. En OBS agregá **Fuentes → + → StreamCam**.

La [guía rápida con más detalles](EMPEZAR.md) incluye OBS y solución de errores. Para videollamadas, iniciá **OBS Virtual Camera** desde OBS y elegí esa cámara en la aplicación.

## Qué incluye

- Cámara frontal o trasera, perfiles SD, HD y Full HD.
- H.264 por hardware; audio USB y Wi‑Fi.
- En la V2 en desarrollo: faders de voz y eco creativo con demora, repeticiones y mezcla en Windows; selección de entradas de micrófono, compresor, puerta de ruido y cancelación acústica de eco en Android cuando el teléfono las admite. Aún falta validar estos cambios con micrófonos reales.
- Conexión local autenticada. No usa cuentas, no guarda audio o video y no transmite antes de la confirmación visible.

El procesamiento depende de las funciones que expone cada teléfono. La puerta de ruido no reemplaza un supresor de ruido; las pruebas con micrófono real, sincronía y sesiones largas siguen pendientes. Consultá el [estado de pruebas](docs/V1_STATUS.md).

## Código fuente (opcional)

Si solo querés usar StreamCam, no hace falta entrar acá. El código está para quien quiera revisarlo o compilarlo: [ver archivos del proyecto](https://github.com/julianzanutti/StreamCam/tree/main) · [descargar ZIP del código](https://github.com/julianzanutti/StreamCam/archive/refs/heads/main.zip) · [guía para compilar](BUILDING.md) · [licencia GPL-3.0-or-later](LICENSE).
