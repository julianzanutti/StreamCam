# IPC local para OBS

StreamCam usa dos named pipes locales, uno por cada medio. QLocalServer limita acceso al usuario Windows actual. Los pipes transportan medios transitorios y no contienen credenciales de emparejamiento ni comandos.

- [Video v1](frame-v1.md): BGRA empaquetado con cabecera fija de 40 bytes.
- Audio v1: `\\.\pipe\StreamCamAudio-v1`; bloques PCM16 little endian a 48 kHz, mono/estéreo, máximo 960 muestras por bloque. La cabecera de 32 bytes incluye magic `SCA1`, versión, frecuencia, canales, cantidad de muestras, timestamp de captura y reservado cero. El receptor valida todos los campos antes de usar el búfer.

OBS lee en hilos cancelables y los une al destruir la fuente. El publicador descarta paquetes cuando un cliente mantiene una cola de salida grande, para mantener latencia y memoria acotadas. No se guarda audio ni video a disco.
