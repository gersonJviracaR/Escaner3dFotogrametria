// Parámetros del Epic 2 (características, emparejamiento y geometría epipolar).
// Son valores iniciales configurables desde la sección "matching" de la configuración.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

struct MatchingParams {
  // HU-07 / HU-08: extracción
  int work_long_side = 2000;     // lado mayor de la imagen de trabajo (px)
  int max_features = 8000;       // tope de puntos SIFT por foto
  bool clahe = true;             // normalización de contraste antes de SIFT
  double clahe_clip_limit = 2.0;
  int clahe_tile = 8;            // rejilla de clahe_tile x clahe_tile
  // HU-09 / HU-10: emparejamiento
  double ratio = 0.75;           // ratio test de Lowe
  double max_angle_deg = 45.0;   // diferencia angular máxima entre vistas vecinas
  int max_neighbors = 6;         // vecinas por foto
  // Ampliación autorizada en el Sprint 3: además de los pares por orientación, cada vista
  // se empareja con las neighbor_index_window siguientes (por posición) y, con
  // ring_closure, las últimas con las primeras. Por defecto 0 / false (selección del
  // Sprint 2): en la sesión tenis la ventana 3 no agregó ningún par (ver sprint3-reporte.md).
  int neighbor_index_window = 0;
  bool ring_closure = false;
  // HU-11 / HU-12: geometría epipolar
  double ransac_threshold_px = 1.0;  // umbral de Sampson (px de la imagen original)
  double ransac_confidence = 0.999;
  int min_inliers = 30;
  double min_inlier_ratio = 0.25;
  double fallback_focal_factor = 1.2;  // f = factor * max(ancho, alto) si no hay focal_35mm
};

// Aplica sobre `base` solo los campos presentes. Lanza std::invalid_argument con
// un mensaje en español si hay claves desconocidas o valores fuera de rango.
MatchingParams applyMatchingJson(MatchingParams base, const nlohmann::json& j);
nlohmann::json toJson(const MatchingParams& p);

// Opciones de línea de comandos de scanner_match: "--max-angle-deg 30" equivale a
// {"max_angle_deg": 30}. Los valores "true"/"false" son booleanos; el resto, números.
bool isMatchingOverride(const std::string& option);
MatchingParams applyMatchingOverride(MatchingParams base, const std::string& option, const std::string& value);
