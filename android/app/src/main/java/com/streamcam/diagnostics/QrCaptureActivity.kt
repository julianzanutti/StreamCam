package com.streamcam.diagnostics

import android.hardware.Camera
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import com.journeyapps.barcodescanner.CaptureActivity
import com.journeyapps.barcodescanner.DecoratedBarcodeView
import com.journeyapps.barcodescanner.DefaultDecoderFactory
import com.google.zxing.BarcodeFormat

/** Camera IDs must come from Camera1, the API used by the embedded scanner. */
@Suppress("DEPRECATION")
class QrCaptureActivity : CaptureActivity() {
    private lateinit var scanner: DecoratedBarcodeView
    private var cameraId = -1

    override fun initializeContent(): DecoratedBarcodeView {
        scanner = super.initializeContent()
        return scanner
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val cameras = (0 until Camera.getNumberOfCameras()).toList()
        val preferences = getSharedPreferences("qr-scanner", MODE_PRIVATE)
        cameraId = savedInstanceState?.getInt("camera", -1)
            ?: preferences.getInt("camera", -1)
        if (cameraId !in cameras) cameraId = cameras.firstOrNull { id ->
            Camera.CameraInfo().also { Camera.getCameraInfo(id, it) }.facing == Camera.CameraInfo.CAMERA_FACING_BACK
        } ?: cameras.firstOrNull() ?: -1
        scanner.barcodeView.cameraSettings = scanner.barcodeView.cameraSettings.apply {
            requestedCameraId = cameraId
            isContinuousFocusEnabled = true
        }
        // Decode both normal and inverted QR codes.
        scanner.barcodeView.decoderFactory = DefaultDecoderFactory(listOf(BarcodeFormat.QR_CODE), null, null, 2)
        scanner.setStatusText("Apuntá al QR de tu PC. Acercá el teléfono si el código se ve pequeño.")

        val controls = LinearLayout(this).apply { gravity = Gravity.CENTER }
        val switchCamera = Button(this).apply {
            text = "Cambiar cámara"
            isEnabled = cameras.size > 1
            setOnClickListener {
                scanner.pauseAndWait()
                cameraId = cameras[(cameras.indexOf(cameraId) + 1) % cameras.size]
                scanner.barcodeView.cameraSettings = scanner.barcodeView.cameraSettings.apply {
                    requestedCameraId = cameraId
                }
                preferences.edit().putInt("camera", cameraId).apply()
                scanner.resume()
            }
        }
        controls.addView(switchCamera)
        controls.addView(Button(this).apply {
            text = "Volver"
            setOnClickListener { finish() }
        })
        addContentView(controls, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP or Gravity.CENTER_HORIZONTAL
        ))
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putInt("camera", cameraId)
        super.onSaveInstanceState(outState)
    }
}
