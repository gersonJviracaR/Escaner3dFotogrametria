// Escáner 3D por Fotogrametría — app de captura (Sprint 1).
// El celular solo captura y envía; todo el procesamiento ocurre en el servidor C++.
import 'package:material_ui/material_ui.dart';

import 'screens/home_screen.dart';

void main() {
  runApp(const ScannerApp());
}

class ScannerApp extends StatelessWidget {
  const ScannerApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Escáner 3D',
      theme: ThemeData(colorSchemeSeed: Colors.indigo, useMaterial3: true),
      home: const HomeScreen(),
    );
  }
}
