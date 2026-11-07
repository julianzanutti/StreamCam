# Probá StreamCam paso a paso

Necesitás una PC con Windows de 64 bits, un teléfono con Android 10 o posterior y, para usarlo en OBS, OBS Studio. No necesitás programar ni instalar Android Studio, Qt o Visual Studio.

## 1. Descargar

Abrí [Releases de StreamCam](https://github.com/julianzanutti/StreamCam/releases) y entrá a la versión preliminar más reciente. En **Assets**, descargá:

- El archivo que termina en **windows-x64-setup.exe**, el instalador para la PC.
- El archivo que termina en **android-preview.apk**, para el teléfono.

No descargues **Source code**: es para desarrollar. El APK es de prueba, no una versión estable de Play Store.

## 2. Preparar los dos equipos

1. En Windows, abrí el archivo **windows-x64-setup.exe** y seguí el asistente. Instala la aplicación para tu usuario con sus dependencias, crea accesos directos y permite desinstalarla desde Aplicaciones de Windows. No necesitás instalar Visual C++ por separado. Si usás OBS, cerralo y marcá la opción de instalar la fuente StreamCam. Después abrí **StreamCam** desde el escritorio o el menú Inicio.
2. Pasá el APK al teléfono, abrilo e instalalo. Si Android pide permiso para instalar desde esa aplicación, habilitalo para esta instalación.
3. Abrí StreamCam en el teléfono.

## 3. Primera conexión por Wi-Fi

1. Conectá la PC y el teléfono a la misma red Wi-Fi. Evitá la red de invitados.
2. En StreamCam para Windows elegí **Wi-Fi**, seleccioná la interfaz de esa red y generá el QR. Si el firewall pide acceso, permitilo en tu red privada de confianza.
3. En Android tocá **Conectar por Wi-Fi**, escaneá el QR y confirmá la vinculación en ambos equipos. Si el QR vence, generá otro.
4. Elegí **Solo video**, calidad **HD / 720p**, tocá **Iniciar** y aceptá el permiso de cámara.
5. Comprobá que ves la imagen en Windows. Tocá **Detener** en Android para terminar.

Si preferís cable, seguí [la conexión por USB](docs/INSTALACION.md#conectar-por-usb): necesita un cable de datos, depuración USB y autorización del teléfono. ADB ya viene en el ZIP.

## 4. Usarlo en OBS

1. Si marcaste la opción **Instalar la fuente StreamCam para OBS** en el instalador, ya está preparada. Si no, cerrá OBS y volvé a ejecutar el instalador marcando esa opción. El plugin se compiló para OBS Studio **33.0.1 de 64 bits**. Como alternativa para el ZIP portable, abrí PowerShell en la carpeta extraída y ejecutá:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\obs-plugin\install.ps1
   ```

2. Abrí OBS y agregá **Fuentes → + → StreamCam**.
3. Para probar sonido, elegí **Audio + video** en Android, iniciá y aceptá el permiso de micrófono. Hablá y mirá el medidor de StreamCam en el mezclador de OBS.
4. Para usar la imagen en una videollamada, pulsá **Iniciar cámara virtual** en OBS y elegí **OBS Virtual Camera** en la llamada.

La cámara virtual de OBS lleva video. StreamCam entrega audio a OBS, pero **no instala un micrófono virtual de Windows**. Para esta primera prueba, usá el micrófono habitual de la PC en la videollamada.

## 5. Contarme cómo salió

Probá unos minutos, detené e iniciá de nuevo y, si podés, probá también por USB. Anotá:

- Versión descargada y versiones de Android, Windows y OBS.
- Conexión Wi-Fi o USB, modo y calidad elegidos.
- Qué esperabas, qué pasó y los pasos para repetirlo.

Podés registrar el problema en [Issues](https://github.com/julianzanutti/StreamCam/issues). No compartas QR activos, claves, direcciones IP, números de serie ni imágenes personales.

Si no conecta o falta imagen/sonido, consultá [Solución de problemas](docs/SOLUCION_DE_PROBLEMAS.md). La [guía completa](docs/INSTALACION.md) tiene más opciones.
