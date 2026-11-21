// Pruebas del estado local de la sesión (repetir tomas con el mismo índice).
import 'package:escaner3d_app/api/scanner_api.dart';
import 'package:escaner3d_app/session/capture_session.dart';
import 'package:escaner3d_app/session/photo_metadata.dart';
import 'package:flutter_test/flutter_test.dart';

SessionPhoto photo(int index) => SessionPhoto(
      path: '/tmp/${photoFileName(index)}',
      metadata: PhotoMetadata(index: index, capturedAt: DateTime(2026), width: 1, height: 1),
    );

PhotoValidation validation(int index, bool accepted) => PhotoValidation(
      index: index,
      accepted: accepted,
      sharpness: accepted ? 150 : 40,
      overexposedRatio: 0,
      reasons: accepted ? [] : ['borrosa'],
    );

void main() {
  test('nombres de archivo con tres dígitos', () {
    expect(photoFileName(1), 'img_001.jpg');
    expect(photoFileName(20), 'img_020.jpg');
  });

  test('índices consecutivos y fotos ordenadas', () {
    final s = CaptureSession(sessionId: 's', directory: '/tmp');
    expect(s.nextIndex, 1);
    s.putPhoto(photo(2));
    s.putPhoto(photo(1));
    expect(s.nextIndex, 3);
    expect(s.photos.map((p) => p.index), [1, 2]);
  });

  test('repetir una toma reemplaza la foto y la vuelve pendiente de envío', () {
    final s = CaptureSession(sessionId: 's', directory: '/tmp');
    s.putPhoto(photo(1)..validation = validation(1, true));
    s.putPhoto(photo(2)..validation = validation(2, false));
    expect(s.rejected.map((p) => p.index), [2]);
    expect(s.pendingUpload, isEmpty);

    s.putPhoto(photo(2));
    expect(s.photos.length, 2);
    expect(s.rejected, isEmpty);
    expect(s.pendingUpload.map((p) => p.index), [2]);
  });
}
