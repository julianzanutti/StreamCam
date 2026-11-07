# Audio y video por USB con depuración ADB

Este modo no necesita tethering ni compartir Internet. Windows crea `adb reverse tcp:47831 tcp:47831`; Android conecta a `wss://127.0.0.1:47831/pair`. Se conserva la validación del certificado por la identidad pública entregada mediante ADB, la prueba de identidad del teléfono y la confirmación de ambos lados.

1. Conectar un único Android por USB y autorizar la depuración en el teléfono.
2. En Windows elegir **USB · depuración ADB** y **Conectar por USB**.
3. StreamCam se abre en Android por ADB. Confirmar la vinculación en ambos equipos la primera vez; USB no usa QR.
4. Elegir **Solo video**, **Solo audio** o **Audio + video**. Seleccionar cámara, calidad y bitrate cuando el modo incluya video. Pulsar **Iniciar** y aceptar los permisos necesarios.
5. La conexión Windows indica **USB · ADB**. El video usa Camera2 + MediaCodec H.264; el micrófono usa PCM mono de 48 kHz en bloques de 20 ms. Ambos viajan como mensajes binarios en el WebSocket TLS por el cable; no utilizan ICE ni Wi‑Fi.

La cola de envío está limitada; si se satura, se detiene la sesión y se pide reconectar. La cámara continúa mediante un servicio en primer plano y una notificación para detenerla. No se reinicia automáticamente después de cerrar el proceso.

Video: cabecera little endian de 20 bytes (magic `SCH1`, ancho uint32, alto uint32, timestamp microsegundos uint64), seguida por H.264 Annex B. Audio: cabecera de 32 bytes (magic `SCA1`, versión 1, frecuencia, canales, muestras, timestamp microsegundos y reservado cero), seguida por PCM16 little endian. Solo se aceptan después del emparejamiento y en el transporte USB.

Estado de validación: autenticación USB sin QR aprobada (TLS, desafío nuevo firmado, reconexión y revocación). La cámara USB se probó físicamente en Moto G32 a 1280×720 y alrededor de 30 fps. El audio USB recién integrado requiere validación en un teléfono y una fuente OBS reales.
