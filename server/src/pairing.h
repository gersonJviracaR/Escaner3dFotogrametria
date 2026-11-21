// Selección de pares de vistas vecinas según la orientación registrada (HU-10).
#pragma once

#include <array>
#include <vector>

using Quaternion = std::array<double, 4>;  // [w, x, y, z], como lo envía la app

// Ángulo de la rotación relativa entre dos orientaciones, en grados:
// θ = 2·acos(|q1·q2|) con ambos cuaterniones normalizados (q y -q dan 0°).
// Es la misma fórmula que usa la app (capture_guide.dart). Lanza
// std::invalid_argument si alguno tiene norma 0.
double quaternionAngleDeg(const Quaternion& a, const Quaternion& b);

struct ViewOrientation {
  int index = 0;  // índice de la foto en el manifiesto
  Quaternion q{1, 0, 0, 0};
};

struct ViewPair {
  int i = 0;  // índices del manifiesto, i < j
  int j = 0;
  double angle_deg = 0;
};

// Para cada vista, las hasta max_neighbors vistas de menor ángulo con θ <= max_angle_deg.
// Devuelve la unión de esos pares sin repetidos, ordenada por (i, j).
std::vector<ViewPair> selectNeighborPairs(const std::vector<ViewOrientation>& views, double max_angle_deg,
                                          int max_neighbors);
