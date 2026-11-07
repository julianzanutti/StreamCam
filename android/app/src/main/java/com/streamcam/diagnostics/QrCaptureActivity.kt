package com.streamcam.diagnostics

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.hardware.camera2.CameraCharacteristics
import android.os.Bundle
import android.util.Size
import android.view.Gravity
import android.view.MotionEvent
import android.view.Surface
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.camera.camera2.interop.Camera2CameraInfo
import androidx.camera.camera2.interop.ExperimentalCamera2Interop
import androidx.camera.core.Camera
import androidx.camera.core.CameraInfo
import androidx.camera.core.CameraSelector
import androidx.camera.core.ExperimentalGetImage
import androidx.camera.core.FocusMeteringAction
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import java.util.concurrent.Executors

/** Bundled, offline QR recognition on full CameraX frames, independent of preview cropping. */
@androidx.annotation.OptIn(markerClass = [ExperimentalCamera2Interop::class, ExperimentalGetImage::class])
class QrCaptureActivity : AppCompatActivity() {
    private lateinit var preview: PreviewView
    private lateinit var status: TextView
    private lateinit var switchCamera: Button
    private lateinit var zoom: Button
    private val executor = Executors.newSingleThreadExecutor()
    private val scanner = BarcodeScanning.getClient(BarcodeScannerOptions.Builder()
        .setBarcodeFormats(Barcode.FORMAT_QR_CODE).build())
    private var provider: ProcessCameraProvider? = null
    private var cameras = emptyList<CameraInfo>()
    private var camera: Camera? = null
    private var selected = 0
    private var generation = 0
    private var returned = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        ViewCompat.setOnApplyWindowInsetsListener(root) { view, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            insets
        }
        status = TextView(this).apply {
            text = "Apuntá al QR completo de la PC. Tocá la imagen para enfocar."
            gravity = Gravity.CENTER
            setPadding(16, 16, 16, 16)
        }
        root.addView(status)
        preview = PreviewView(this).apply {
            implementationMode = PreviewView.ImplementationMode.COMPATIBLE
            scaleType = PreviewView.ScaleType.FIT_CENTER
        }
        root.addView(preview, LinearLayout.LayoutParams(-1, 0, 1f))
        val controls = LinearLayout(this).apply { gravity = Gravity.CENTER }
        switchCamera = Button(this).apply {
            text = "Cámara"
            isEnabled = false
            setOnClickListener { selected = (selected + 1) % cameras.size; bindCamera() }
        }
        zoom = Button(this).apply {
            text = "Zoom 1×"
            isEnabled = false
            setOnClickListener {
                val active = camera ?: return@setOnClickListener
                val state = active.cameraInfo.zoomState.value ?: return@setOnClickListener
                val ratio = if (state.zoomRatio > 1.1f) 1f else minOf(2f, state.maxZoomRatio)
                active.cameraControl.setZoomRatio(ratio.coerceIn(state.minZoomRatio, state.maxZoomRatio))
                text = if (ratio > 1.1f) "Zoom 2×" else "Zoom 1×"
            }
        }
        controls.addView(switchCamera)
        controls.addView(zoom)
        controls.addView(Button(this).apply { text = "Volver"; setOnClickListener { finish() } })
        root.addView(controls)
        setContentView(root)
        preview.setOnTouchListener { view, event ->
            if (event.action == MotionEvent.ACTION_UP) {
                view.performClick()
                val point = preview.meteringPointFactory.createPoint(event.x, event.y)
                camera?.cameraControl?.startFocusAndMetering(FocusMeteringAction.Builder(point).build())
            }
            true
        }
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            openScanner()
        } else requestPermissions(arrayOf(Manifest.permission.CAMERA), 10)
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 10 && grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) openScanner()
        else status.text = "Permití el acceso a la cámara para leer el QR."
    }

    private fun openScanner() {
        val future = ProcessCameraProvider.getInstance(this)
        future.addListener({
            if (isFinishing || isDestroyed) return@addListener
            runCatching {
                provider = future.get()
                cameras = provider!!.availableCameraInfos
                check(cameras.isNotEmpty())
                val saved = getSharedPreferences("qr-scanner", MODE_PRIVATE).getString("camera2", null)
                selected = cameras.indexOfFirst { Camera2CameraInfo.from(it).cameraId == saved }
                if (selected < 0) selected = cameras.indexOfFirst {
                    Camera2CameraInfo.from(it).getCameraCharacteristic(CameraCharacteristics.LENS_FACING) ==
                        CameraCharacteristics.LENS_FACING_BACK
                }.coerceAtLeast(0)
                switchCamera.isEnabled = cameras.size > 1
                bindCamera()
            }.onFailure { status.text = "No se pudo abrir la cámara. Cerrá otras apps e intentá de nuevo." }
        }, ContextCompat.getMainExecutor(this))
    }

    private fun bindCamera() {
        val owner = provider ?: return
        val frameGeneration = ++generation
        owner.unbindAll()
        camera = null
        zoom.isEnabled = false
        zoom.text = "Zoom 1×"
        val rotation = preview.display?.rotation ?: Surface.ROTATION_0
        val analysis = ImageAnalysis.Builder()
            .setTargetRotation(rotation)
            .setResolutionSelector(ResolutionSelector.Builder().setResolutionStrategy(
                ResolutionStrategy(Size(1920, 1080), ResolutionStrategy.FALLBACK_RULE_CLOSEST_LOWER_THEN_HIGHER)
            ).build())
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .build()
        analysis.setAnalyzer(executor) { frame ->
            val image = frame.image
            if (image == null) frame.close()
            else try {
                scanner.process(InputImage.fromMediaImage(image, frame.imageInfo.rotationDegrees))
                    .addOnSuccessListener { codes ->
                        if (!returned && frameGeneration == generation && !isFinishing && !isDestroyed) {
                            val value = codes.firstNotNullOfOrNull { it.rawValue }
                            if (value != null) {
                                returned = true
                                setResult(RESULT_OK, Intent().putExtra("SCAN_RESULT", value)
                                    .putExtra("SCAN_RESULT_FORMAT", "QR_CODE"))
                                finish()
                            }
                        }
                    }
                    .addOnFailureListener {
                        if (frameGeneration == generation && !isDestroyed) status.text = "No se pudo analizar la imagen. Volvé a abrir el lector."
                    }
                    .addOnCompleteListener { frame.close() }
            } catch (_: Exception) { frame.close() }
        }
        val stream = Preview.Builder().setTargetRotation(rotation).build()
        stream.surfaceProvider = preview.surfaceProvider
        val info = cameras[selected]
        val selector = CameraSelector.Builder().addCameraFilter { available -> available.filter { it == info } }.build()
        runCatching {
            camera = owner.bindToLifecycle(this, selector, stream, analysis)
            zoom.isEnabled = true
            val id = Camera2CameraInfo.from(info).cameraId
            getSharedPreferences("qr-scanner", MODE_PRIVATE).edit().putString("camera2", id).apply()
            status.text = "Cámara $id · Encuadrá todo el QR. Tocá para enfocar o probá el zoom."
        }.onFailure { status.text = "Esta cámara no está disponible. Probá cambiar de cámara." }
    }

    override fun onDestroy() {
        generation++
        provider?.unbindAll()
        executor.shutdown()
        scanner.close()
        super.onDestroy()
    }
}
