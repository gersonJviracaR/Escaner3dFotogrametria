// Pruebas de HU-03: ángulo entre cuaternios y estados de la guía de captura.
import 'dart:math' as math;

import 'package:escaner3d_app/capture/capture_constants.dart';
import 'package:escaner3d_app/capture/capture_guide.dart';
import 'package:escaner3d_app/capture/quaternion.dart';
import 'package:flutter_test/flutter_test.dart';

/// Rotación de [degrees] grados alrededor del eje Z, como [w, x, y, z].
List<double> rotZ(double degrees) {
  final half = degrees * math.pi / 360.0;
  return [math.cos(half), 0, 0, math.sin(half)];
}

void main() {
  group('angleBetween', () {
    test('cuaternios iguales dan 0°', () {
      expect(angleBetween(rotZ(30), rotZ(30)), closeTo(0, 1e-6));
    });

    test('rotación conocida de 90° da 90°', () {
      expect(angleBetween([1, 0, 0, 0], rotZ(90)), closeTo(90, 1e-6));
    });

    test('q y -q representan la misma orientación (0°)', () {
      final q = rotZ(45);
      final neg = q.map((v) => -v).toList();
      expect(angleBetween(q, neg), closeTo(0, 1e-6));
    });

    test('normaliza cuaternios no unitarios', () {
      final scaled = rotZ(90).map((v) => v * 3).toList();
      expect(angleBetween([2, 0, 0, 0], scaled), closeTo(90, 1e-6));
    });

    test('180° es el máximo', () {
      expect(angleBetween([1, 0, 0, 0], rotZ(180)), closeTo(180, 1e-6));
      expect(angleBetween([1, 0, 0, 0], rotZ(270)), closeTo(90, 1e-6));
    });

    test('rechaza cuaternios mal formados', () {
      expect(() => angleBetween([1, 0, 0], [1, 0, 0, 0]), throwsArgumentError);
      expect(() => angleBetween([0, 0, 0, 0], [1, 0, 0, 0]), throwsArgumentError);
    });
  });

  group('rotationStateFor', () {
    test('estados en los umbrales', () {
      expect(rotationStateFor(0), RotationState.waitingForTurn);
      expect(rotationStateFor(14.9), RotationState.waitingForTurn);
      expect(rotationStateFor(15), RotationState.readyToCapture);
      expect(rotationStateFor(18), RotationState.readyToCapture);
      expect(rotationStateFor(25), RotationState.readyToCapture);
      expect(rotationStateFor(25.1), RotationState.turnedTooMuch);
    });
  });

  group('CaptureGuide', () {
    test('valores por defecto del sprint', () {
      expect(kTargetStepDegrees, 18);
      expect(kTargetPhotoCount, 20);
      expect(kMinPhotosToSend, 12);
    });

    test('la primera foto siempre está lista', () {
      final guide = CaptureGuide();
      expect(guide.update(rotZ(0)).state, RotationState.readyToCapture);
      expect(guide.photoCount, 0);
    });

    test('tras una foto mide el giro desde la última toma', () {
      final guide = CaptureGuide();
      guide.registerCapture(rotZ(0));
      expect(guide.update(rotZ(5)).state, RotationState.waitingForTurn);
      final status = guide.update(rotZ(18));
      expect(status.state, RotationState.readyToCapture);
      expect(status.degreesSinceLast, closeTo(18, 1e-6));
      expect(guide.update(rotZ(40)).state, RotationState.turnedTooMuch);
    });

    test('el giro se mide desde la última foto, no desde la primera', () {
      final guide = CaptureGuide();
      guide.registerCapture(rotZ(0));
      guide.registerCapture(rotZ(18));
      expect(guide.update(rotZ(36)).degreesSinceLast, closeTo(18, 1e-6));
    });

    test('contador, mínimo para enviar y completitud', () {
      final guide = CaptureGuide();
      for (var i = 0; i < 11; i++) {
        guide.registerCapture(rotZ(i * 18.0));
      }
      expect(guide.photoCount, 11);
      expect(guide.canSend, isFalse);
      guide.registerCapture(rotZ(11 * 18.0));
      expect(guide.canSend, isTrue);
      expect(guide.isComplete, isFalse);
      for (var i = 12; i < 20; i++) {
        guide.registerCapture(rotZ(i * 18.0));
      }
      expect(guide.photoCount, 20);
      expect(guide.isComplete, isTrue);
      expect(guide.update(rotZ(0)).isComplete, isTrue);
    });

    test('se puede capturar sin orientación (sin sensor)', () {
      final guide = CaptureGuide();
      guide.registerCapture(null);
      expect(guide.photoCount, 1);
      expect(guide.update(null).state, RotationState.unknown);
    });

    test('progreso total recorrido acumula el giro', () {
      final guide = CaptureGuide();
      guide.registerCapture(rotZ(0));
      guide.registerCapture(rotZ(18));
      guide.registerCapture(rotZ(36));
      expect(guide.totalDegrees, closeTo(36, 1e-6));
    });
  });
}
