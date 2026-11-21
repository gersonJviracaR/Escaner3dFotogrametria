// Pruebas de HU-02: serialización de metadatos y conversión de EXIF.
import 'package:escaner3d_app/session/exif_focal.dart';
import 'package:escaner3d_app/session/photo_metadata.dart';
import 'package:exif/exif.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('PhotoMetadata.toJson', () {
    test('produce el formato del contrato HTTP', () {
      final m = PhotoMetadata(
        index: 1,
        capturedAt: DateTime(2026, 11, 10, 15, 31, 2),
        width: 4000,
        height: 3000,
        focalMm: 5.6,
        focal35mm: 24,
        orientationQ: [0.98, 0.01, 0.17, 0.02],
      );
      final j = m.toJson();
      expect(j['index'], 1);
      expect(j['width'], 4000);
      expect(j['height'], 3000);
      expect(j['focal_mm'], 5.6);
      expect(j['focal_35mm'], 24);
      expect(j['orientation_q'], [0.98, 0.01, 0.17, 0.02]);
      expect(j['captured_at'], startsWith('2026-11-10T15:31:02'));
    });

    test('focales y orientación ausentes se envían como null', () {
      final j = PhotoMetadata(index: 3, capturedAt: DateTime(2026), width: 0, height: 0).toJson();
      expect(j.containsKey('focal_mm'), isTrue);
      expect(j['focal_mm'], isNull);
      expect(j['focal_35mm'], isNull);
      expect(j['orientation_q'], isNull);
    });

    test('copyWith conserva el índice al repetir una toma', () {
      final m = PhotoMetadata(index: 7, capturedAt: DateTime(2026), width: 1, height: 1);
      expect(m.copyWith(width: 10).index, 7);
      expect(m.copyWith(width: 10).width, 10);
    });
  });

  group('formatIsoWithOffset', () {
    test('incluye el desfase horario ±HH:MM', () {
      final s = formatIsoWithOffset(DateTime(2026, 11, 10, 15, 30));
      expect(s, matches(RegExp(r'^2026-11-10T15:30:00[+-]\d\d:\d\d$')));
    });

    test('UTC termina en +00:00', () {
      expect(formatIsoWithOffset(DateTime.utc(2026, 1, 2, 3, 4, 5)), '2026-01-02T03:04:05+00:00');
    });
  });

  group('exifValueToDouble', () {
    test('convierte una razón EXIF (FocalLength 56/10 = 5.6 mm)', () {
      expect(exifValueToDouble(IfdRatios([Ratio(56, 10)])), closeTo(5.6, 1e-9));
      expect(exifValueToDouble(IfdRatios([Ratio(4730, 1000)])), closeTo(4.73, 1e-9));
    });

    test('convierte un entero EXIF (FocalLengthIn35mmFilm = 24)', () {
      expect(exifValueToDouble(IfdInts([24])), 24.0);
    });

    test('valores ausentes, vacíos, cero o con denominador cero dan null', () {
      expect(exifValueToDouble(null), isNull);
      expect(exifValueToDouble(IfdNone()), isNull);
      expect(exifValueToDouble(IfdInts([])), isNull);
      expect(exifValueToDouble(IfdInts([0])), isNull);
      expect(exifValueToDouble(IfdRatios([Ratio(5, 0)])), isNull);
    });
  });

  group('focalFromExifTags', () {
    IfdTag tag(IfdValues v) => IfdTag(tag: 0, tagType: 'Ratio', printable: '', values: v);

    test('lee ambas focales cuando existen', () {
      final f = focalFromExifTags({
        'EXIF FocalLength': tag(IfdRatios([Ratio(56, 10)])),
        'EXIF FocalLengthIn35mmFilm': tag(IfdInts([24])),
      });
      expect(f.focalMm, closeTo(5.6, 1e-9));
      expect(f.focal35mm, 24);
      expect(f.warning, isNull);
    });

    test('sin EXIF deja null y registra una advertencia', () {
      final f = focalFromExifTags({});
      expect(f.focalMm, isNull);
      expect(f.focal35mm, isNull);
      expect(f.warning, isNotNull);
    });
  });
}
