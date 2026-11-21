// Lectura de la distancia focal y la resolución desde el EXIF del JPEG (HU-02).
import 'package:exif/exif.dart';

class ExifInfo {
  const ExifInfo({this.focalMm, this.focal35mm, this.width = 0, this.height = 0, this.warning});

  final double? focalMm;
  final double? focal35mm;

  /// 0 si el EXIF no la trae (el servidor usa las dimensiones reales de la imagen).
  final int width;
  final int height;

  /// Mensaje si faltó algún dato.
  final String? warning;
}

/// Primer valor de un campo EXIF como número; null si no existe o no es válido.
double? exifValueToDouble(IfdValues? values) {
  if (values == null || values.length == 0) return null;
  final first = values.toList().first;
  double? v;
  if (first is Ratio) {
    if (first.denominator == 0) return null;
    v = first.numerator / first.denominator;
  } else if (first is num) {
    v = first.toDouble();
  }
  return (v == null || v <= 0) ? null : v;
}

ExifInfo focalFromExifTags(Map<String, IfdTag> tags) {
  final focal = exifValueToDouble(tags['EXIF FocalLength']?.values);
  final focal35 = exifValueToDouble(tags['EXIF FocalLengthIn35mmFilm']?.values);
  final width = exifValueToDouble(tags['EXIF ExifImageWidth']?.values)?.round() ?? 0;
  final height = exifValueToDouble(tags['EXIF ExifImageLength']?.values)?.round() ?? 0;

  final missing = <String>[
    if (focal == null) 'FocalLength',
    if (focal35 == null) 'FocalLengthIn35mmFilm',
  ];
  return ExifInfo(
    focalMm: focal,
    focal35mm: focal35,
    width: width,
    height: height,
    warning: missing.isEmpty ? null : 'El EXIF no trae ${missing.join(' ni ')}; se envía null',
  );
}

Future<ExifInfo> readExifInfo(List<int> jpegBytes) async {
  try {
    return focalFromExifTags(await readExifFromBytes(jpegBytes, details: false));
  } catch (e) {
    return ExifInfo(warning: 'No se pudo leer el EXIF: $e');
  }
}
