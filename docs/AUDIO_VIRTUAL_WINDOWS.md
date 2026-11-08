# Usar el audio de StreamCam como micrófono de Windows

StreamCam envía el micrófono Android a la fuente de audio/video **StreamCam** dentro de OBS. La cámara virtual de OBS comparte el video; no expone por sí misma un micrófono de Windows. Para seleccionar el audio del teléfono en Discord, Meet u otra aplicación, Windows necesita un endpoint de captura de audio virtual.

## Salida directa de 0.3.16-preview

La versión 0.3.16-preview incorpora un selector **Salida directa de audio** en Windows. Con VB-CABLE instalado, elegí **CABLE Input** allí y **CABLE Output** como micrófono en la aplicación de llamadas. OBS no es necesario para esta ruta. La selección está desactivada al iniciar y no modifica los dispositivos predeterminados del sistema. Tras conectar o retirar un dispositivo, pulsá **Actualizar salidas de audio** y seleccioná el destino de nuevo.

Mute, ganancia y efectos se aplican antes de enviar el audio tanto a OBS como a la salida directa. La salida WASAPI limita el audio pendiente a 120 ms; el estado informa errores de apertura/escritura. No selecciones parlantes si eso produce retorno al micrófono.

Prueba local con VB-CABLE: el worker real entregó tonos estéreo separados a CABLE Output sin cliente OBS; mute bajó el pico a 1 unidad PCM16, con cero bloques descartados en esa ejecución. La prueba sintética de 60 segundos pasó sin descartes, con mute y recuperación. La ruta con audio real del Moto G32 pasó 10 segundos sin descartes y con señal capturada. Las pruebas no acreditan sesiones de horas ni Discord, que no está instalado en el equipo de prueba. Si el productor se retrasa más que el presupuesto de cola, puede haber descartes visibles en el contador.

## Ruta para la versión publicada 0.3.15 con VB-CABLE y OBS

VB-CABLE es un controlador virtual independiente. Windows lo presenta como **CABLE Input** (reproducción) y **CABLE Output** (grabación). Instálalo solo desde el [sitio oficial de VB-Audio](https://vb-audio.com/Cable/). La instalación de controladores requiere permisos de administrador y puede pedir reiniciar Windows; StreamCam no lo instala ni lo distribuye.

1. Instala VB-CABLE desde su sitio oficial y reinicia Windows si lo solicita.
2. Abre OBS y agrega la fuente **StreamCam**. En Android selecciona **Solo audio** o **Audio + video**, inicia y concede el permiso de micrófono.
3. En OBS, abre **Archivo → Configuración → Audio → Avanzado** y elige **CABLE Input (VB-Audio Virtual Cable)** como dispositivo de monitorización.
4. En OBS, abre **Editar → Propiedades de audio avanzadas**. En la fila **StreamCam**, elige **Monitorizar y salida** si quieres que el audio llegue tanto al directo/grabación de OBS como a otras aplicaciones. Elige **Monitorizar solamente (silenciar salida)** si lo usarás solo como entrada de Discord.
5. En Discord, abre **Ajustes → Voz y vídeo → Dispositivo de entrada** y selecciona **CABLE Output (VB-Audio Virtual Cable)**. En otras aplicaciones, elige el mismo dispositivo de grabación.

Para un directo hecho en OBS no hace falta VB-CABLE: la fuente StreamCam ya aporta audio al mezclador y a la salida de OBS. Usa el cable virtual cuando otra aplicación necesite recibir la voz como entrada de Windows.

## Evitar eco y retorno

- No selecciones **CABLE Output** como dispositivo de monitorización de OBS; es la salida de grabación del cable.
- Si Discord devuelve el audio de participantes a una fuente que OBS captura, configura las salidas de Discord y las fuentes de escritorio para evitar un bucle.
- Comprueba el medidor de la fuente StreamCam en OBS y luego el indicador de entrada de Discord antes de transmitir.

## Controles de voz de StreamCam

En Windows, **Controles de audio** permite ajustar mute, ganancia de entrada/salida, compresor, puerta de ruido y eco creativo antes de entregar el audio a OBS. La cancelación de eco es un ajuste de captura Android y depende del teléfono. La puerta no es supresión espectral de ruido.
