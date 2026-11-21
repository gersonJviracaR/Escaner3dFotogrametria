// Guía de captura (HU-03): indica cuándo girar ≈18° entre tomas. Dart puro.
import 'capture_constants.dart';
import 'quaternion.dart';

enum RotationState {
  /// Giro menor que el mínimo: seguir girando.
  waitingForTurn,

  /// Giro dentro del rango recomendado: tomar la foto.
  readyToCapture,

  /// Se giró más de lo recomendado: el solapamiento puede ser insuficiente.
  turnedTooMuch,

  /// No hay datos de orientación (el celular no tiene el sensor).
  unknown,
}

RotationState rotationStateFor(double degrees) {
  if (degrees < kMinStepDegrees) return RotationState.waitingForTurn;
  if (degrees <= kMaxStepDegrees) return RotationState.readyToCapture;
  return RotationState.turnedTooMuch;
}

class GuideStatus {
  const GuideStatus({
    required this.state,
    required this.degreesSinceLast,
    required this.photoCount,
    required this.isComplete,
  });

  final RotationState state;
  final double degreesSinceLast;
  final int photoCount;
  final bool isComplete;
}

/// La guía es orientativa: nunca bloquea la captura.
class CaptureGuide {
  List<double>? _lastCaptured;
  int _photoCount = 0;
  double _totalDegrees = 0;

  int get photoCount => _photoCount;
  double get totalDegrees => _totalDegrees;
  bool get canSend => _photoCount >= kMinPhotosToSend;
  bool get isComplete => _photoCount >= kTargetPhotoCount;

  /// Estado para la orientación actual del celular.
  GuideStatus update(List<double>? current) {
    if (current == null) return _status(RotationState.unknown, 0);
    final last = _lastCaptured;
    if (last == null) return _status(RotationState.readyToCapture, 0);
    final degrees = angleBetween(last, current);
    return _status(rotationStateFor(degrees), degrees);
  }

  /// Registra que se tomó una foto con la orientación dada.
  void registerCapture(List<double>? orientation) {
    final last = _lastCaptured;
    if (last != null && orientation != null) {
      _totalDegrees += angleBetween(last, orientation);
    }
    if (orientation != null) _lastCaptured = List.of(orientation);
    _photoCount++;
  }

  GuideStatus _status(RotationState state, double degrees) => GuideStatus(
        state: state,
        degreesSinceLast: degrees,
        photoCount: _photoCount,
        isComplete: isComplete,
      );
}
