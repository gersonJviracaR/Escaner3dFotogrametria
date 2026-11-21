// Pantalla 1: dirección del servidor, prueba de conexión y nueva sesión (HU-04).
import 'dart:io';

import 'package:material_ui/material_ui.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../api/scanner_api.dart';
import '../platform/device_channels.dart';
import '../session/capture_session.dart';
import 'capture_screen.dart';

const _kServerAddressKey = 'server_address';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  final _addressController = TextEditingController();
  final _nameController = TextEditingController();
  String? _status;
  bool _statusOk = false;
  bool _busy = false;

  @override
  void initState() {
    super.initState();
    SharedPreferences.getInstance().then((prefs) {
      final saved = prefs.getString(_kServerAddressKey);
      if (saved != null && mounted) setState(() => _addressController.text = saved);
    });
  }

  @override
  void dispose() {
    _addressController.dispose();
    _nameController.dispose();
    super.dispose();
  }

  Future<ScannerApi?> _apiFromInput() async {
    try {
      final api = ScannerApi(_addressController.text);
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_kServerAddressKey, _addressController.text.trim());
      return api;
    } on FormatException catch (e) {
      _setStatus(e.message, ok: false);
      return null;
    }
  }

  void _setStatus(String text, {required bool ok}) {
    if (!mounted) return;
    setState(() {
      _status = text;
      _statusOk = ok;
    });
  }

  Future<void> _testConnection() async {
    final api = await _apiFromInput();
    if (api == null) return;
    setState(() => _busy = true);
    try {
      final version = await api.health();
      _setStatus('Conectado al servidor (versión $version)', ok: true);
    } on ApiException catch (e) {
      _setStatus('$e\nRevisa que la PC y el celular estén en la misma Wi-Fi y que el servidor esté encendido.',
          ok: false);
    } finally {
      api.close();
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _newSession() async {
    final api = await _apiFromInput();
    if (api == null) return;
    setState(() => _busy = true);
    try {
      final name = _nameController.text.trim().isEmpty ? 'objeto' : _nameController.text.trim();
      final id = await api.createSession(name, device: await deviceInfo());
      final docs = await getApplicationDocumentsDirectory();
      final dir = Directory('${docs.path}/sessions/$id');
      await dir.create(recursive: true);
      final session = CaptureSession(sessionId: id, directory: dir.path);
      _setStatus('Sesión creada: $id', ok: true);
      if (!mounted) return;
      await Navigator.of(context).push(MaterialPageRoute(
        builder: (_) => CaptureScreen(session: session, api: api),
      ));
    } on ApiException catch (e) {
      _setStatus('No se pudo crear la sesión: $e', ok: false);
      api.close();
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Escáner 3D')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          const Text(
            '1. Enciende el servidor en la PC y escribe aquí la dirección que muestra.\n'
            '2. Toma unas 20 fotos alrededor del objeto siguiendo la guía.\n'
            '3. Envía la sesión y repite las fotos que el servidor rechace.',
          ),
          const SizedBox(height: 20),
          TextField(
            controller: _addressController,
            keyboardType: TextInputType.url,
            decoration: const InputDecoration(
              labelText: 'Dirección del servidor (IP:puerto)',
              hintText: '192.168.1.10:8080',
              border: OutlineInputBorder(),
            ),
          ),
          const SizedBox(height: 12),
          OutlinedButton.icon(
            onPressed: _busy ? null : _testConnection,
            icon: const Icon(Icons.wifi_find),
            label: const Text('Probar conexión'),
          ),
          if (_status != null) ...[
            const SizedBox(height: 12),
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(_statusOk ? Icons.check_circle : Icons.error,
                    color: _statusOk ? Colors.green : Colors.red),
                const SizedBox(width: 8),
                Expanded(child: Text(_status!)),
              ],
            ),
          ],
          const Divider(height: 40),
          TextField(
            controller: _nameController,
            decoration: const InputDecoration(
              labelText: 'Nombre del objeto (opcional)',
              hintText: 'estatua01',
              border: OutlineInputBorder(),
            ),
          ),
          const SizedBox(height: 12),
          FilledButton.icon(
            onPressed: _busy ? null : _newSession,
            icon: const Icon(Icons.add_a_photo),
            label: const Text('Nueva sesión'),
          ),
          if (_busy) const Padding(padding: EdgeInsets.all(16), child: Center(child: CircularProgressIndicator())),
        ],
      ),
    );
  }
}
