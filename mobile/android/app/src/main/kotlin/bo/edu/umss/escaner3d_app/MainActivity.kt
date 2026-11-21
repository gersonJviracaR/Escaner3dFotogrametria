package bo.edu.umss.escaner3d_app

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Build
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        val messenger = flutterEngine.dartExecutor.binaryMessenger
        val sensorManager = getSystemService(Context.SENSOR_SERVICE) as SensorManager

        EventChannel(messenger, "escaner3d/orientation")
            .setStreamHandler(OrientationStreamHandler(sensorManager))

        // Modelo y versión de Android para el manifiesto de la sesión.
        MethodChannel(messenger, "escaner3d/device").setMethodCallHandler { call, result ->
            if (call.method == "info") {
                result.success(
                    mapOf(
                        "model" to "${Build.MANUFACTURER} ${Build.MODEL}",
                        "android" to Build.VERSION.RELEASE,
                        "sdk" to Build.VERSION.SDK_INT,
                    )
                )
            } else {
                result.notImplemented()
            }
        }
    }
}

/**
 * Emite el cuaternio [w, x, y, z] de TYPE_GAME_ROTATION_VECTOR
 * (o TYPE_ROTATION_VECTOR si el primero no existe).
 */
private class OrientationStreamHandler(
    private val sensorManager: SensorManager,
) : EventChannel.StreamHandler, SensorEventListener {
    private var sink: EventChannel.EventSink? = null
    private val quaternion = FloatArray(4)

    override fun onListen(arguments: Any?, events: EventChannel.EventSink) {
        val sensor = sensorManager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
            ?: sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
        if (sensor == null) {
            events.error("NO_SENSOR", "El celular no tiene sensor de rotación", null)
            return
        }
        sink = events
        sensorManager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_UI)
    }

    override fun onCancel(arguments: Any?) {
        sensorManager.unregisterListener(this)
        sink = null
    }

    override fun onSensorChanged(event: SensorEvent) {
        // getQuaternionFromVector devuelve [w, x, y, z].
        SensorManager.getQuaternionFromVector(quaternion, event.values)
        sink?.success(quaternion.map { it.toDouble() })
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
}
