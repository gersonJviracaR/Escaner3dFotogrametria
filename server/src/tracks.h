// Pistas (tracks) multivista: unión de las correspondencias inlier de los pares aceptados.
#pragma once

#include "matching_report.h"

#include <opencv2/core.hpp>

#include <vector>

// Observación de un punto SIFT: foto (índice del manifiesto), índice del punto en su
// archivo .feat y coordenadas en píxeles de la imagen original.
struct Observation {
  int image = 0;
  int point = 0;
  cv::Point2d xy;
};

struct Track {
  std::vector<Observation> observations;  // ordenadas por foto; como mucho una por foto
};

struct PairMatches {
  int i = 0;
  int j = 0;
  std::vector<InlierCorrespondence> inliers;
};

struct TrackStats {
  int observations = 0;           // observaciones distintas en la entrada
  int tracks = 0;                 // pistas conservadas
  int discarded_inconsistent = 0; // pistas con dos puntos distintos en la misma foto
};

// Unión-búsqueda sobre (foto, posición del punto): las copias de un mismo punto SIFT con
// distinta orientación (mismas coordenadas, otro índice) se unen. Las pistas inconsistentes se descartan
// enteras. Cada observación pertenece como mucho a una pista.
std::vector<Track> buildTracks(const std::vector<PairMatches>& pairs, TrackStats* stats = nullptr);
