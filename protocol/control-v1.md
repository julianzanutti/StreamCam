# Control de medios v1 (wire actual)

Los mensajes de esta fase viajan como objetos JSON UTF‑8 directos por el WebSocket ya autenticado. No llevan `protocol_version`, secuencia ni sobre `payload`; no agregar esos campos solo en un extremo. Los tamaños efectivos de WebSocket los limitan los receptores Windows. El detalle estructural está en `control-v1.schema.json`.

| Mensaje | Dirección | Transporte/efecto |
|---|---|---|
| `rtc.offer` | Android → Windows | Wi‑Fi; SDP de oferta del emisor de Android. |
| `rtc.answer` | Windows → Android | Wi‑Fi; SDP del receptor Windows. |
| `rtc.ice` | Ambos sentidos | Wi‑Fi; candidato ICE con `mid`, `mline` y `candidate`. |
| `media.stop` | Android → Windows | Detiene la sesión de medios y reinicia el receptor sin quitar el pairing. |
| `video.stop` | Android → Windows | Alias anterior de parada que Windows todavía acepta. |
| `audio.effects` | Windows → Android | Solicita guardar la opción AEC para la próxima captura; no cambia los efectos de una sesión ya iniciada y no incluye audio. |

En USB no se negocia WebRTC; sus cuadros de video y paquetes SCA1/PCM se transportan como datos binarios por el socket emparejado. `audio.effects` se usa en ambas rutas para guardar la preferencia Android.

Las extensiones optativas de control v2 están descritas en `control-v2.schema.json`. Después de `pair.accepted`, cualquiera de los dos extremos puede iniciar `control.hello`; el receptor devuelve `control.ready` con la intersección de capacidades. Si ambos inician a la vez, cada lado procesa de manera independiente la respuesta a su propio saludo y la capacidad negociada debe coincidir en ambos sentidos. El pairing conserva el mensaje v1 sin campos extra, así los clientes viejos no reciben extensiones que todavía no negociaron. Windows solo envía `audio.effects` con `version: 2` y `request_id` cuando ambos acordaron `audio.effects.ack`; Android guarda la preferencia para la próxima captura y devuelve `audio.effects.ack`. Si no hay capacidad compartida, se mantiene el canal v1 sin comandos extendidos. Los mensajes RTC y `media.stop` mantienen el wire directo v1.
