// Metadatos crudos de cada foto (HU-02). La matriz K NO se calcula aquí.

/// Fecha local en ISO 8601 con desfase, p. ej. "2026-11-10T15:31:02-04:00".
String formatIsoWithOffset(DateTime t) {
  String two(int v) => v.toString().padLeft(2, '0');
  final offset = t.timeZoneOffset;
  final sign = offset.isNegative ? '-' : '+';
  final abs = offset.abs();
  return '${t.year.toString().padLeft(4, '0')}-${two(t.month)}-${two(t.day)}'
      'T${two(t.hour)}:${two(t.minute)}:${two(t.second)}'
      '$sign${two(abs.inHours)}:${two(abs.inMinutes % 60)}';
}

class PhotoMetadata {
  const PhotoMetadata({
    required this.index,
    required this.capturedAt,
    required this.width,
    required this.height,
    this.focalMm,
    this.focal35mm,
    this.orientationQ,
  });

  final int index;
  final DateTime capturedAt;
  final int width;
  final int height;
  final double? focalMm;
  final double? focal35mm;

  /// Cuaternio [w, x, y, z] del sensor de rotación al disparar.
  final List<double>? orientationQ;

  PhotoMetadata copyWith({
    DateTime? capturedAt,
    int? width,
    int? height,
    double? focalMm,
    double? focal35mm,
    List<double>? orientationQ,
  }) =>
      PhotoMetadata(
        index: index,
        capturedAt: capturedAt ?? this.capturedAt,
        width: width ?? this.width,
        height: height ?? this.height,
        focalMm: focalMm ?? this.focalMm,
        focal35mm: focal35mm ?? this.focal35mm,
        orientationQ: orientationQ ?? this.orientationQ,
      );

  Map<String, dynamic> toJson() => {
        'index': index,
        'captured_at': formatIsoWithOffset(capturedAt),
        'width': width,
        'height': height,
        'focal_mm': focalMm,
        'focal_35mm': focal35mm,
        'orientation_q': orientationQ,
      };
}
