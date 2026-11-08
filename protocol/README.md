# Protocolo PC ↔ teléfono

`pairing-v1.md` documenta la invitación, TLS pinning, autenticación del teléfono, reconexión y revocación. El QR solo autoriza la vinculación; la captura de medios se inicia después desde Android.

`control-v1.md` explica los mensajes JSON directos posteriores al pairing y sus direcciones. `control-v1.schema.json` describe esos objetos; `control-v2.schema.json` documenta negociación optativa de capacidades y el ACK de `audio.effects`.

Medios: Wi‑Fi usa pistas WebRTC H.264/Opus; USB/ADB lleva video H.264 y PCM por el WebSocket local autenticado. No se envían muestras de medios dentro de los objetos JSON. El control de medios V1 sigue siendo directo; después de `pair.accepted`, V2 negocia `audio.effects.ack` con `control.hello`/`control.ready`, usa `request_id` y conserva fallback con clientes antiguos.

Los schemas son contratos documentales: Android y Windows todavía validan cada mensaje en sus handlers; no comparten un validador generado desde JSON Schema.
