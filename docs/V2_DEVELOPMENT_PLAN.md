# StreamCam V2 · Plan de desarrollo

Fecha: 2026-11-08. Base auditada: `e1a646d`, revisión estática del repositorio y documentación V1. Este documento combina el plan y el estado local de implementación; las pruebas físicas siguen pendientes cuando se indican.

## Estado local de implementación (2026-11-08)

La release 0.3.14-preview integra Windows y Android. Android versionCode 18 incorpora un botón manual para comprobar GitHub Releases, descargar solo APK con digest SHA-256, validar paquete/versionCode y abrir el instalador Android para confirmación del usuario. `assembleDebug` y `lintDebug` pasan. El APK fue firmado con el mismo certificado que la release 0.2.2 para mantener continuidad; la clave privada queda local y no se publica. Windows 0.3.14-preview y OBS compilan; el instalador integrado pasó `--check-install`. El APK es debug y el instalador no tiene Authenticode; sigue siendo preview. El flujo de actualización aún necesita prueba física en teléfono. El smoke elevado de instalación y las 27 pruebas unitarias corresponden al paquete 0.3.13 anterior y no deben atribuirse a la release actual.

En 0.3.13, la política de selección de entradas Android distingue rutas por ID runtime, limpia automáticamente una preferencia retirada o ambigua y explica el fallback a Automático. Android build/lint y 27 pruebas unitarias pasaron. El AVD Android 35 confirmó pairing USB con código correcto y rechazo del incorrecto; el emulador no informa cápsulas integradas. Aún faltan Android físico, variedad de micrófonos/OEM, pairing real Android↔Windows y sesiones largas.

El árbol de trabajo ya contiene una primera implementación de SC-001 y SC-003, además de ajustes de voz para Windows y selección de entradas de Android. SC-001: se corrigió el serializador USB Android para emitir exactamente los 32 bytes SCA1 que valida Windows; el parser se extrajo a una rutina comprobable y el probe golden rechaza truncamiento y reserved no cero. Los probes C++ actuales pasan, incluyendo SC-013 y la cola USB H.264; `assembleDebug`/`lintDebug` Android pasan también tras añadir el inventario informativo y el reporte de cápsulas activas USB; falta validar el micrófono físico en teléfono. SC-005 ahora detecta `DISCONNECTED`/`FAILED` en Wi-Fi y hace hasta dos intentos de ICE restart, espera corta para cortes transitorios y timeout de respuesta; al agotar intentos comunica una acción clara, y cerrar la sesión cancela los temporizadores. En SC-006, el catálogo Android filtra perfiles por ruta, tamaño, FPS y bitrate, y enumera/persiste un `cameraId` Camera2 explícito que se aplica igual a USB y Wi-Fi; la preferencia anterior Trasera/Frontal se migra al ID correspondiente. La sesión muestra el tamaño/FPS observado en los cuadros capturados, junto con cámara y bitrate de salida, pero falta validar el formato abierto en distintos OEM y confirmar que las estadísticas coinciden bajo carga. Para SC-007, el encoder USB prueba los encoders H.264 hardware compatibles en orden y reintenta configuraciones sin opciones CBR/perfil si el fabricante las rechaza; la selección real necesita QA física. Para SC-008, el cierre de medios corre en un executor serial de fondo y bloquea nuevos inicios hasta terminar. SC-013 ahora limita 64 identidades activas y permite conservar identidades revocadas aparte de ese cupo, sin permitir su reautorización; un probe recorre 64 altas, 40 revocaciones, 40 reemplazos y el límite restante. SC-016: el decode H.264 USB sale del hilo de recepción/UI a un worker dedicado con cola máxima de 3 cuadros/8 MiB, resincronización al siguiente IDR tras saturación/error y dimensiones de decoder tomadas del encabezado; falta una prueba prolongada con teléfono USB real. En SC-017 se agregó la solicitud en tiempo de ejecución de `BLUETOOTH_CONNECT` cuando se selecciona una entrada SCO/BLE o se inicia con esa preferencia guardada; si se deniega se revierte a Automático. En Android 12+, StreamCam solicita `MODE_IN_COMMUNICATION` y selecciona una ruta desde `getAvailableCommunicationDevices`/`setCommunicationDevice`, emparejando la salida con el micrófono elegido por dirección Bluetooth o nombre; si hay varias rutas ambiguas se niega a elegir arbitrariamente. Restaura el modo previo y limpia la ruta después de cerrar USB/Wi-Fi. `BLUETOOTH_CONNECT` se pide al seleccionar o iniciar con Bluetooth guardado; ante denegación se vuelve a Automático. USB y entradas Android separadas siguen usando `AudioRecord.setPreferredDevice`. Build, lint y 27 pruebas unitarias pasan; cuatro validan la selección de ruta Bluetooth inequívoca y cuatro la recuperación de entradas ausentes o ambiguas. El APK 0.3.13 se instaló en un AVD Android 35 limpio; ese entorno no valida permisos ni captura física, y faltan micrófonos físicos SCO/BLE, USB, varios OEM y confirmar que cada fabricante expone y aplica esas rutas como se espera. No equivale a completar M0/M2: reconexión sobre una red real, selección de formatos probada en distintos OEM, 100 ciclos de inicio/cierre, ruta física de audio y sesión de OBS de varias horas aún requieren pruebas reales. En esta iteración se sustituyó el corte arbitrario de una hora por un mapeo monotónico de época en OBS, que detecta duplicados, regresiones, saltos y overflow; el probe cubre más de una hora. El catálogo Android usa el ID de entrada en ejecución para distinguir micrófonos con nombres duplicados, vuelve a automático si el ID persistido ya no existe y refresca la lista al agregar o retirar una entrada. La pantalla de Android consulta además `AudioManager.microphones` y muestra las cápsulas integradas y grupos no desconocidos que informa el fabricante; en captura USB se consulta `AudioRecord.activeMicrophones` y se informa cuántas cápsulas Android marca activas. Son lecturas informativas, no un selector de cápsulas: el arreglo integrado lo enruta el HAL.

El probe local `StreamCamVoiceProbe` comprueba ganancia, compresión, puerta y dos repeticiones de eco creativo en paquetes PCM sintéticos. Aún no mide latencia, calidad perceptual ni comportamiento específico de un OEM. Los paquetes de trabajo previos se conservan en `dist/StreamCam-v2-usb-echo-local` y `dist/v2-usb-echo-local`; la recuperación Wi-Fi está en `dist/StreamCam-v2-ice-recovery-final` y `dist/v2-ice-recovery-final`; el selector de perfiles está en `dist/StreamCam-v2-camera-profiles` y `dist/v2-camera-profiles`; el paquete con fallback de encoder Android está en `dist/StreamCam-v2-android-encoder-fallback` y `dist/v2-android-encoder-fallback`; la iteración integrada con worker OBS está en `dist/StreamCam-v2-obs-frame-worker` y `dist/v2-obs-frame-worker`; el cierre Android asíncrono está en `dist/StreamCam-v2-android-async-close` y `dist/v2-android-async-close`; la revisión con guard de conexión está en `dist/StreamCam-v2-async-close-guard` y `dist/v2-async-close-guard`; los workers independientes de audio/video OBS están en `dist/StreamCam-v2-audio-output-worker` y `dist/v2-audio-output-worker`; la selección Camera2 explícita está en `dist/StreamCam-v2-camera-id` y `dist/v2-camera-id`; la iteración que reporta formato observado está en `dist/StreamCam-v2-camera-id-observed-format` y `dist/v2-camera-id-observed-format`; el contrato SCA1 USB y el cupo de confianza por activos están en `dist/StreamCam-v2-trust-quota` y `dist/v2-trust-quota`; la negociación compatible de controles V2 está empaquetada en `dist/StreamCam-v2-control-negotiation-hardened` y `dist/v2-control-negotiation-hardened`. Cada artefacto histórico tiene la evidencia de su propia fecha; el paquete actual 0.3.11 pasó smoke aislado. El paquete vigente pasa `--check-install` y sus hashes se verifican. El APK empaquetado es debug. Las descargas públicas siguen siendo 0.2.2-preview; no presentar estos binarios como V2 estable: falta QA física, firma de producción, compatibilidad y revisión de release.

La barra de estado muestra backlog de audio (0–100 paquetes) y descartes acumulados de cola/envío a clientes locales lentos. Un worker de audio drena hasta cinco paquetes cada 10 ms, procesa los filtros y publica a OBS fuera del hilo UI; al detener o cambiar de teléfono se limpia la cola y se reinicia la memoria del eco creativo. El medidor solo se mantiene activo si al menos un cliente OBS recibió el paquete; sin cliente, durante descartes o tras desconexión cae a silencio y vuelve al reconectar. El probe verifica entrega mientras UI bloqueada, caída del medidor sin consumidor y recuperación al reconectar. `VoiceProcessor` ahora mantiene una línea de demora por canal estéreo y el probe comprueba que los impulsos izquierda/derecha no se mezclen. `StreamCamObsTimestampProbe` valida continuidad por más de una hora, duplicados, regresión y salto discontinuo. `StreamCamAudioQueueProbe` valida la cola acotada y el drenado parcial. `StreamCamAudioOutputWorkerProbe` recibe paquetes mientras el hilo UI queda bloqueado 300 ms. La DLL OBS compila contra la interfaz exportada por el OBS instalado.

SC-004 tiene ahora un worker Qt independiente que lee la ranura del último frame y publica al named pipe cada frame nuevo, sin depender del timer ni del tamaño de la vista previa. `StreamCamFrameOutputWorkerProbe` bloquea el hilo principal 300 ms mientras genera cuadros y confirma recepción múltiple; pasa en CTest. La prueba valida el desacople y el contrato local básico, pero falta verificar continuidad del plugin OBS real con preview minimizada/apagada y duración prolongada.

Ampliación: la [revisión de seguridad V2](SECURITY_REVIEW_V2.md) incorpora hallazgos/controles, verificación de firmas locales y el reporte de avisos al instalar. SEC-001 y SEC-003 tienen corrección, build Windows y regresión WSS local aprobada: el consentimiento viejo no autorizó a B y un socket sobrante se cerró al aceptar B. SEC-002 está mitigado en el flujo local: Android exige introducir el código OOB exacto de seis dígitos, los errores no habilitan la conexión y el reto vence a los 60 segundos; pasó regresión de UI/AuthProbe en AVD Android 35. Queda pendiente QA física y adversarial en dispositivos reales. SEC-009 ahora limita el fallback de PowerShell/ADB del sistema a builds Debug; Release requiere las copias del paquete y se recompiló. Firma, procedencia de dependencias, memoria de claves y verificación del paquete son puertas de publicación estable, no garantías de ausencia de vulnerabilidades.

## 1. Objetivo y alcance

Convertir la preview actual en una herramienta confiable para sesiones largas de cámara y micrófono Android → Windows → OBS, con recuperación de fallos, ajustes comprensibles y compatibilidad publicada con evidencia.

Orden de inversión: integridad de audio/video → estabilidad y diagnóstico → rendimiento medido → compatibilidad → interfaz y controles avanzados. El rediseño se puede prototipar en paralelo; no debe bloquear los hotfixes.

La base incluye Android 10+, Windows x64, USB/ADB, Wi-Fi local, H.264, audio PCM USB y WebRTC, y fuente OBS. Mantener operación local sin cuentas, captura iniciada por el usuario y revocación de confianza. La cámara virtual sigue dependiendo de OBS. Agregar una cámara virtual propia, iOS, macOS o Linux requiere proyectos de viabilidad separados.

## 2. Hallazgos y acciones inmediatas

P0: bloquea una función central o la publicación estable. P1: afecta continuidad, integridad o recuperación. P2: mejora de producto. P3: expansión opcional. Las prioridades son propuestas de este plan.

| ID | Prioridad y evidencia | Acción | Criterio de aceptación |
|---|---|---|---|
| SC-001 | P0, defecto comprobable. Android escribía 36 bytes de cabecera; Windows y el contrato SCA1 esperan 32. | Corregido: serializador Android y parser explícito comparten offsets/campos; Windows rechaza paquete truncado o reservado no cero. | Probe golden C++ pasa (cabecera, LE timestamp, PCM y malformed); Android compila/linta. Micrófono físico USB y salida OBS siguen pendientes. |
| SC-002 | P1, condición comprobable. `obs-plugin/streamcam-source.cpp:66,95` compara tiempo acumulado desde origen con una hora y corta el pipe. | Validar discontinuidades entre muestras y definir reinicio de época; no rechazar una sesión válida por duración total. | Reloj simulado cruza 1 h y 8 h sin reconexión; prueba física de al menos 2 h. |
| SC-003 | P1, pérdida posible por diseño en la base auditada. `LatestAudio` conservaba un único bloque y el drenado ocurría en UI. | Estado parcial: cola acotada de 100 paquetes, drenado en worker hasta cinco cada 10 ms, filtros, mute persistente, medidor pico de salida y publicación OBS fuera de UI; reset del eco al cortar una sesión. Probes de cola, worker, backlog visible y mute pasan. | UI bloqueada 300 ms conserva salida sintética; el medidor refleja entrega a cliente y se recupera al reconectar (probe Windows 11/11); faltan congestión sostenida y micrófono real hacia OBS con medición de latencia. |
| SC-004 | P1, acoplamiento comprobado en la base auditada. `main.cpp` publicaba OBS desde el timer de preview. | Estado parcial: `FrameOutputWorker` drena y publica desde un QThread; la preview queda en UI. Probe bloquea UI 300 ms y recibe varios frames. OBS 33.0.1 portable carga el plugin y, con la variante QA de pipes aislados, el source recibe audio PCM y cuadros sintéticos por el protocolo real, confirma ambos primeros paquetes y entrega ambos tipos a libobs. | Pendiente continuidad de una fuente Android real, ventana minimizada/redimensionada, preview apagada y sesión larga; el loopback sintético no valida el transporte del teléfono ni la captura física. |
| SC-005 | P1, hallazgo inicial: el observador solo mostraba estado ICE. | Estado parcial: detecta `DISCONNECTED`/`FAILED`, espera cortes breves, hace hasta dos ICE restart con timeout y cancela callbacks al cerrar. | Build/lint Android pasan; falta corte de red real, retorno del video y confirmación de que detener siempre gana al reintento. |
| SC-006 | P1, limitación de la base auditada: perfiles probados a 2 Mbps y filtrados por ambos transports. | Estado parcial: perfila por transporte, formato y rango de bitrate; enumera y guarda `cameraId`, lo aplica en rutas USB/Wi-Fi y migra la preferencia vieja; muestra tamaño/FPS observados en cuadros capturados. | Un perfil Wi-Fi ya no queda excluido por USB; selección y métricas bajo carga requieren varios OEM. |
| SC-007 | P1, riesgo inicial: se elegía el primer encoder H.264 compatible y faltaban callbacks de cámara. | Estado parcial: encoder USB recorre candidatos hardware y prueba configuraciones sin opciones CBR/perfil si el fabricante las rechaza. | Build/lint pasan; fallback real ante codec/cámara defectuoso requiere teléfono y matriz OEM. |
| SC-008 | P1, riesgo inicial de bloqueo en cierres Android. | Estado parcial: `CameraSession` serializa liberación en un executor de fondo, mantiene controles deshabilitados durante el cierre y evita abrir otra captura antes de terminar. | Build/lint pasan; faltan 100 ciclos físicos sin ANR, crecimiento sostenido de recursos ni cámara retenida. |
| SC-009 | P1, brecha de validación. `docs/V1_STATUS.md` mantiene pendientes audio físico, A/V simultáneo, bloqueo de pantalla y sesiones largas. | Completar QA real antes de anunciar estabilidad. | Evidencia por dispositivo, versión, transporte y modo; fallos abiertos asociados a tickets. |
| SC-010 | P2, documentación contradictoria en la base auditada. | Actualizados README de descarga, inicio, instalación, mapa, estado y versión coherente de artefactos locales; se señala qué controles de voz pertenecen a V2 y cuáles siguen pendientes de validación real. | Un usuario distingue los Assets públicos 0.2.2-preview del paquete local V2. Smoke 0.3.13 elevado aislado pasa instalación, reinstalación, `--check-install`, PATH mínimo/AllSigned y desinstalación; aún falta QA física y firma de producción. |
| SC-011 | P1. La documentación anterior exigía sobre con versión/secuencia/payload, mientras Android y Windows intercambian objetos directos; `audio.effects` no se enroutaba al callback Android. | Control v2 optativo tras pairing: cualquiera de los peers puede iniciar `control.hello`; cada `control.ready` confirma la intersección. Android conserva por separado ambas respuestas para tolerar inicios simultáneos y rechaza intersecciones inconsistentes. Se conserva pairing v1 sin metadatos; faders solo cambian procesamiento local. AEC crea una solicitud; una activa, un retry con mismo ID, cambios posteriores coalescidos, ACK duplicado ignorado e ID desconocido cierra. Capabilities valida tipo, unicidad, tamaño máximo 16 y longitud de 1–64 en ambos extremos. | 9 pruebas Kotlin, probe C++ y 11/11 tests aislados (incluido TLS loopback) cubren handshake local/remoto/simultáneo, intersecciones, duplicados, límites, ACK y retry. `check-stale-consent.py` pasa WSS. Sigue pendiente la integración Android↔Windows real y una pérdida de ACK en una red física. |
| SC-012 | P1, brecha de release. La auditoría previa no compilaba los ejecutables de la app. | Workflow actualizado: Android corre `assembleDebug`, `lintDebug` y `testDebugUnitTest`; Windows descarga el WebRTC fijado, verifica el SHA-256, compila StreamCamDesktop y sus probes, ejecuta CTest y conserva la suite aislada; también obtiene las cabeceras OBS 33.0.1 y el ZIP portable con SHA-256 fijado, genera una import library desde `obs.dll`, compila el plugin y corre sus pruebas. La auditoría pública es independiente. Firma de producción, promoción gated y migración desde debug requieren diseño. | Android build/lint y 27 pruebas unitarias pasan localmente; CTest escritorio 10/10, OBS 2/2 y probes aislados 11/11 pasan localmente. El generador de import library local produce 1.781 exports y el plugin compila contra ella. Falta confirmar los jobs en GitHub Actions después de publicar; el workflow aún no firma ni publica instaladores. |
| SC-013 | P2, límite comprobable. El máximo de 64 contaba también identidades revocadas. | Corregido: el cupo cuenta solo identidades activas; las revocadas quedan como tombstones y no se pueden volver a autorizar con el mismo ID. | Probe CTest pasa 64 activas, 40 revocaciones, 40 altas nuevas, rechazo del ID revocado y del exceso activo. Persistencia cercana al límite de 64 KB aún debe vigilarse en uso real prolongado. |
| SC-014 | P2, limitación comprobable. `desktop/app/PairingServer.cpp` restringe IPv4 y usa puerto fijo 47831. | Mantiene selección explícita de interfaz LAN, puerto documentado y límite de conexiones pendientes; un fallo de `listen` llega al manejo de inicio y muestra error con instrucción de volver a conectar. Evaluar selección dinámica de puerto solo con diseño de pairing compatible. | Conflicto devuelve error de apertura del endpoint y no degrada TLS ni el consentimiento; validar mensaje con puerto ocupado real. Pinning y autorización siguen siendo obligatorios. |
| SC-015 | P2, riesgo temporal. Video y audio podían crear orígenes OBS separados; USB perdía su timestamp de captura al decodificar H.264, y RTP 32-bit no sirve como reloj de sesión largo. | El plugin OBS usa un mapper monotónico compartido que conserva la relación capturada entre pistas y valida el orden por pista. `AvSessionClock` espera los dos pipes; al reconectar ambos inicia una época nueva para aceptar relojes Android reiniciados sin perder el offset A/V, y no publica mientras falta una pista. Video Wi‑Fi usa el timestamp monotónico de WebRTC; USB lleva timestamp Android exacto por `EncodedImage::PresentationTimestamp` y por el tiempo de muestra MF. | Pruebas sintéticas verifican offset A/V de 300 ms, frames tardíos, orden por pista, ocho horas simuladas, overflow y reconexión con reloj Android menor; mapper OBS 2/2, probes de escritorio 10/10, suite aislada 11/11 incluido TLS y build de app/plugin pasan. Falta medir sincronía física con teléfonos Android reales (incluyendo el origen de timestamp de Camera2 por OEM), latencia y deriva en OBS bajo sesiones largas. |
| SC-016 | P2, bloqueo comprobable por diseño. `desktop/media/WebRtcReceiver.cpp` ejecutaba decode USB mediante `BlockingCall` desde recepción en UI. | Implementación parcial: decoder H.264 USB dedicado, cola 3 cuadros/8 MiB, resincronización IDR tras overflow/error y tamaño de salida configurado desde el frame USB. | Probe cubre overflow/recovery de cola, pero faltan inyección de decoder lento, continuidad de audio bajo carga y prueba física con video USB real. Paquete local: `dist/StreamCam-v2-usb-decode-async` / `dist/v2-usb-decode-async`. |
| SC-017 | P2, compatibilidad de micrófonos Android: el número de cápsulas y las rutas expuestas cambian por modelo/OEM. | Implementación parcial: enumera entradas Android por ID runtime, refresca con callbacks, muestra cápsulas/grupos del inventario `AudioManager` y cápsulas activas `AudioRecord`; USB agrupa lecturas cortas de `AudioRecord` a bloques PCM16 de 20 ms, prueba tasas de 48/44.1/32/24/16/12/8 kHz, remuestrea a 48 kHz y prefiere el timestamp monotónico de `AudioRecord`. Seis pruebas PCM cubren conversión nativa y tasas 8–44.1 kHz, lectura USB partida en bloques de 20 ms, límites de byte impar y entradas inválidas. En un AVD Android 35 limpio, **Solo audio** permite elegir/actualizar la entrada antes de vincular la PC; la pantalla informa que el emulador no expone cápsulas integradas y conserva Automático. El HAL sigue eligiendo/mezclando el arreglo integrado en Automático. | 6 pruebas unitarias de remuestreo PCM16 y builds/lint pasan; UI de Android 35 inicia tras instalación limpia y expone selector de entrada en Solo audio; comprobar tasas reales, selección USB/Bluetooth, timestamp A/V y arreglos de dos o más micrófonos en varios OEM. No afirmar selección individual de cápsulas integradas. |

Los fallos SC-002/003/004 y los riesgos Android requieren reproducción dirigida para cuantificar impacto. No se midieron aquí latencia, consumo ni comportamiento físico nuevo.

## 3. Arquitectura objetivo

Separar responsabilidades incrementalmente, conservando pruebas de protocolo y autenticación:

1. **Sesión:** estados Desconectado → Preparando → Autorizando → Vinculado → Transmitiendo → Recuperando/Error. Transiciones tipadas, id de generación y cancelación; callbacks antiguos no modifican sesiones nuevas.
2. **Captura Android:** interfaces para cámara, micrófono, capacidades y políticas térmicas. El servicio posee recursos; Activity representa estado y envía acciones.
3. **Transporte:** USB y Wi-Fi exponen control y medios sin obligar a compartir restricciones de hardware. Reintentos limitados y negociación de capacidades/versiones.
4. **Media Windows:** decode y publicación independientes de UI. Video puede descartar cuadros viejos; audio requiere continuidad y una política explícita de presión.
5. **Reloj A/V:** documentar dominio temporal por transporte, conversión a reloj receptor, reinicio de época, deriva y discontinuidades. Audio/video deben mantener relación temporal común; dos orígenes independientes no garantizan sincronía.
6. **IPC/OBS:** contratos binarios versionados, límites, timeouts y compatibilidad. Un consumidor lento no bloquea captura ni otro consumidor.
7. **Presentación:** componentes de interfaz separados del coordinador de sesión; `main.cpp` pierde progresivamente lógica de negocio, IO y procesamiento.
8. **Configuración:** esquema versionado, validación, migración, defaults seguros, alcance por dispositivo y opción restaurar. Nunca exportar claves de confianza.

Registrar ADR para máquina de estados, reloj A/V, colas/backpressure, negociación de protocolo, configuración y estrategia GPU. No reescribir toda la aplicación de una vez.

## 4. Rendimiento y optimización

Primero capturar baseline repetible: mismo dispositivo, resolución, FPS, bitrate, conexión, escena, temperatura y duración. Medir latencia extremo a extremo con cámara/reloj externo; no inferirla solo de RTT ni comparar relojes sin sincronización.

| Área | Trabajo | Métrica |
|---|---|---|
| Video | Desacoplar preview; evitar escalados si no cambia cuadro/tamaño; reducir preview en segundo plano. | Tiempo UI p95, FPS de salida, cuadros descartados. |
| Memoria | Auditar copias BGRA, asignaciones por cuadro y colas; reutilizar buffers con ownership explícito. | Copias/cuadro, asignaciones/s, RAM en sesión de 2/8 h. |
| Decode/GPU | Medir Media Foundation, transferencias GPU→CPU e IPC; investigar NV12/texturas compartidas como experimento posterior. | CPU/GPU y latencia frente a baseline; no afirmar zero-copy sin evidencia. |
| Audio | Cola temporal, clock común, jitter buffer acotado y deriva; resampling solo cuando sea necesario. | Underruns, overflows, discontinuidades, offset A/V. |
| Android | Cachear capacidades con invalidación, liberar efectos/cámara/codec, limitar trabajo UI y sondeo. | Temperatura, batería/h, throttling y tiempo de cierre. |
| Red | Bitrate adaptativo con histéresis; no cambiar resolución agresivamente; detectar Wi-Fi degradado. | Pérdida, RTT, bitrate real, eventos de adaptación y recuperación. |

Metas iniciales, sujetas a baseline al cerrar M1: latencia p95 ≤150 ms USB y ≤250 ms Wi-Fi en entorno de referencia; offset A/V absoluto p95 ≤80 ms; al menos 29 FPS medios para perfil 720p30 durante 30 min; cero pérdidas locales de audio en prueba sin congestión; sin crecimiento sostenido de memoria tras calentamiento; mejora de CPU de al menos 20% si el perfilado identifica margen. No son resultados ni garantías universales. Si el baseline exige otros límites, registrar motivo y renegociar antes de congelar beta.

## 5. Interfaz V2

### Windows

- Pantalla principal con preview, estado de sesión, teléfono activo y acción principal coherente con el estado.
- Asistente de primera conexión con rutas USB/Wi-Fi, detección de ADB no autorizado, cable sin datos, firewall/red aislada y QR vencido.
- Distinguir teléfono vinculado, medios detenidos, audio activo, video activo y salida OBS conectada. No mostrar éxito solo por completar pairing.
- Panel de calidad con preset y parámetros efectivos; cambios que requieren reiniciar indican esa consecuencia antes de aplicarse.
- Medidor de micrófono, silencio visible, señal de video obsoleto y recuperación. No conservar último cuadro como si siguiera en vivo.
- Navegación: Transmisión, Dispositivos, Ajustes y Diagnóstico. Mostrar controles avanzados bajo expansión, con valores y unidades claros.
- Tema claro/oscuro/sistema, navegación por teclado, lector de pantalla, foco visible, contraste, escalado 100–200% y pantallas pequeñas.
- Ayuda OBS contextual con estado del plugin y pasos verificables. No prometer audio por OBS Virtual Camera: comprobar cada ruta de audio y documentarla.

### Android

- Estado compacto de PC/transporte y dos acciones diferenciadas: detener medios y desconectar.
- Presets visibles, cámara/lente por capacidades y resumen antes de iniciar. Audio debe mostrar si captura y si está silenciado.
- Retrato/paisaje con controles accesibles, texto grande y restauración de selección sin reiniciar captura por recreación de Activity.
- Modo pantalla atenuada durante captura, aviso térmico accionable y notificación con acción de detener fiable.
- Errores con causa probable, acción concreta y detalles técnicos desplegables; no reemplazar un error relevante con métricas periódicas.

Entregables UX: mapa de flujos, wireframes de estados normales/fallidos, prototipo navegable, revisión de accesibilidad y prueba de primera conexión con usuarios que no conozcan ADB.

## 6. Catálogo de ajustes

| Grupo | Ajustes propuestos | Condiciones |
|---|---|---|
| Video básico | Automático/equilibrado/calidad/ahorro, lente, resolución, FPS y bitrate máximo. | Mostrar soporte y formato efectivo; 60 FPS solo tras validar cadena completa. |
| Imagen | Espejo de preview separado de salida, orientación, zoom, foco automático/manual, exposición, balance de blancos y anti-parpadeo. | Cada control depende de capacidades; reset por lente. |
| Audio | Solo audio/video/ambos, mute, fuente compatible, ganancia, AEC/NS/AGC y offset A/V avanzado. | Evitar doble procesamiento; ganancia debe limitar clipping; cambios con reinicio explícito. |
| Conexión | Transporte preferido, interfaz de red, timeout y reconexión limitada. | Nunca degradar TLS/autenticación para recuperar conexión. |
| Rendimiento | Preview apagada/reducida, presupuesto de latencia, bitrate adaptativo y política térmica. | Valores avanzados con rangos y defaults medidos. |
| Aplicación | Tema, idioma ES/EN, minimizar a bandeja, comportamiento al cerrar y notificaciones. | Captura activa siempre visible; minimizar/cerrar no cambia semántica silenciosamente. |
| Dispositivos | Nombre local, último uso, revocar, preferencias por teléfono. | No mostrar identificadores sensibles ni autoconectar una captura sin acción explícita. |
| Diagnóstico | Métricas, nivel temporal de log, exportar informe y restaurar ajustes. | Informe local revisable, sin tokens, QR, claves ni contenido de medios. |
| Actualizaciones | Canal estable/preview, verificación manual y aviso de compatibilidad de componentes. | Sin instalar automáticamente; conservar identidad/configuración y probar migración/rollback. |

Resolver precedencia PC/teléfono: Android informa capacidades; PC solicita perfil; teléfono valida y confirma valores efectivos. Evitar dos configuraciones que se sobrescriben sin avisar. Introducir comandos remotos solo después de versionar el control y probar clientes antiguos.

## 7. Compatibilidad

Publicar tres estados: verificado, experimental y no soportado. Un build exitoso no convierte una plataforma en verificada.

- Android: cubrir API mínima 29, intermedias y máximas disponibles al ejecutar QA; al menos tres fabricantes y gamas baja/media/alta. Probar varias lentes, codecs Qualcomm/MediaTek/Exynos cuando haya equipos, micrófono ocupado, permisos revocados, ahorro de energía y cámara desconectada.
- Windows: definir versiones soportadas usando política del proyecto y pruebas reales; cubrir GPU Intel/AMD/NVIDIA, iGPU y equipo híbrido, usuario sin privilegios, ruta con espacios/no ASCII, suspensión y cambio de pantalla/DPI. Windows ARM64 requiere viabilidad específica; no incluirlo como soporte implícito de x64.
- OBS: fijar una versión mínima y matriz de versiones estables seleccionadas al iniciar implementación; probar compatibilidad ABI, instalación/detección de plugin, dos fuentes, cambio de escena, monitorización de audio y OBS Virtual Camera.
- USB: ADB autorizado/no autorizado/offline, varios teléfonos, desconexión/reconexión, puerto distinto, cable defectuoso y ADB de terceros.
- Wi-Fi: 2,4/5 GHz disponibles, red aislada, varias interfaces/VPN, cambio de IP, pérdida/jitter y desconexión del router. IPv6 queda experimental hasta verificar pairing, candidatos y selección de rutas.
- Versiones mixtas: app vieja/nueva, APK viejo/nuevo y plugin viejo/nuevo; negociar soporte o rechazar con instrucciones claras. Mantener fixture de V1 para pruebas de regresión.

4K, HDR, HEVC/AV1, multicámara, driver virtual propio, captura remota por Internet e iOS quedan fuera del compromiso V2 inicial. Evaluarlos tras estabilizar 1080p/audio y medir costo real.

## 8. Equipo profesional y responsabilidades

Para ejecutar el plan, trabajar con los siguientes roles lógicos. Una persona puede cubrir varios; mantener revisión cruzada en código sensible.

| Rol | Responsabilidad y entregable |
|---|---|
| Producto/coordinación | Alcance, prioridades, aceptación, decisiones y seguimiento de riesgos. |
| Tech lead | Arquitectura, ADR, compatibilidad entre componentes e integración. |
| Android | Cámara/audio, servicio, permisos, codecs, energía y UX móvil. |
| Windows/media | Sesiones Qt, decode, concurrencia, métricas y configuración. |
| OBS/IPC y audio | Contratos locales, clocks, colas, continuidad y compatibilidad libobs. |
| UX/UI y accesibilidad | Flujos, estados, componentes y validación con usuarios. |
| QA/automatización | Regresiones reproducibles, fault injection, matriz física y evidencias. |
| Seguridad/release | Pairing, límites de entrada, firma, dependencias, CI e instaladores. |

Organización propuesta: tres frentes de implementación como máximo al principio (Android, Windows/media, QA/contrato); UX diseña sobre los estados acordados. OBS/audio requiere dueño explícito aunque comparta persona con Windows. No abrir cambios simultáneos sobre el mismo contrato sin fixture y responsable de integración.

## 9. Roadmap y dependencias

Estimación orientativa: 10–14 semanas con 3 desarrolladores experimentados, QA con acceso a dispositivos y apoyo parcial UX/release. No equivale a tiempo de agentes ni a una fecha comprometida. Reestimar al terminar M1 según baseline, deuda y disponibilidad de hardware.

| Hito | Ventana orientativa | Entregables | Puerta de salida |
|---|---|---|---|
| M0 · contención | Semana 1 | SC-001, SC-002, prueba de audio USB, revisión de contrato y documentación de limitaciones. | Regressions reproducibles pasan; hotfix candidato validado físicamente. |
| M1 · base medible | Semanas 2–3 | Estados de sesión, métricas/baseline, fixtures, CI Android/Windows y backlog refinado. | Métricas y criterios congelados; ningún fallo P0 abierto. |
| M2 · estabilidad media | Semanas 4–6 | SC-003/004/005/007/008, reloj A/V, colas, cierre/cancelación y reconexión. | 2 h A/V reales USB/Wi-Fi y 100 ciclos start/stop; fallos inyectados recuperables. |
| M3 · producto V2 | Semanas 7–9 | UI V2, configuración migrable, capacidades por transporte, ajustes básicos y diagnóstico. | Recorridos completos y accesibilidad; ajustes efectivos coherentes en ambos extremos. |
| M4 · beta/compatibilidad | Semanas 10–11 | Matriz de equipos/OBS, perfilado, fallback codecs, instalación y actualización. | Sin P0/P1 pendientes; objetivos medidos en entorno de referencia. |
| M5 · candidata/estable | Semanas 12–14 | Sesión 8 h, firma de APK/Windows según infraestructura, rollback, changelog, manuales y artefactos. | Evidencia, paquetes íntegros y revisión de release; anunciar solo soporte verificado. |

Ruta crítica: contrato de audio → cola/reloj → publicación fuera de UI → prueba larga → beta. La negociación de capacidades precede controles remotos. La firma estable precede distribución estable; no cambiar `applicationId` sin plan de migración porque afecta instalación/identidad.

## 10. Pruebas y publicación

- Unitarias/contrato: tamaños y offsets de audio/video, límites y overflow, timestamps, máquinas de estado, configuración/migración, selección de capacidades y versiones.
- Integración: Android emisor/Windows receptor/IPC/OBS, autenticación/revocación, fixtures V1, detención, reconexión y varias fuentes. CTest ejecuta los probes de voz, cola y reloj; registrar los probes restantes como checks con resultado y timeout. No contar una compilación como prueba pasada.
- Fallos: paquete truncado/grande, orden inválido, cliente lento, socket perdido, codec fallido, PC suspendida, permiso revocado y cancelación durante preparación.
- Física: ambos transportes × los tres modos; pantalla bloqueada, cambio de lente/calidad, 100 ciclos, 2 h por ruta esencial y 8 h candidata final.
- Rendimiento: 720p30 y 1080p30 por rutas soportadas; guardar entorno, percentiles, drops y muestra de memoria después del calentamiento.
- Distribución: instalación limpia, reinstalación, actualización desde preview, desinstalación, rutas difíciles, plugin cargado por OBS y rollback conservando settings/identidad.
- CI: auditoría pública existente más compilación/lint y unit tests Android, además de builds/probes Windows y plugin OBS; el job de probes cubre el monitor de underrun. Las dependencias WebRTC y el runtime/cabeceras OBS tienen versiones y checksums fijados. Confirmar remotamente todos los jobs y archivar resultados; secretos solo fuera del repositorio.
- Seguridad: regresiones de pin TLS incorrecto, replay de desafío, QR vencido/usado, dispositivo revocado y mensajes malformados; preservar límites previos a autenticación. Revisar exportación para redactar también direcciones e identificadores según privacidad.

Cada ticket debe tener reproducción o problema de usuario, alcance, dueño, prioridad, dependencias, aceptación observable, prueba y evidencia. Definición de terminado: revisión de código, checks pertinentes, documentación actualizada y QA física cuando toca cámara/audio/transporte. No reemplazar pruebas físicas por datos sintéticos.

## 11. Riesgos y decisiones

| Riesgo | Mitigación |
|---|---|
| Cambiar audio/IPC rompe clientes anteriores. | Mantener contrato compatible cuando sea posible; versionar y negociar cuando cambie. |
| Reconexión mantiene captura inesperada o resucita una sesión cancelada. | Consentimiento vigente, límite temporal, estado visible y token de generación. |
| Optimización GPU aumenta complejidad sin beneficio. | Prototipo aislado con comparación; conservar ruta probada. |
| OEM anuncia capacidades que luego fallan. | Prueba de apertura real, fallback, cache invalidable e informe del formato efectivo. |
| UI/settings crecen antes de estabilizar medios. | Básicos en M3; avanzados experimentales tras aceptación de M2. |
| Falta hardware para prometer compatibilidad. | Publicar verificado/experimental y reservar equipos/tiempo antes de comprometer alcance. |
| Cambio de firma o identidad rompe upgrades. | Definir custodia de claves, backups seguros y ensayo de actualización; nunca versionar secretos. |

Primer sprint recomendado: SC-001, SC-002, fixture binario audio, reproducción SC-003, baseline 720p30 USB/Wi-Fi, CI de probes, actualización del mapa técnico y prototipo de estados de interfaz. Después, reestimar M2 con datos y abrir tickets del resto del backlog.
