// Control de calidad de una foto (HU-06): nitidez y saturación.
#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

struct QualityThresholds {
  double sharpness_min = 100.0;   // varianza mínima del Laplaciano (lado mayor = 1000 px)
  double overexposed_max = 0.15;  // fracción máxima de píxeles con gris >= 250
};

struct QualityResult {
  double sharpness = 0.0;
  double overexposed_ratio = 0.0;
  bool accepted = false;
  std::vector<std::string> reasons;  // "borrosa" y/o "saturada"
};

// Lado mayor al que se normaliza la imagen antes de medir.
constexpr int kQualityLongSide = 1000;
// Nivel de gris a partir del cual un píxel cuenta como saturado.
constexpr int kOverexposedLevel = 250;

// Evalúa una imagen BGR o en escala de grises. Lanza std::invalid_argument si está vacía.
QualityResult evaluateQuality(const cv::Mat& image, const QualityThresholds& thresholds);
