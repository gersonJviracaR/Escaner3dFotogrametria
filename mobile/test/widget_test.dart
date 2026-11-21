// Prueba de humo de la pantalla de inicio.
import 'package:escaner3d_app/main.dart';
import 'package:material_ui/material_ui.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  testWidgets('la pantalla de inicio carga la dirección guardada y muestra las acciones', (tester) async {
    SharedPreferences.setMockInitialValues({'server_address': '10.0.0.7:8080'});
    await tester.pumpWidget(const ScannerApp());
    await tester.pumpAndSettle();

    expect(find.text('Escáner 3D'), findsOneWidget);
    expect(find.text('Probar conexión'), findsOneWidget);
    expect(find.text('Nueva sesión'), findsOneWidget);
    final address = tester.widget<TextField>(find.byType(TextField).first);
    expect(address.controller!.text, '10.0.0.7:8080');
  });

  testWidgets('una dirección vacía muestra un error sin intentar conectar', (tester) async {
    SharedPreferences.setMockInitialValues({});
    await tester.pumpWidget(const ScannerApp());
    await tester.pumpAndSettle();
    await tester.tap(find.text('Probar conexión'));
    await tester.pumpAndSettle();
    expect(find.textContaining('Escribe la dirección'), findsOneWidget);
  });
}
