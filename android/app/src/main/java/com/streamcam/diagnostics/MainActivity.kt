package com.streamcam.diagnostics

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Matrix
import android.graphics.RectF
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.CaptureResult
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaCodecList
import android.os.Bundle
import android.util.Size
import android.util.Range
import android.view.Surface
import android.view.TextureView
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import org.webrtc.SurfaceViewRenderer

/** Local-only Camera2/encoder diagnostics. Encoded H.264 is discarded and no socket is opened. */
class MainActivity : Activity() {
    private lateinit var cameraManager: CameraManager
    private lateinit var cameraPicker: Spinner
    private lateinit var resolutionPicker: Spinner
    private lateinit var preview: TextureView
    private lateinit var previewStatus: TextView
    private lateinit var captureStatus: TextView
    private lateinit var encoderStatus: TextView
    private lateinit var encoderTestButton: Button
    private lateinit var webrtcCaptureButton: Button
    private lateinit var webrtcPreview: SurfaceViewRenderer
    private lateinit var manualExposureToggle: CheckBox
    private lateinit var isoSlider: SeekBar
    private lateinit var exposureSlider: SeekBar
    private lateinit var isoValueLabel: TextView
    private lateinit var exposureValueLabel: TextView
    private lateinit var report: TextView

    private var cameraDevice: CameraDevice? = null
    private var cameraOpenPending = false
    private var captureSession: CameraCaptureSession? = null
    private var previewSurface: Surface? = null
    private var currentCameraId: String? = null
    private var previewBufferSize: Size? = null
    private var activePreviewSize: Size? = null
    private val previewSizeOptions = mutableListOf<Size>()
    private var updatingResolutionPicker = false
    private var previewFrameCount = 0
    private var previewRateStartMs = 0L
    private var previewRequestBuilder: CaptureRequest.Builder? = null
    private var captureResultCount = 0
    private var appliedIso: Int? = null
    private var appliedExposureNs: Long? = null
    private var h264Encoder: HardwareH264Encoder? = null
    private var webRtcLocalCapture: WebRtcLocalCapture? = null
    private var webRtcCodecDiagnostics: WebRtcCodecDiagnostics? = null
    private var webRtcInitError: String? = null
    private var manualExposureEnabled = false
    private var manualControlsSupported = false
    private var manualIsoRange = 100..6400
    private var manualExposureRangeNs = 42_000L..250_000_000L
    private var manualIso = 400
    private var manualExposureNs = 8_000_000L
    private val cameraIds = mutableListOf<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        cameraManager = getSystemService(Context.CAMERA_SERVICE) as CameraManager
        buildScreen()
        runCatching { WebRtcCodecDiagnostics(applicationContext) }
            .onSuccess { webRtcCodecDiagnostics = it }
            .onFailure { webRtcInitError = it.message ?: it.javaClass.simpleName }
        refreshDiagnostics()
        preview.surfaceTextureListener = surfaceListener
        cameraPicker.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
            override fun onItemSelected(
                parent: android.widget.AdapterView<*>?,
                view: android.view.View?,
                position: Int,
                id: Long
            ) {
                val selected = cameraIds.getOrNull(position) ?: return
                report.text = describeCamera(selected)
                updatePreviewSizeOptions(selected)
                updateManualControlRanges(selected)
                if (preview.isAvailable && hasCameraPermission()) openCamera(selected)
            }
        }

        resolutionPicker.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
            override fun onItemSelected(
                parent: android.widget.AdapterView<*>?,
                view: android.view.View?,
                position: Int,
                id: Long
            ) {
                if (updatingResolutionPicker) return
                previewSizeOptions.getOrNull(position)?.let { selected ->
                    if (previewBufferSize != selected) {
                        previewBufferSize = selected
                        currentCameraId?.let { if (preview.isAvailable && hasCameraPermission()) openCamera(it) }
                    }
                }
            }
        }

        if (hasCameraPermission()) startPreviewIfReady() else {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION_REQUEST)
        }
    }

    private fun buildScreen() {
        val page = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(16))
        }
        val title = TextView(this).apply {
            text = "StreamCam · Diagnóstico de cámara"
            textSize = 20f
        }
        val note = TextView(this).apply {
            text = "La vista previa queda en este teléfono. La prueba H.264 descarta los cuadros codificados; no transmite ni guarda video."
            textSize = 14f
            setPadding(0, dp(8), 0, dp(8))
        }
        cameraPicker = Spinner(this)
        resolutionPicker = Spinner(this)
        manualExposureToggle = CheckBox(this).apply {
            text = "Exposición manual (ISO y obturación)"
            isEnabled = false
            setOnCheckedChangeListener { _, checked ->
                manualExposureEnabled = checked && manualControlsSupported
                isoSlider.isEnabled = manualExposureEnabled
                exposureSlider.isEnabled = manualExposureEnabled
                applyPreviewRequest()
            }
        }
        isoValueLabel = TextView(this).apply { textSize = 13f }
        isoSlider = SeekBar(this).apply {
            max = 1000
            isEnabled = false
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    manualIso = mapProgress(progress, manualIsoRange.first, manualIsoRange.last)
                    isoValueLabel.text = "ISO: $manualIso"
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
                override fun onStopTrackingTouch(seekBar: SeekBar?) = applyPreviewRequest()
            })
        }
        exposureValueLabel = TextView(this).apply { textSize = 13f }
        exposureSlider = SeekBar(this).apply {
            max = 1000
            isEnabled = false
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    manualExposureNs = mapExposureProgress(progress, manualExposureRangeNs)
                    exposureValueLabel.text = shutterLabel(manualExposureNs)
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
                override fun onStopTrackingTouch(seekBar: SeekBar?) = applyPreviewRequest()
            })
        }
        previewStatus = TextView(this).apply {
            text = "Esperando la cámara…"
            textSize = 13f
            setPadding(0, dp(4), 0, dp(4))
        }
        captureStatus = TextView(this).apply {
            text = "Esperando resultados Camera2…"
            textSize = 13f
            setPadding(0, 0, 0, dp(4))
        }
        encoderStatus = TextView(this).apply {
            text = "Encoder H.264 detenido"
            textSize = 13f
            setPadding(0, dp(2), 0, dp(4))
        }
        encoderTestButton = Button(this).apply {
            text = "Iniciar prueba H.264 hardware"
            isEnabled = false
            setOnClickListener {
                if (h264Encoder == null) startH264Test() else stopH264Test()
            }
        }
        webrtcCaptureButton = Button(this).apply {
            text = "Probar captura WebRTC local"
            isEnabled = false
            setOnClickListener {
                if (webRtcLocalCapture == null) startWebRtcLocalCapture() else stopWebRtcLocalCapture()
            }
        }
        preview = TextureView(this)
        preview.layoutParams = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            dp(220)
        ).apply { bottomMargin = dp(8) }
        webrtcPreview = SurfaceViewRenderer(this).apply {
            visibility = android.view.View.GONE
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(220)
            ).apply { bottomMargin = dp(8) }
        }
        report = TextView(this).apply {
            textSize = 13f
            typeface = android.graphics.Typeface.MONOSPACE
            setTextIsSelectable(true)
        }
        page.addView(title)
        page.addView(note)
        page.addView(cameraPicker)
        page.addView(resolutionPicker)
        page.addView(previewStatus)
        page.addView(captureStatus)
        page.addView(encoderStatus)
        page.addView(encoderTestButton)
        page.addView(webrtcCaptureButton)
        page.addView(webrtcPreview)
        page.addView(preview)
        page.addView(manualExposureToggle)
        page.addView(isoValueLabel)
        page.addView(isoSlider)
        page.addView(exposureValueLabel)
        page.addView(exposureSlider)
        page.addView(report)
        setContentView(ScrollView(this).apply {
            isFillViewport = true
            addView(page)
        })
    }

    private fun refreshDiagnostics() {
        cameraIds.clear()
        runCatching { cameraIds.addAll(cameraManager.cameraIdList) }
            .onFailure { report.text = "No se pudo enumerar cámaras: ${it.message}" }

        val labels = cameraIds.map { id ->
            val chars = runCatching { cameraManager.getCameraCharacteristics(id) }.getOrNull()
            val facing = chars?.get(CameraCharacteristics.LENS_FACING)
            val facingText = when (facing) {
                CameraCharacteristics.LENS_FACING_BACK -> "trasera"
                CameraCharacteristics.LENS_FACING_FRONT -> "frontal"
                CameraCharacteristics.LENS_FACING_EXTERNAL -> "externa"
                else -> "orientación desconocida"
            }
            "Cámara $id · $facingText"
        }

        cameraPicker.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, labels)
        if (cameraIds.isEmpty()) report.text = "El sistema no anunció cámaras Camera2."
        else {
            report.text = describeCamera(cameraIds.first())
            updatePreviewSizeOptions(cameraIds.first())
            updateManualControlRanges(cameraIds.first())
        }
        if (cameraIds.isNotEmpty() && preview.isAvailable && hasCameraPermission()) {
            openCamera(cameraIds.first())
        }
    }

    private fun updatePreviewSizeOptions(cameraId: String) {
        val sizes = runCatching {
            cameraManager.getCameraCharacteristics(cameraId)
                .get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                ?.getOutputSizes(SurfaceTexture::class.java)
                ?.filter { it.width <= 1920 && it.height <= 1080 }
                .orEmpty()
        }.getOrDefault(emptyList())
        val preferred = listOf(Size(1920, 1080), Size(1280, 720), Size(960, 540), Size(640, 480))
        previewSizeOptions.clear()
        preferred.forEach { target -> sizes.firstOrNull { it == target }?.let(previewSizeOptions::add) }
        if (previewSizeOptions.isEmpty()) {
            sizes.sortedByDescending { it.width.toLong() * it.height }.take(5).forEach(previewSizeOptions::add)
        }
        updatingResolutionPicker = true
        resolutionPicker.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_dropdown_item,
            previewSizeOptions.map { "${it.width}×${it.height}" }
        )
        val defaultIndex = previewSizeOptions.indexOfFirst { it.width == 1280 && it.height == 720 }
            .takeIf { it >= 0 } ?: 0
        previewBufferSize = previewSizeOptions.getOrNull(defaultIndex)
        resolutionPicker.setSelection(defaultIndex, false)
        updatingResolutionPicker = false
    }

    private fun updateManualControlRanges(cameraId: String) {
        val characteristics = runCatching { cameraManager.getCameraCharacteristics(cameraId) }.getOrNull()
        val capabilities = characteristics?.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)
        manualControlsSupported = capabilities?.contains(
            CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR
        ) == true
        characteristics?.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE)?.let {
            manualIsoRange = it.lower..it.upper
        }
        characteristics?.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE)?.let {
            manualExposureRangeNs = it.lower..it.upper
        }
        if (!manualControlsSupported) {
            manualExposureEnabled = false
            manualExposureToggle.isChecked = false
        }
        manualIso = manualIso.coerceIn(manualIsoRange.first, manualIsoRange.last)
        manualExposureNs = manualExposureNs.coerceIn(manualExposureRangeNs.first, manualExposureRangeNs.last)
        manualExposureToggle.isEnabled = manualControlsSupported
        manualExposureToggle.text = if (manualControlsSupported) {
            "Exposición manual (ISO y obturación)"
        } else {
            "Exposición manual no disponible en esta cámara"
        }
        isoSlider.isEnabled = manualExposureEnabled && manualControlsSupported
        exposureSlider.isEnabled = manualExposureEnabled && manualControlsSupported
        isoSlider.progress = valueToProgress(manualIso, manualIsoRange.first, manualIsoRange.last)
        exposureSlider.progress = exposureProgressFor(manualExposureNs, manualExposureRangeNs)
        isoValueLabel.text = "ISO: $manualIso · rango ${manualIsoRange.first}–${manualIsoRange.last}"
        exposureValueLabel.text = "${shutterLabel(manualExposureNs)} · máximo anunciado ${shutterLabel(manualExposureRangeNs.last)}"
    }

    private fun mapProgress(progress: Int, minimum: Int, maximum: Int): Int =
        (minimum + (maximum - minimum).toDouble() * progress.coerceIn(0, 1000) / 1000.0).toInt()

    private fun valueToProgress(value: Int, minimum: Int, maximum: Int): Int =
        if (maximum <= minimum) 0 else {
            ((value - minimum).toDouble() * 1000 / (maximum - minimum)).toInt().coerceIn(0, 1000)
        }

    private fun mapExposureProgress(progress: Int, range: LongRange): Long {
        val minLog = kotlin.math.ln(range.first.toDouble())
        val maxLog = kotlin.math.ln(range.last.toDouble())
        return kotlin.math.exp(minLog + (maxLog - minLog) * progress.coerceIn(0, 1000) / 1000.0)
            .toLong().coerceIn(range.first, range.last)
    }

    private fun exposureProgressFor(value: Long, range: LongRange): Int {
        if (range.last <= range.first) return 0
        val minLog = kotlin.math.ln(range.first.toDouble())
        val maxLog = kotlin.math.ln(range.last.toDouble())
        return ((kotlin.math.ln(value.coerceAtLeast(1L).toDouble()) - minLog) * 1000 / (maxLog - minLog))
            .toInt().coerceIn(0, 1000)
    }

    private fun shutterLabel(exposureNs: Long): String =
        "Obturación: 1/${(1_000_000_000L / exposureNs.coerceAtLeast(1L))} s"

    private fun describeCamera(cameraId: String): String = runCatching {
        val chars = cameraManager.getCameraCharacteristics(cameraId)
        val facing = when (chars.get(CameraCharacteristics.LENS_FACING)) {
            CameraCharacteristics.LENS_FACING_BACK -> "Trasera"
            CameraCharacteristics.LENS_FACING_FRONT -> "Frontal"
            CameraCharacteristics.LENS_FACING_EXTERNAL -> "Externa"
            else -> "Desconocida"
        }
        val map = chars.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        buildString {
            appendLine("ID: $cameraId · $facing")
            appendLine("Nivel Camera2: ${chars.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)?.let(::hardwareLevel)}")
            appendLine("Estabilización video: ${chars.get(CameraCharacteristics.CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES)?.joinToString() ?: "sin dato"}")
            appendLine("Autoenfoque: ${chars.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)?.joinToString() ?: "sin dato"}")
            appendLine("Antiparpadeo: ${chars.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_ANTIBANDING_MODES)?.joinToString() ?: "sin dato"}")
            appendLine("Exposición manual (sensor): ${chars.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)?.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR) == true}")
            appendLine("Postprocesamiento manual: ${chars.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)?.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING) == true}")
            appendLine("ISO: ${chars.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE) ?: "automático / sin rango manual"}")
            appendLine("Exposición: ${chars.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE) ?: "automática / sin rango manual"}")
            appendLine("Distancia mínima enfoque: ${chars.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE) ?: "sin dato"}")
            appendLine()
            appendLine("Salidas SurfaceTexture anunciadas:")
            appendSizes(map?.getOutputSizes(SurfaceTexture::class.java))
            appendLine("Salidas YUV anunciadas:")
            appendSizes(map?.getOutputSizes(android.graphics.ImageFormat.YUV_420_888))
            appendLine("Rangos FPS anunciados:")
            val ranges = chars.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
            appendLine(ranges?.joinToString { "${it.lower}–${it.upper}" } ?: "sin dato")
            appendLine()
            appendLine("Encoders H.264 del teléfono:")
            val codecs = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
                .filter { it.isEncoder && it.supportedTypes.any { type -> type.equals("video/avc", true) } }
            if (codecs.isEmpty()) appendLine("No se encontró encoder H.264 publicitado.")
            codecs.forEach { codec ->
                appendLine("• ${codec.name} · hardware=${codec.isHardwareAccelerated}")
                runCatching { codec.getCapabilitiesForType("video/avc").videoCapabilities }
                    .onSuccess { caps ->
                        appendLine("  tamaño: ${caps.supportedWidths} × ${caps.supportedHeights}")
                        appendLine("  tasa: ${caps.supportedFrameRates}")
                    }
            }
            appendLine()
            appendLine("WebRTC para transmisión futura:")
            appendLine(webRtcCodecDiagnostics?.report ?: "No se pudo inicializar: ${webRtcInitError ?: "sin resultado"}")
            appendLine()
            appendLine("Nota: las capacidades declaradas no garantizan una sesión sostenida a ese tamaño/FPS. Verificar cámara + encoder juntos en el prototipo.")
        }
    }.getOrElse { "No se pudo leer la cámara $cameraId: ${it.message}" }

    private fun StringBuilder.appendSizes(sizes: Array<Size>?) {
        if (sizes.isNullOrEmpty()) {
            appendLine("sin dato")
            return
        }
        sizes.sortedByDescending { it.width.toLong() * it.height }
            .forEach { appendLine("${it.width}×${it.height}") }
    }

    private fun hardwareLevel(level: Int): String = when (level) {
        CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY -> "LEGACY"
        CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED -> "LIMITED"
        CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_FULL -> "FULL"
        CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_3 -> "LEVEL_3"
        CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL -> "EXTERNAL"
        else -> "desconocido ($level)"
    }

    private val surfaceListener = object : TextureView.SurfaceTextureListener {
        override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) = startPreviewIfReady()
        override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
            configurePreviewTransform(width, height)
        }
        override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
            closeCamera()
            return true
        }
        override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
            previewFrameCount++
            if (previewRateStartMs == 0L) previewRateStartMs = android.os.SystemClock.elapsedRealtime()
            if (previewFrameCount >= 30) {
                val now = android.os.SystemClock.elapsedRealtime()
                val elapsed = (now - previewRateStartMs).coerceAtLeast(1L)
                val observedFps = previewFrameCount * 1000f / elapsed
                previewStatus.text = "Vista previa ${activePreviewSize?.let { "${it.width}×${it.height}" } ?: "activa"} · ${"%.1f".format(observedFps)} FPS observados"
                previewFrameCount = 0
                previewRateStartMs = now
            }
        }
    }

    private fun startPreviewIfReady() {
        if (!hasCameraPermission() || !preview.isAvailable || cameraIds.isEmpty()) return
        val id = cameraIds.getOrNull(cameraPicker.selectedItemPosition) ?: cameraIds.first()
        openCamera(id)
    }

    private fun openCamera(cameraId: String) {
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return
        if (currentCameraId == cameraId && cameraOpenPending) return
        if (currentCameraId == cameraId && cameraDevice != null && activePreviewSize == previewBufferSize) return
        closeCamera()
        currentCameraId = cameraId
        cameraOpenPending = true
        runCatching {
            cameraManager.openCamera(cameraId, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    cameraOpenPending = false
                    if (currentCameraId != cameraId || isFinishing || isDestroyed) {
                        camera.close()
                        return
                    }
                    cameraDevice = camera
                    createPreviewSession(camera)
                }
                override fun onDisconnected(camera: CameraDevice) {
                    cameraOpenPending = false
                    camera.close()
                    if (cameraDevice === camera) cameraDevice = null
                    if (currentCameraId == cameraId) currentCameraId = null
                    captureSession = null
                }
                override fun onError(camera: CameraDevice, error: Int) {
                    cameraOpenPending = false
                    camera.close()
                    if (cameraDevice === camera) cameraDevice = null
                    if (currentCameraId == cameraId) currentCameraId = null
                    captureSession = null
                    runOnUiThread { Toast.makeText(this@MainActivity, "Error de cámara: $error", Toast.LENGTH_LONG).show() }
                }
            }, null)
        }.onFailure {
            cameraOpenPending = false
            currentCameraId = null
            Toast.makeText(this, "No se pudo abrir la cámara: ${it.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun createPreviewSession(camera: CameraDevice) {
        val texture = preview.surfaceTexture ?: return
        val sizes = runCatching {
            cameraManager.getCameraCharacteristics(camera.id)
                .get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                ?.getOutputSizes(SurfaceTexture::class.java)
                ?.toList()
                .orEmpty()
        }.getOrDefault(emptyList())
        val size = previewBufferSize?.takeIf { it in sizes } ?: sizes
            .filter { it.width <= 1920 && it.height <= 1080 }
            .minByOrNull { kotlin.math.abs(it.width - 1280) + kotlin.math.abs(it.height - 720) }
            ?: sizes.minByOrNull { it.width.toLong() * it.height }
        if (size == null) {
            Toast.makeText(this, "Esta cámara no anunció tamaños compatibles con la vista previa.", Toast.LENGTH_LONG).show()
            return
        }
        texture.setDefaultBufferSize(size.width, size.height)
        previewBufferSize = size
        setPreviewAspectRatio(camera.id, size)
        val surface = Surface(texture)
        previewSurface = surface
        val outputSurfaces = buildList {
            add(surface)
            h264Encoder?.inputSurface?.let(::add)
        }
        runCatching {
            camera.createCaptureSession(outputSurfaces, object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(session: CameraCaptureSession) {
                    if (cameraDevice !== camera) {
                        session.close()
                        surface.release()
                        if (previewSurface === surface) previewSurface = null
                        return
                    }
                    captureSession = session
                    activePreviewSize = size
                    encoderTestButton.isEnabled = true
                    webrtcCaptureButton.isEnabled = true
                    runCatching {
                        val builder = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                            addTarget(surface)
                            h264Encoder?.inputSurface?.let(::addTarget)
                            set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO)
                            val characteristics = cameraManager.getCameraCharacteristics(camera.id)
                            val afModes = characteristics.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES) ?: intArrayOf()
                            val afMode = when {
                                CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO in afModes -> CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO
                                CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE in afModes -> CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE
                                else -> CaptureRequest.CONTROL_AF_MODE_AUTO
                            }
                            set(CaptureRequest.CONTROL_AF_MODE, afMode)
                            val stabilizationModes = characteristics.get(CameraCharacteristics.CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES) ?: intArrayOf()
                            if (CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_ON in stabilizationModes) {
                                set(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_ON)
                            }
                        }
                        previewRequestBuilder = builder
                        previewStatus.text = "Iniciando ${size.width}×${size.height}…"
                        previewFrameCount = 0
                        previewRateStartMs = android.os.SystemClock.elapsedRealtime()
                        applyPreviewRequest()
                    }.onFailure {
                        Toast.makeText(this@MainActivity, "No se pudo iniciar la vista previa: ${it.message}", Toast.LENGTH_LONG).show()
                    }
                }
                override fun onConfigureFailed(session: CameraCaptureSession) {
                    session.close()
                    surface.release()
                    if (previewSurface === surface) previewSurface = null
                    Toast.makeText(this@MainActivity, "El sistema no pudo configurar la vista previa.", Toast.LENGTH_LONG).show()
                }
            }, null)
        }.onFailure {
            surface.release()
            Toast.makeText(this, "No se pudo crear la vista previa: ${it.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun preferredPreviewFpsRange(characteristics: CameraCharacteristics): Range<Int>? {
        val ranges = characteristics.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
            ?: return null
        return ranges.firstOrNull { it.lower == 30 && it.upper == 30 }
            ?: ranges.firstOrNull { it.upper == 30 && it.lower <= 30 }
    }

    private fun startH264Test() {
        val camera = cameraDevice ?: run {
            Toast.makeText(this, "La cámara todavía no está lista.", Toast.LENGTH_SHORT).show()
            return
        }
        val size = activePreviewSize ?: run {
            Toast.makeText(this, "No hay un tamaño de preview activo.", Toast.LENGTH_SHORT).show()
            return
        }
        runCatching {
            val encoder = HardwareH264Encoder(
                width = size.width,
                height = size.height,
                frameRate = 30,
                onStats = { codecName, fps, bitrateKbps ->
                    runOnUiThread {
                        if (h264Encoder != null) {
                            encoderStatus.text = "H.264 hardware · $codecName · ${"%.1f".format(fps)} FPS · ${bitrateKbps} kbit/s"
                        }
                    }
                },
                onError = { message ->
                    runOnUiThread { encoderStatus.text = "Error del encoder: $message" }
                }
            )
            encoder.start()
            h264Encoder = encoder
            encoderTestButton.text = "Detener prueba H.264"
            encoderStatus.text = "Encoder iniciado · ${size.width}×${size.height} · esperando cuadros codificados…"
            reconfigureCameraSession(camera)
        }.onFailure {
            h264Encoder?.stop()
            h264Encoder = null
            encoderTestButton.text = "Iniciar prueba H.264 hardware"
            encoderStatus.text = "No se pudo iniciar H.264: ${it.message}"
            Toast.makeText(this, "No se pudo iniciar el encoder H.264: ${it.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun stopH264Test() {
        val encoder = h264Encoder ?: return
        h264Encoder = null
        encoder.stop()
        encoderTestButton.text = "Iniciar prueba H.264 hardware"
        encoderStatus.text = "Prueba H.264 detenida"
        cameraDevice?.let(::reconfigureCameraSession)
    }

    private fun startWebRtcLocalCapture() {
        val size = previewBufferSize ?: return
        val diagnostics = webRtcCodecDiagnostics ?: run {
            Toast.makeText(this, "El runtime WebRTC no está disponible.", Toast.LENGTH_LONG).show()
            return
        }
        closeCamera()
        preview.visibility = android.view.View.GONE
        webrtcPreview.visibility = android.view.View.VISIBLE
        webrtcCaptureButton.text = "Detener captura WebRTC local"
        webrtcCaptureButton.isEnabled = true
        captureStatus.text = "Iniciando WebRTC Camera2Capturer…"
        runCatching {
            WebRtcLocalCapture(
                context = applicationContext,
                factory = diagnostics.peerConnectionFactory,
                eglContext = diagnostics.eglBase.eglBaseContext,
                renderer = webrtcPreview,
                width = size.width,
                height = size.height,
                frameRate = 30,
                onStatus = { status -> runOnUiThread { captureStatus.text = status } }
            ).also { webRtcLocalCapture = it }
        }.onFailure {
            webRtcLocalCapture?.close()
            webRtcLocalCapture = null
            webrtcPreview.visibility = android.view.View.GONE
            preview.visibility = android.view.View.VISIBLE
            webrtcCaptureButton.text = "Probar captura WebRTC local"
            captureStatus.text = "Falló la captura local WebRTC: ${it.message}"
            Toast.makeText(this, "WebRTC Camera2Capturer: ${it.message}", Toast.LENGTH_LONG).show()
            startPreviewIfReady()
        }
    }

    private fun stopWebRtcLocalCapture() {
        closeCamera()
        startPreviewIfReady()
    }

    private fun reconfigureCameraSession(camera: CameraDevice) {
        runCatching { captureSession?.stopRepeating() }
        runCatching { captureSession?.close() }
        captureSession = null
        runCatching { previewSurface?.release() }
        previewSurface = null
        activePreviewSize = null
        createPreviewSession(camera)
    }

    private fun applyPreviewRequest() {
        val builder = previewRequestBuilder ?: return
        val session = captureSession ?: return
        runCatching {
            if (manualExposureEnabled && manualControlsSupported) {
                builder.set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_OFF)
                builder.set(CaptureRequest.SENSOR_SENSITIVITY, manualIso.coerceIn(manualIsoRange.first, manualIsoRange.last))
                builder.set(CaptureRequest.SENSOR_EXPOSURE_TIME, manualExposureNs.coerceIn(manualExposureRangeNs.first, manualExposureRangeNs.last))
            } else {
                builder.set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_ON)
                val cameraId = currentCameraId ?: return
                val characteristics = cameraManager.getCameraCharacteristics(cameraId)
                preferredPreviewFpsRange(characteristics)?.let {
                    builder.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, it)
                }
            }
                        captureResultCount = 0
                        session.setRepeatingRequest(builder.build(), object : CameraCaptureSession.CaptureCallback() {
                            override fun onCaptureCompleted(
                                session: CameraCaptureSession,
                                request: CaptureRequest,
                                result: android.hardware.camera2.TotalCaptureResult
                            ) {
                                captureResultCount++
                                val iso = result.get(CaptureResult.SENSOR_SENSITIVITY)
                                val exposure = result.get(CaptureResult.SENSOR_EXPOSURE_TIME)
                                if (iso != null) appliedIso = iso
                                if (exposure != null) appliedExposureNs = exposure
                                if (captureResultCount % 15 == 0) {
                                    runOnUiThread {
                                        val actual = if (manualExposureEnabled) {
                                            " · aplicado ISO ${appliedIso ?: "—"}, ${appliedExposureNs?.let(::shutterLabel) ?: "—"}"
                                        } else {
                                            " · AE ISO ${appliedIso ?: "—"}, ${appliedExposureNs?.let(::shutterLabel) ?: "—"}"
                                        }
                                        captureStatus.text = "Resultados Camera2$actual"
                                    }
                                }
                            }
                        }, null)
        }.onFailure {
            previewStatus.text = "No se pudo aplicar la configuración: ${it.message}"
        }
    }

    /** Keep the displayed frame's aspect ratio and account for sensor/display rotation. */
    private fun setPreviewAspectRatio(cameraId: String, size: Size) {
        val sensorOrientation = runCatching {
            cameraManager.getCameraCharacteristics(cameraId)
                .get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 0
        }.getOrDefault(0)
        val rotation = windowManager.defaultDisplay.rotation
        val dimensionsSwapped = when (rotation) {
            Surface.ROTATION_0, Surface.ROTATION_180 -> sensorOrientation == 90 || sensorOrientation == 270
            Surface.ROTATION_90, Surface.ROTATION_270 -> sensorOrientation == 0 || sensorOrientation == 180
            else -> false
        }
        val aspectWidth = if (dimensionsSwapped) size.height else size.width
        val aspectHeight = if (dimensionsSwapped) size.width else size.height
        preview.post {
            val viewWidth = preview.width
            if (viewWidth > 0 && aspectWidth > 0) {
                val params = preview.layoutParams
                params.height = (viewWidth.toFloat() * aspectHeight / aspectWidth).toInt()
                preview.layoutParams = params
                configurePreviewTransform(preview.width, preview.height)
            }
        }
    }

    private fun configurePreviewTransform(viewWidth: Int, viewHeight: Int) {
        val buffer = previewBufferSize ?: return
        if (viewWidth <= 0 || viewHeight <= 0) return
        val rotation = windowManager.defaultDisplay.rotation
        val matrix = Matrix()
        val viewRect = RectF(0f, 0f, viewWidth.toFloat(), viewHeight.toFloat())
        val bufferRect = RectF(0f, 0f, buffer.height.toFloat(), buffer.width.toFloat())
        val centerX = viewRect.centerX()
        val centerY = viewRect.centerY()

        if (rotation == Surface.ROTATION_90 || rotation == Surface.ROTATION_270) {
            bufferRect.offset(centerX - bufferRect.centerX(), centerY - bufferRect.centerY())
            val scale = maxOf(viewHeight.toFloat() / buffer.height, viewWidth.toFloat() / buffer.width)
            matrix.setRectToRect(viewRect, bufferRect, Matrix.ScaleToFit.FILL)
            matrix.postScale(scale, scale, centerX, centerY)
            matrix.postRotate((90 * (rotation - 2)).toFloat(), centerX, centerY)
        } else if (rotation == Surface.ROTATION_180) {
            matrix.postRotate(180f, centerX, centerY)
        }
        preview.setTransform(matrix)
    }

    private fun closeCamera() {
        val webRtcCapture = webRtcLocalCapture
        webRtcLocalCapture = null
        webRtcCapture?.close()
        if (::webrtcPreview.isInitialized) webrtcPreview.visibility = android.view.View.GONE
        if (::preview.isInitialized) preview.visibility = android.view.View.VISIBLE
        if (::webrtcCaptureButton.isInitialized) {
            webrtcCaptureButton.text = "Probar captura WebRTC local"
            webrtcCaptureButton.isEnabled = false
        }
        val encoder = h264Encoder
        h264Encoder = null
        encoder?.stop()
        if (::encoderTestButton.isInitialized) {
            encoderTestButton.text = "Iniciar prueba H.264 hardware"
            encoderTestButton.isEnabled = false
        }
        if (::encoderStatus.isInitialized && encoder != null) encoderStatus.text = "Encoder H.264 detenido"
        runCatching { captureSession?.stopRepeating() }
        runCatching { captureSession?.close() }
        captureSession = null
        runCatching { previewSurface?.release() }
        previewSurface = null
        previewRequestBuilder = null
        appliedIso = null
        appliedExposureNs = null
        runCatching { cameraDevice?.close() }
        cameraDevice = null
        cameraOpenPending = false
        currentCameraId = null
        activePreviewSize = null
        previewStatus.text = "Vista previa detenida"
        captureStatus.text = "Esperando resultados Camera2…"
    }

    private fun hasCameraPermission(): Boolean =
        ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == CAMERA_PERMISSION_REQUEST && grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) {
            refreshDiagnostics()
            startPreviewIfReady()
        } else if (requestCode == CAMERA_PERMISSION_REQUEST) {
            Toast.makeText(this, "Se necesita permiso de cámara para mostrar el diagnóstico.", Toast.LENGTH_LONG).show()
        }
    }

    override fun onPause() {
        closeCamera()
        super.onPause()
    }

    override fun onDestroy() {
        closeCamera()
        webRtcCodecDiagnostics?.close()
        webRtcCodecDiagnostics = null
        super.onDestroy()
    }

    override fun onResume() {
        super.onResume()
        if (::preview.isInitialized && preview.isAvailable) startPreviewIfReady()
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    companion object {
        private const val CAMERA_PERMISSION_REQUEST = 1001
    }
}
