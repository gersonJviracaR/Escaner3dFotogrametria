// Pantalla 3: envío por Wi-Fi con progreso, reintento y fotos rechazadas (HU-04, HU-06).
import 'dart:io';

import 'package:material_ui/material_ui.dart';

import '../api/scanner_api.dart';
import '../session/capture_session.dart';
import 'capture_screen.dart';

class SendScreen extends StatefulWidget {
  const SendScreen({super.key, required this.session, required this.api});

  final CaptureSession session;
  final ScannerApi api;

  @override
  State<SendScreen> createState() => _SendScreenState();
}

class _SendScreenState extends State<SendScreen> {
  bool _sending = false;
  int _done = 0;
  int _toSend = 0;
  String? _error;
  SessionSummary? _summary;
  Duration? _elapsed;

  @override
  void initState() {
    super.initState();
    _send();
  }

  /// Sube las fotos pendientes una a una y cierra la sesión. Si falla, se puede reintentar
  /// y solo se reenvían las fotos que faltan.
  Future<void> _send() async {
    final pending = widget.session.pendingUpload;
    setState(() {
      _sending = true;
      _error = null;
      _summary = null;
      _done = 0;
      _toSend = pending.length;
    });
    final watch = Stopwatch()..start();
    try {
      for (final photo in pending) {
        final bytes = await File(photo.path).readAsBytes();
        photo.validation = await widget.api.uploadPhoto(widget.session.sessionId, bytes, photo.metadata);
        if (mounted) setState(() => _done++);
      }
      final summary = await widget.api.complete(widget.session.sessionId);
      watch.stop();
      if (mounted) {
        setState(() {
          _summary = summary;
          _elapsed = watch.elapsed;
        });
      }
    } on ApiException catch (e) {
      if (mounted) setState(() => _error = e.toString());
    } on FileSystemException catch (e) {
      if (mounted) setState(() => _error = 'No se pudo leer una foto: ${e.message}');
    } finally {
      if (mounted) setState(() => _sending = false);
    }
  }

  Future<void> _retakeRejected() async {
    final indices = widget.session.rejected.map((p) => p.index).toList();
    final retaken = await Navigator.of(context).push<bool>(MaterialPageRoute(
      builder: (_) => CaptureScreen(session: widget.session, api: widget.api, retakeIndices: indices),
    ));
    if (retaken == true && mounted) await _send();
  }

  @override
  Widget build(BuildContext context) {
    final rejected = widget.session.rejected;
    return Scaffold(
      appBar: AppBar(title: const Text('Envío de la sesión')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          Text('Sesión: ${widget.session.sessionId}', style: const TextStyle(fontWeight: FontWeight.bold)),
          const SizedBox(height: 16),
          if (_sending) ...[
            Text('Enviando foto ${_done + (_done < _toSend ? 1 : 0)} de $_toSend…'),
            const SizedBox(height: 8),
            LinearProgressIndicator(value: _toSend == 0 ? null : _done / _toSend),
          ],
          if (_error != null) ...[
            Row(children: [
              const Icon(Icons.error, color: Colors.red),
              const SizedBox(width: 8),
              Expanded(child: Text('Error en el envío: $_error')),
            ]),
            const SizedBox(height: 12),
            FilledButton.icon(onPressed: _send, icon: const Icon(Icons.refresh), label: const Text('Reintentar')),
          ],
          if (_summary != null) ...[
            Row(children: [
              Icon(rejected.isEmpty ? Icons.check_circle : Icons.warning,
                  color: rejected.isEmpty ? Colors.green : Colors.orange),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  'Servidor: ${_summary!.accepted} de ${_summary!.total} fotos aceptadas'
                  '${_elapsed == null ? '' : ' (envío: ${_elapsed!.inSeconds} s)'}',
                  style: const TextStyle(fontSize: 16),
                ),
              ),
            ]),
            const SizedBox(height: 16),
            if (rejected.isEmpty)
              const Text('Todas las fotos pasaron el control de calidad. La sesión quedó guardada en el servidor.')
            else ...[
              const Text('Fotos rechazadas (repítelas antes de reconstruir):',
                  style: TextStyle(fontWeight: FontWeight.bold)),
              for (final p in rejected)
                ListTile(
                  leading: Image.file(File(p.path), width: 56, height: 56, fit: BoxFit.cover, cacheWidth: 112),
                  title: Text('Foto ${p.index}: ${p.validation!.reasonsText}'),
                  subtitle: Text('Nitidez ${p.validation!.sharpness.toStringAsFixed(0)} · '
                      'saturación ${(p.validation!.overexposedRatio * 100).toStringAsFixed(1)} %'),
                ),
              const SizedBox(height: 12),
              FilledButton.icon(
                onPressed: _retakeRejected,
                icon: const Icon(Icons.replay),
                label: Text('Repetir ${rejected.length} foto(s) rechazada(s)'),
              ),
            ],
          ],
        ],
      ),
    );
  }
}
