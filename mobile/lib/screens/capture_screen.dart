// Pantalla 2: captura con vista previa, guía de giro y contador (HU-01, HU-02, HU-03).
import 'dart:async';
import 'dart:io';

import 'package:camera/camera.dart';
import 'package:material_ui/material_ui.dart';

import '../api/scanner_api.dart';
import '../capture/capture_constants.dart';
import '../capture/capture_guide.dart';
import '../platform/device_channels.dart';
import '../session/capture_session.dart';
import '../session/exif_focal.dart';
import '../session/photo_metadata.dart';
import 'send_screen.dart';

class CaptureScreen extends StatefulWidget {
  const CaptureScreen({super.key, required this.session, required this.api, this.retakeIndices});

  final CaptureSession session;
  final ScannerApi api;

  /// Si no es null, la pantalla solo repite estas tomas (mismos índices) y luego vuelve.
  final List<int>? retakeIndices;

  @override
  State<CaptureScreen> createState() => _CaptureScreenState();
}

class _CaptureScreenState extends State<CaptureScreen> {
  CameraController? _camera;
  String? _cameraError;
  StreamSubscription<List<double>>? _orientationSub;
  List<double>? _orientation;
  bool _sensorMissing = false;
  final _guide = CaptureGuide();
  bool _capturing = false;
  String? _lastWarning;
  String? _lastInfo;
  late final List<int> _pendingRetakes = List.of(widget.retakeIndices ?? const []);

  bool get _isRetakeMode => widget.retakeIndices != null;

  @override
  void initState() {
    super.initState();
    // Al volver de repetir tomas la guía sigue contando las fotos existentes.
    for (final p in widget.session.photos) {
      _guide.registerCapture(p.metadata.orientationQ);
    }
    _initCamera();
    _orientationSub = orientationStream().listen(
      (q) => setState(() => _orientation = q),
      onError: (Object e) => setState(() => _sensorMissing = true),
    );
  }

  Future<void> _initCamera() async {
    try {
      final cameras = await availableCameras();
      final back = cameras.firstWhere(
        (c) => c.lensDirection == CameraLensDirection.back,
        orElse: () => cameras.first,
      );
      final controller = CameraController(
        back,
        // Máxima resolución del sensor: con veryHigh el Poco X3 Pro entregaba solo 1920x1080,
        // poco detalle para la reconstrucción (Sprints 2-4).
        ResolutionPreset.max,
        enableAudio: false,
        imageFormatGroup: ImageFormatGroup.jpeg,
      );
      await controller.initialize();
      await controller.setFlashMode(FlashMode.off);
      if (!mounted) {
        await controller.dispose();
        return;
      }
      setState(() => _camera = controller);
    } on CameraException catch (e) {
      setState(() => _cameraError = e.code == 'CameraAccessDenied'
          ? 'La app necesita permiso de cámara. Actívalo en Ajustes > Apps > Escáner 3D > Permisos.'
          : 'No se pudo abrir la cámara: ${e.description ?? e.code}');
    } on StateError {
      setState(() => _cameraError = 'El celular no tiene cámara disponible.');
    }
  }

  @override
  void dispose() {
    _orientationSub?.cancel();
    _camera?.dispose();
    super.dispose();
  }

  Future<void> _capture() async {
    final camera = _camera;
    if (camera == null || _capturing || camera.value.isTakingPicture) return;
    if (_isRetakeMode && _pendingRetakes.isEmpty) return;
    setState(() => _capturing = true);

    // La orientación se toma justo al disparar.
    final orientation = _orientation == null ? null : List<double>.of(_orientation!);
    final capturedAt = DateTime.now();
    final index = _isRetakeMode ? _pendingRetakes.first : widget.session.nextIndex;

    try {
      final shot = await camera.takePicture();
      final path = '${widget.session.directory}/${photoFileName(index)}';
      await shot.saveTo(path);
      // Al repetir una toma el archivo se sobrescribe: evita mostrar la miniatura vieja.
      PaintingBinding.instance.imageCache.clear();
      final bytes = await File(path).readAsBytes();
      final exif = await readExifInfo(bytes);
      if (exif.warning != null) debugPrint('Foto $index: ${exif.warning}');

      final metadata = PhotoMetadata(
        index: index,
        capturedAt: capturedAt,
        width: exif.width,
        height: exif.height,
        focalMm: exif.focalMm,
        focal35mm: exif.focal35mm,
        orientationQ: orientation,
      );
      widget.session.putPhoto(SessionPhoto(path: path, metadata: metadata, warning: exif.warning));
      if (!_isRetakeMode) _guide.registerCapture(orientation);

      setState(() {
        _lastWarning = exif.warning;
        _lastInfo = 'Última foto: ${exif.width}×${exif.height}, '
            '${(bytes.length / (1024 * 1024)).toStringAsFixed(1)} MB';
        if (_isRetakeMode) _pendingRetakes.removeAt(0);
      });
      if (_isRetakeMode && _pendingRetakes.isEmpty && mounted) Navigator.of(context).pop(true);
    } on CameraException catch (e) {
      _showMessage('No se pudo tomar la foto: ${e.description ?? e.code}');
    } finally {
      if (mounted) setState(() => _capturing = false);
    }
  }

  void _showMessage(String text) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(text)));
  }

  Future<void> _goToSend() async {
    await Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => SendScreen(session: widget.session, api: widget.api),
    ));
    if (mounted) setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text(_isRetakeMode ? 'Repetir fotos' : 'Captura')),
      body: Column(
        children: [
          Expanded(child: _buildPreview()),
          _buildGuidePanel(),
          _buildControls(),
        ],
      ),
    );
  }

  Widget _buildPreview() {
    if (_cameraError != null) {
      return Center(child: Padding(padding: const EdgeInsets.all(24), child: Text(_cameraError!)));
    }
    final camera = _camera;
    if (camera == null || !camera.value.isInitialized) {
      return const Center(child: CircularProgressIndicator());
    }
    return Center(child: CameraPreview(camera));
  }

  Widget _buildGuidePanel() {
    final count = widget.session.photos.length;
    if (_isRetakeMode) {
      final text = _pendingRetakes.isEmpty
          ? 'Listo'
          : 'Repite la foto ${_pendingRetakes.first} desde la misma posición '
              '(quedan ${_pendingRetakes.length}). Sostén el celular firme.';
      return _panel(Colors.blueGrey, text);
    }

    final status = _guide.update(_orientation);
    final degrees = status.degreesSinceLast.toStringAsFixed(0);
    final (Color color, String text) = switch (status.state) {
      RotationState.waitingForTurn =>
        (Colors.orange, 'Sigue girando alrededor del objeto: $degrees° de ${kTargetStepDegrees.toStringAsFixed(0)}°'),
      RotationState.readyToCapture =>
        (Colors.green, count == 0 ? 'Listo: toma la primera foto' : 'Listo para tomar la foto ($degrees°)'),
      RotationState.turnedTooMuch =>
        (Colors.red, 'Giraste demasiado ($degrees°): vuelve un poco atrás'),
      RotationState.unknown => (
          Colors.blueGrey,
          _sensorMissing
              ? 'Este celular no tiene sensor de giro: avanza unos ${kTargetStepDegrees.toStringAsFixed(0)}° por foto'
              : 'Esperando el sensor de giro…'
        ),
    };
    final complete = status.isComplete
        ? '\n¡Sesión completa! Ya tienes $kTargetPhotoCount fotos: puedes enviarla.'
        : '';
    return _panel(color, '$text$complete');
  }

  Widget _panel(Color color, String text) {
    final count = widget.session.photos.length;
    return Container(
      width: double.infinity,
      color: color.withValues(alpha: 0.15),
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      child: Row(
        children: [
          Container(width: 14, height: 14, decoration: BoxDecoration(color: color, shape: BoxShape.circle)),
          const SizedBox(width: 10),
          Expanded(child: Text(text, style: const TextStyle(fontSize: 16))),
          const SizedBox(width: 10),
          Text('$count / $kTargetPhotoCount',
              style: const TextStyle(fontSize: 22, fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }

  Widget _buildControls() {
    final last = widget.session.photos.isEmpty
        ? null
        : widget.session.photos.reduce((a, b) => a.metadata.capturedAt.isAfter(b.metadata.capturedAt) ? a : b);
    final canSend = widget.session.photos.length >= kMinPhotosToSend;
    return SafeArea(
      top: false,
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (_lastInfo != null) Text(_lastInfo!, style: const TextStyle(fontSize: 12, color: Colors.black54)),
            if (_lastWarning != null)
              Text('Aviso: $_lastWarning', style: const TextStyle(fontSize: 12, color: Colors.black54)),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                SizedBox(
                  width: 64,
                  height: 64,
                  child: last == null
                      ? const DecoratedBox(decoration: BoxDecoration(color: Colors.black12))
                      : Image.file(File(last.path), fit: BoxFit.cover, cacheWidth: 128, key: ValueKey(last.metadata.capturedAt)),
                ),
                // El botón de captura nunca se bloquea por la guía (es orientativa).
                SizedBox(
                  width: 80,
                  height: 80,
                  child: FilledButton(
                    onPressed: _camera == null || _capturing ? null : _capture,
                    style: FilledButton.styleFrom(shape: const CircleBorder()),
                    child: _capturing
                        ? const CircularProgressIndicator(color: Colors.white)
                        : const Icon(Icons.camera_alt, size: 36),
                  ),
                ),
                SizedBox(
                  width: 96,
                  child: _isRetakeMode
                      ? const SizedBox.shrink()
                      : OutlinedButton(
                          onPressed: canSend ? _goToSend : null,
                          child: Text(canSend ? 'Enviar sesión' : 'Mín. $kMinPhotosToSend', textAlign: TextAlign.center),
                        ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
