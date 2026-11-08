# Usar el audio de StreamCam como micrófono de Windows

StreamCam envía el micrófono Android a la fuente de audio/video **StreamCam** dentro de OBS. La cámara virtual de OBS comparte el video; no expone por sí misma un micrófono de Windows. Para seleccionar el audio del teléfono en Discord, Meet u otra aplicación, Windows necesita un endpoint de captura de audio virtual.

## Ruta recomendada con VB-CABLE

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
