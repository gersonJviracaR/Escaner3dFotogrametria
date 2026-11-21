// Cliente HTTP del servidor local (contrato en docs/api.md) — HU-04.
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;

import '../session/photo_metadata.dart';

const int kDefaultServerPort = 8080;

class ApiException implements Exception {
  const ApiException(this.message, {this.statusCode});

  final String message;

  /// null si no hubo respuesta (error de red).
  final int? statusCode;

  @override
  String toString() => statusCode == null ? message : '$message (HTTP $statusCode)';
}

/// "192.168.1.5:8080" → "http://192.168.1.5:8080". Lanza FormatException si no es válida.
String normalizeServerAddress(String input) {
  var text = input.trim();
  if (text.isEmpty) throw const FormatException('Escribe la dirección IP:puerto del servidor');
  if (!text.contains('://')) text = 'http://$text';
  final uri = Uri.tryParse(text);
  if (uri == null || uri.host.isEmpty) throw FormatException('Dirección inválida: $input');
  final port = uri.hasPort ? uri.port : kDefaultServerPort;
  return '${uri.scheme}://${uri.host}:$port';
}

class PhotoValidation {
  const PhotoValidation({
    required this.index,
    required this.accepted,
    required this.sharpness,
    required this.overexposedRatio,
    required this.reasons,
  });

  factory PhotoValidation.fromJson(Map<String, dynamic> j) => PhotoValidation(
        index: j['index'] as int,
        accepted: j['accepted'] as bool,
        sharpness: (j['sharpness'] as num).toDouble(),
        overexposedRatio: (j['overexposed_ratio'] as num).toDouble(),
        reasons: (j['reasons'] as List).cast<String>(),
      );

  final int index;
  final bool accepted;
  final double sharpness;
  final double overexposedRatio;
  final List<String> reasons;

  String get reasonsText => reasons.join(' y ');
}

class SessionSummary {
  const SessionSummary({required this.total, required this.accepted, required this.rejected});

  factory SessionSummary.fromJson(Map<String, dynamic> j) => SessionSummary(
        total: j['total'] as int,
        accepted: j['accepted'] as int,
        rejected: (j['rejected'] as List).cast<int>(),
      );

  final int total;
  final int accepted;
  final List<int> rejected;
}

class ScannerApi {
  ScannerApi(String baseUrl, {http.Client? client, this.timeout = const Duration(seconds: 30)})
      : baseUrl = normalizeServerAddress(baseUrl),
        _client = client ?? http.Client();

  final String baseUrl;
  final Duration timeout;
  final http.Client _client;

  Uri _uri(String path) => Uri.parse('$baseUrl$path');

  /// GET /health → versión del servidor.
  Future<String> health() async {
    final j = await _send(() => _client.get(_uri('/health')), timeout: const Duration(seconds: 5));
    return j['version'] as String? ?? '?';
  }

  /// POST /sessions → id de la sesión.
  Future<String> createSession(String name, {Map<String, dynamic>? device}) async {
    final j = await _send(() => _client.post(
          _uri('/sessions'),
          headers: {'content-type': 'application/json'},
          body: jsonEncode({'name': name, 'device': device ?? {}}),
        ));
    return j['session_id'] as String;
  }

  /// POST /sessions/{id}/photos (multipart: image + metadata).
  Future<PhotoValidation> uploadPhoto(String sessionId, List<int> jpegBytes, PhotoMetadata metadata) async {
    final request = http.MultipartRequest('POST', _uri('/sessions/$sessionId/photos'))
      ..fields['metadata'] = jsonEncode(metadata.toJson())
      ..files.add(http.MultipartFile.fromBytes(
        'image',
        jpegBytes,
        filename: 'img_${metadata.index.toString().padLeft(3, '0')}.jpg',
      ));
    final j = await _send(() async => http.Response.fromStream(await _client.send(request)),
        timeout: const Duration(minutes: 2));
    return PhotoValidation.fromJson(j);
  }

  /// POST /sessions/{id}/complete → resumen.
  Future<SessionSummary> complete(String sessionId) async {
    final j = await _send(() => _client.post(_uri('/sessions/$sessionId/complete')));
    return SessionSummary.fromJson(j);
  }

  void close() => _client.close();

  Future<Map<String, dynamic>> _send(Future<http.Response> Function() call, {Duration? timeout}) async {
    final http.Response res;
    try {
      res = await call().timeout(timeout ?? this.timeout);
    } on TimeoutException {
      throw ApiException('El servidor no respondió a tiempo ($baseUrl)');
    } on SocketException catch (e) {
      throw ApiException('No se pudo conectar con $baseUrl: ${e.message}');
    } on http.ClientException catch (e) {
      throw ApiException('No se pudo conectar con $baseUrl: ${e.message}');
    }

    Map<String, dynamic>? body;
    try {
      final decoded = jsonDecode(utf8.decode(res.bodyBytes));
      if (decoded is Map<String, dynamic>) body = decoded;
    } on FormatException {
      body = null;
    }

    if (res.statusCode < 200 || res.statusCode >= 300) {
      final msg = body?['error'] as String? ?? 'Error del servidor';
      throw ApiException(msg, statusCode: res.statusCode);
    }
    if (body == null) throw ApiException('Respuesta inválida del servidor', statusCode: res.statusCode);
    return body;
  }
}
