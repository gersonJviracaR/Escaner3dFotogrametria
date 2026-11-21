// Operaciones con cuaternios [w, x, y, z]. Dart puro, sin Flutter.
import 'dart:math' as math;

List<double> _normalized(List<double> q) {
  if (q.length != 4) {
    throw ArgumentError('Un cuaternio debe tener 4 componentes [w, x, y, z]');
  }
  final norm = math.sqrt(q.fold<double>(0, (s, v) => s + v * v));
  if (norm < 1e-12) throw ArgumentError('Cuaternio nulo');
  return q.map((v) => v / norm).toList();
}

/// Ángulo de rotación entre dos orientaciones, en grados (0 a 180).
/// `2·acos(|q1·q2|)`; `q` y `-q` representan la misma orientación.
double angleBetween(List<double> q1, List<double> q2) {
  final a = _normalized(q1);
  final b = _normalized(q2);
  var dot = 0.0;
  for (var i = 0; i < 4; i++) {
    dot += a[i] * b[i];
  }
  final clamped = dot.abs().clamp(0.0, 1.0);
  return 2 * math.acos(clamped) * 180 / math.pi;
}
