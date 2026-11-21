// Sesión de captura en el celular: fotos guardadas localmente y su estado de envío.
import '../api/scanner_api.dart';
import 'photo_metadata.dart';

/// Nombre de archivo local de cada foto: img_001.jpg, img_002.jpg...
String photoFileName(int index) => 'img_${index.toString().padLeft(3, '0')}.jpg';

class SessionPhoto {
  SessionPhoto({required this.path, required this.metadata, this.warning});

  String path;
  PhotoMetadata metadata;

  /// Advertencia al leer el EXIF (p. ej. sin focal).
  String? warning;

  /// Resultado del servidor; null si aún no se envió esta versión de la foto.
  PhotoValidation? validation;

  int get index => metadata.index;
}

class CaptureSession {
  CaptureSession({required this.sessionId, required this.directory});

  final String sessionId;
  final String directory;
  final List<SessionPhoto> photos = [];

  int get nextIndex => photos.isEmpty ? 1 : photos.map((p) => p.index).reduce((a, b) => a > b ? a : b) + 1;

  /// Agrega una foto nueva o reemplaza la del mismo índice (repetir toma).
  void putPhoto(SessionPhoto photo) {
    final i = photos.indexWhere((p) => p.index == photo.index);
    if (i >= 0) {
      photos[i] = photo;
    } else {
      photos.add(photo);
      photos.sort((a, b) => a.index.compareTo(b.index));
    }
  }

  List<SessionPhoto> get pendingUpload => photos.where((p) => p.validation == null).toList();

  List<SessionPhoto> get rejected => photos.where((p) => p.validation != null && !p.validation!.accepted).toList();
}
