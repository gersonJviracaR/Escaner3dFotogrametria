// Salidas del emparejamiento de una sesión (sección 3.8 del Sprint 2):
// matching_report.json y las correspondencias inlier de cada par aceptado.
// Formatos documentados en docs/formatos.md.
#pragma once

#include "intrinsics.h"
#include "matching_params.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

constexpr int kMatchingReportSchemaVersion = 1;

struct ViewReport {
  int index = 0;
  std::string file;           // "img_001.jpg"
  std::string features_file;  // "features/img_001.feat"
  int n_features = 0;
  int work_width = 0;
  int work_height = 0;
  CameraIntrinsics intrinsics;
  double load_ms = 0;     // lectura y decodificación del JPEG
  double extract_ms = 0;  // preparación (gris, reducción, CLAHE) + SIFT
};

struct ExcludedView {
  int index = 0;
  std::string reason;  // "rechazada_calidad", "sin_orientacion", "sin_dimensiones", "imagen_ilegible"
};

struct PairReport {
  int i = 0;
  int j = 0;
  double angle_deg = 0;
  int n_matches = 0;
  int n_inliers = 0;
  double inlier_ratio = 0;
  double mean_sampson_px = 0;
  bool accepted = false;
  std::vector<std::string> reasons;
  bool rank2_enforced = false;
  std::optional<cv::Matx33d> F;  // ausente si no se estimó
  std::optional<cv::Matx33d> E;
  double match_ms = 0;
  double geometry_ms = 0;
  std::string inliers_file;  // "matches/pair_001_002.csv"; vacío si el par fue rechazado
};

struct MatchingSummary {
  int views_used = 0;
  int views_excluded = 0;
  int pairs_all_vs_all = 0;  // n (n - 1) / 2 con las vistas usadas
  int pairs_selected = 0;
  int pairs_accepted = 0;
  int pairs_rejected = 0;
};

struct MatchingReport {
  int schema_version = kMatchingReportSchemaVersion;
  std::string session_id;
  std::string generated_at;
  MatchingParams params;
  std::vector<ViewReport> views;
  std::vector<ExcludedView> excluded_views;
  std::vector<PairReport> pairs;
  std::vector<std::string> warnings;
  double extraction_ms = 0;
  double matching_ms = 0;  // emparejamiento + geometría de todos los pares
  double total_ms = 0;
};

MatchingSummary summarize(const MatchingReport& r);

nlohmann::json toJson(const MatchingReport& r);
// Lanza std::invalid_argument si faltan campos obligatorios.
MatchingReport matchingReportFromJson(const nlohmann::json& j);
void writeMatchingReport(const std::filesystem::path& file, const MatchingReport& r);
MatchingReport readMatchingReport(const std::filesystem::path& file);

// Correspondencia inlier de un par aceptado (entrada del Sprint 3).
struct InlierCorrespondence {
  int index_i = 0;  // índice del punto en features/img_<i>.feat
  int index_j = 0;  // índice del punto en features/img_<j>.feat
  cv::Point2f point_i;  // píxeles de la imagen original
  cv::Point2f point_j;
  float distance = 0;      // distancia entre descriptores
  double sampson_px = 0;   // distancia de Sampson con la F del par
};

// "matches/pair_001_002.csv" (relativo a la carpeta de la sesión)
std::string inliersFileName(int i, int j);
void writeInliers(const std::filesystem::path& file, const std::vector<InlierCorrespondence>& inliers);
std::vector<InlierCorrespondence> readInliers(const std::filesystem::path& file);
