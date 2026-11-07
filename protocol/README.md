# Protocolo PC ↔ teléfono

`pairing-v1.md` propone el contrato y estados del QR de invitación; todavía no implementa transporte, escáner ni pairing real.

`control-v1.schema.json` define mensajes JSON estrictos para el canal de control autenticado posterior al pairing: capacidades, inicio/parada, ajustes acotados, estado y errores. No permite propiedades arbitrarias. Video y SDP se mantienen fuera de estos mensajes; el video debe viajar por la pista WebRTC cifrada.

La app debe validar valores contra las capacidades reales anunciadas por la cámara seleccionada antes de enviar o aplicar comandos. El esquema no establece por sí solo el protocolo de pairing, el secreto QR ni la señalización WebRTC; esos flujos deben documentarse antes de abrir conectividad. Revisar la sección 7 de `../PLAN_CAMARA_STREAM.md`.

El esquema es un contrato preliminar v1, no hay todavía un serializador ni un validador en Android/Windows.
