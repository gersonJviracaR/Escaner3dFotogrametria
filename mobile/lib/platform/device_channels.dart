// Canales nativos (Kotlin, MainActivity.kt): orientación del celular y datos del dispositivo.
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

const _orientationChannel = EventChannel('escaner3d/orientation');
const _deviceChannel = MethodChannel('escaner3d/device');

/// Cuaternios [w, x, y, z] del sensor TYPE_GAME_ROTATION_VECTOR.
Stream<List<double>> orientationStream() => _orientationChannel
    .receiveBroadcastStream()
    .map((event) => (event as List).map((v) => (v as num).toDouble()).toList());

/// {"model": "...", "android": "..."}; vacío si falla.
Future<Map<String, dynamic>> deviceInfo() async {
  try {
    final info = await _deviceChannel.invokeMapMethod<String, dynamic>('info');
    return info ?? {};
  } on PlatformException catch (e) {
    debugPrint('No se pudo leer la información del dispositivo: $e');
    return {};
  }
}
