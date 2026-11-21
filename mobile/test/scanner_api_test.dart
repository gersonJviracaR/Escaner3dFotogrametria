// Pruebas de HU-04: cliente HTTP de la app contra un servidor simulado.
import 'dart:convert';
import 'dart:io';

import 'package:escaner3d_app/api/scanner_api.dart';
import 'package:escaner3d_app/session/photo_metadata.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

http.Response jsonResponse(Object body, int status) =>
    http.Response(jsonEncode(body), status, headers: {'content-type': 'application/json'});

void main() {
  group('normalizeServerAddress', () {
    test('agrega http:// y quita la barra final', () {
      expect(normalizeServerAddress('192.168.1.5:8080'), 'http://192.168.1.5:8080');
      expect(normalizeServerAddress(' http://192.168.1.5:8080/ '), 'http://192.168.1.5:8080');
    });

    test('usa el puerto 8080 si no se indica', () {
      expect(normalizeServerAddress('192.168.1.5'), 'http://192.168.1.5:8080');
    });

    test('rechaza direcciones vacías o inválidas', () {
      expect(() => normalizeServerAddress(''), throwsFormatException);
      expect(() => normalizeServerAddress('http://'), throwsFormatException);
    });
  });

  group('ScannerApi', () {
    test('health devuelve la versión', () async {
      final api = ScannerApi('http://pc:8080', client: MockClient((req) async {
        expect(req.method, 'GET');
        expect(req.url.toString(), 'http://pc:8080/health');
        return jsonResponse({'status': 'ok', 'version': '0.1.0'}, 200);
      }));
      expect(await api.health(), '0.1.0');
    });

    test('createSession envía nombre y dispositivo', () async {
      final api = ScannerApi('http://pc:8080', client: MockClient((req) async {
        expect(req.url.path, '/sessions');
        final body = jsonDecode(req.body) as Map<String, dynamic>;
        expect(body['name'], 'taza');
        expect(body['device']['model'], 'Pixel');
        return jsonResponse({'session_id': '20261110-153000-taza'}, 201);
      }));
      expect(await api.createSession('taza', device: {'model': 'Pixel'}), '20261110-153000-taza');
    });

    test('uploadPhoto envía multipart con image y metadata y lee la validación', () async {
      final api = ScannerApi('http://pc:8080', client: MockClient((req) async {
        expect(req.url.path, '/sessions/s1/photos');
        expect(req.headers['content-type'], startsWith('multipart/form-data'));
        final raw = latin1.decode(req.bodyBytes);
        expect(raw, contains('name="image"'));
        expect(raw, contains('name="metadata"'));
        expect(raw, contains('"index":7'));
        return jsonResponse(
            {'index': 7, 'accepted': false, 'sharpness': 41.2, 'overexposed_ratio': 0.01, 'reasons': ['borrosa']},
            200);
      }));
      final v = await api.uploadPhoto(
        's1',
        [0xFF, 0xD8, 0xFF, 0xD9],
        PhotoMetadata(index: 7, capturedAt: DateTime(2026), width: 1, height: 1),
      );
      expect(v.index, 7);
      expect(v.accepted, isFalse);
      expect(v.reasons, ['borrosa']);
      expect(v.reasonsText, 'borrosa');
      expect(v.sharpness, closeTo(41.2, 1e-9));
    });

    test('complete devuelve el resumen con las rechazadas', () async {
      final api = ScannerApi('http://pc:8080', client: MockClient((req) async {
        expect(req.url.path, '/sessions/s1/complete');
        return jsonResponse({'total': 20, 'accepted': 19, 'rejected': [7]}, 200);
      }));
      final s = await api.complete('s1');
      expect(s.total, 20);
      expect(s.accepted, 19);
      expect(s.rejected, [7]);
    });

    test('un error HTTP del servidor se convierte en ApiException con su mensaje', () async {
      final api = ScannerApi('http://pc:8080', client: MockClient((req) async {
        return jsonResponse({'error': 'la sesión no existe'}, 404);
      }));
      expect(
        () => api.complete('nada'),
        throwsA(isA<ApiException>()
            .having((e) => e.statusCode, 'statusCode', 404)
            .having((e) => e.message, 'message', contains('no existe'))),
      );
    });

    test('un error sin JSON igual da un mensaje legible', () async {
      final api = ScannerApi('http://pc:8080', client: MockClient((req) async => http.Response('boom', 500)));
      expect(() => api.health(), throwsA(isA<ApiException>().having((e) => e.statusCode, 'statusCode', 500)));
    });

    test('error de conexión se reporta como ApiException sin código', () async {
      final api = ScannerApi('http://pc:8080', client: MockClient((req) async {
        throw const SocketException('Connection refused');
      }));
      expect(
        () => api.health(),
        throwsA(isA<ApiException>()
            .having((e) => e.statusCode, 'statusCode', isNull)
            .having((e) => e.message, 'message', contains('No se pudo conectar'))),
      );
    });
  });
}
