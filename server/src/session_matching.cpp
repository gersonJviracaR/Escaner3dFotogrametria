#include "session_matching.h"

#include "epipolar.h"
#include "features.h"
#include "manifest.h"
#include "matching.h"
#include "pairing.h"
#include "session_store.h"

#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void say(const SessionMatchingOptions& o, const std::string& text) {
  if (o.log) o.log(text);
}

std::string format(const char* fmt, double a, double b = 0, double c = 0, double d = 0) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), fmt, a, b, c, d);
  return buffer;
}

struct LoadedView {
  ViewReport report;
  ImageFeatures features;
  cv::Matx33d K;
};

// Resultado completo de un par (las correspondencias se guardan para la depuración).
struct PairWork {
  PairReport report;
  std::vector<FeatureMatch> matches;
  std::vector<int> inliers;
};

PairWork processPair(const LoadedView& vi, const LoadedView& vj, double angle_deg, const MatchingParams& params) {
  PairWork w;
  PairReport& pr = w.report;
  pr.i = vi.report.index;
  pr.j = vj.report.index;
  pr.angle_deg = angle_deg;

  const Clock::time_point t_match = Clock::now();
  w.matches = matchFeatures(vi.features, vj.features, params.ratio, params.mutual_check);
  pr.match_ms = msSince(t_match);

  std::vector<cv::Point2f> pts_i, pts_j;
  pts_i.reserve(w.matches.size());
  pts_j.reserve(w.matches.size());
  for (const FeatureMatch& m : w.matches) {
    pts_i.push_back(m.point_a);
    pts_j.push_back(m.point_b);
  }
  const Clock::time_point t_geom = Clock::now();
  PairGeometry g = estimatePairGeometry(pts_i, pts_j, vi.K, vj.K, params);
  pr.geometry_ms = msSince(t_geom);

  // T0b (opción, apagada por defecto): un par ya aceptado se vuelve a emparejar dentro de la
  // banda epipolar de su F, con el mismo ratio test y el mismo umbral; se queda el
  // resultado guiado solo si también es aceptado y tiene más inliers.
  if (params.guided_matching && g.accepted) {
    const Clock::time_point t_guided = Clock::now();
    std::vector<FeatureMatch> guided =
        matchFeaturesGuided(vi.features, vj.features, g.F, params.ransac_threshold_px, params.ratio, params.mutual_check);
    std::vector<cv::Point2f> gi, gj;
    for (const FeatureMatch& m : guided) {
      gi.push_back(m.point_a);
      gj.push_back(m.point_b);
    }
    PairGeometry g2 = estimatePairGeometry(gi, gj, vi.K, vj.K, params);
    if (g2.accepted && g2.n_inliers > g.n_inliers) {
      w.matches = std::move(guided);
      g = std::move(g2);
    }
    pr.match_ms += msSince(t_guided);
  }

  pr.n_matches = g.n_matches;
  pr.n_inliers = g.n_inliers;
  pr.inlier_ratio = g.inlier_ratio;
  pr.mean_sampson_px = g.mean_sampson_px;
  pr.accepted = g.accepted;
  pr.reasons = g.reasons;
  pr.rank2_enforced = g.rank2_enforced;
  if (g.f_estimated) {
    pr.F = g.F;
    pr.E = g.E;
  }
  w.inliers = g.inliers;
  return w;
}

std::vector<InlierCorrespondence> inlierCorrespondences(const PairWork& w) {
  std::vector<InlierCorrespondence> out;
  out.reserve(w.inliers.size());
  for (int k : w.inliers) {
    const FeatureMatch& m = w.matches[static_cast<std::size_t>(k)];
    InlierCorrespondence c;
    c.index_i = m.index_a;
    c.index_j = m.index_b;
    c.point_i = m.point_a;
    c.point_j = m.point_b;
    c.distance = m.distance;
    c.sampson_px = sampsonDistance(*w.report.F, m.point_a, m.point_b);
    out.push_back(c);
  }
  return out;
}

// Imagen lado a lado reducida: correspondencias candidatas en rojo e inliers en verde.
void writeDebugImage(const fs::path& out_file, const fs::path& session_dir, const LoadedView& vi,
                     const LoadedView& vj, const PairWork& w) {
  constexpr int kDisplayLongSide = 1600;
  constexpr std::size_t kMaxLines = 400;
  auto load = [&](const LoadedView& v, double& s) {
    cv::Mat img = cv::imread((session_dir / v.report.file).string(), cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
    s = std::min(1.0, static_cast<double>(kDisplayLongSide) / std::max(img.cols, img.rows));
    cv::Mat small;
    cv::resize(img, small, cv::Size(), s, s, cv::INTER_AREA);
    return small;
  };
  double si = 1, sj = 1;
  const cv::Mat img_i = load(vi, si);
  const cv::Mat img_j = load(vj, sj);
  auto toDisplay = [](cv::Point2f p, double s) {
    return cv::Point2f(static_cast<float>((p.x + 0.5) * s - 0.5), static_cast<float>((p.y + 0.5) * s - 0.5));
  };

  std::vector<cv::KeyPoint> kps_i, kps_j;
  std::vector<cv::DMatch> all, inl;
  std::vector<bool> is_inlier(w.matches.size(), false);
  for (int k : w.inliers) is_inlier[static_cast<std::size_t>(k)] = true;
  const std::size_t stride = std::max<std::size_t>(1, w.matches.size() / kMaxLines);
  for (std::size_t k = 0; k < w.matches.size(); k += stride) {
    const int n = static_cast<int>(kps_i.size());
    kps_i.emplace_back(toDisplay(w.matches[k].point_a, si), 4.f);
    kps_j.emplace_back(toDisplay(w.matches[k].point_b, sj), 4.f);
    (is_inlier[k] ? inl : all).emplace_back(n, n, w.matches[k].distance);
  }
  cv::Mat canvas;
  cv::drawMatches(img_i, kps_i, img_j, kps_j, all, canvas, cv::Scalar(0, 0, 255), cv::Scalar(0, 0, 255), {},
                  cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
  cv::drawMatches(img_i, kps_i, img_j, kps_j, inl, canvas, cv::Scalar(0, 200, 0), cv::Scalar(0, 200, 0), {},
                  cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS | cv::DrawMatchesFlags::DRAW_OVER_OUTIMG);
  const std::string caption = format("%.0f-%.0f  angulo %.1f  inliers %.0f", w.report.i, w.report.j,
                                     w.report.angle_deg, w.report.n_inliers) +
                              format(" / %.0f correspondencias", w.report.n_matches) +
                              (w.report.accepted ? "  ACEPTADO" : "  RECHAZADO");
  cv::putText(canvas, caption, {20, 50}, cv::FONT_HERSHEY_SIMPLEX, 1.4, cv::Scalar(0, 0, 0), 6);
  cv::putText(canvas, caption, {20, 50}, cv::FONT_HERSHEY_SIMPLEX, 1.4, cv::Scalar(255, 255, 255), 2);
  cv::imwrite(out_file.string(), canvas, {cv::IMWRITE_JPEG_QUALITY, 90});
}

std::string pairTag(int i, int j) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%03d_%03d", i, j);
  return buffer;
}

}  // namespace

MatchingReport runSessionMatching(const fs::path& session_dir, const MatchingParams& params,
                                  const SessionMatchingOptions& options) {
  const Clock::time_point t_start = Clock::now();
  const fs::path manifest_file = session_dir / "manifest.json";
  if (!fs::is_directory(session_dir)) throw std::runtime_error("no existe la sesión " + session_dir.string());
  if (!fs::exists(manifest_file)) throw std::runtime_error("la sesión no tiene manifest.json: " + session_dir.string());
  Manifest manifest = readManifest(manifest_file);
  std::sort(manifest.photos.begin(), manifest.photos.end(),
            [](const PhotoRecord& a, const PhotoRecord& b) { return a.metadata.index < b.metadata.index; });

  MatchingReport r;
  r.session_id = manifest.session_id;
  r.generated_at = formatLocalTimestamp(std::chrono::system_clock::now());
  r.params = params;

  fs::create_directories(session_dir / "features");
  // Se vacía para que ningún par rechazado conserve inliers de una corrida anterior.
  fs::remove_all(session_dir / "matches");
  fs::create_directories(session_dir / "matches");

  // 1. Características por foto (HU-07, HU-08).
  std::vector<LoadedView> views;
  std::vector<ViewOrientation> orientations;
  for (const PhotoRecord& p : manifest.photos) {
    const int index = p.metadata.index;
    if (!p.quality.accepted) {
      r.excluded_views.push_back({index, "rechazada_calidad"});
      continue;
    }
    if (!p.metadata.orientation_q) {
      r.excluded_views.push_back({index, "sin_orientacion"});
      continue;
    }
    if (p.metadata.width <= 0 || p.metadata.height <= 0) {
      r.excluded_views.push_back({index, "sin_dimensiones"});
      continue;
    }
    const Clock::time_point t_load = Clock::now();
    const cv::Mat image =
        cv::imread((session_dir / p.file).string(), cv::IMREAD_GRAYSCALE | cv::IMREAD_IGNORE_ORIENTATION);
    const double load_ms = msSince(t_load);
    if (image.empty()) {
      r.excluded_views.push_back({index, "imagen_ilegible"});
      continue;
    }
    if (image.cols != p.metadata.width || image.rows != p.metadata.height)
      r.warnings.push_back("la foto " + std::to_string(index) + " mide " + std::to_string(image.cols) + "x" +
                           std::to_string(image.rows) + " pero el manifiesto dice " +
                           std::to_string(p.metadata.width) + "x" + std::to_string(p.metadata.height));

    LoadedView v;
    const Clock::time_point t_extract = Clock::now();
    v.features = extractFeatures(image, params);
    v.report.extract_ms = msSince(t_extract);
    v.report.load_ms = load_ms;
    v.report.index = index;
    v.report.file = p.file;
    v.report.features_file = "features/" + featureFileName(index);
    v.report.n_features = static_cast<int>(v.features.keypoints.size());
    v.report.work_width = v.features.work_width;
    v.report.work_height = v.features.work_height;
    v.report.intrinsics = intrinsicsFromMetadata(p.metadata, params.fallback_focal_factor);
    if (v.report.intrinsics.used_fallback)
      r.warnings.push_back("la foto " + std::to_string(index) + " no trae focal_35mm: f = " +
                           format("%.2f", params.fallback_focal_factor) + " x lado mayor");
    v.K = v.report.intrinsics.matrix();
    writeFeatures(session_dir / v.report.features_file, v.features);
    r.extraction_ms += load_ms + v.report.extract_ms;
    say(options, "foto " + std::to_string(index) + ": " + std::to_string(v.report.n_features) + " puntos, " +
                     format("%.0f ms", v.report.extract_ms));

    orientations.push_back({index, *p.metadata.orientation_q});
    r.views.push_back(v.report);
    views.push_back(std::move(v));
  }

  // 2. Pares vecinos (HU-10).
  // HU-10 (por orientación) más la ventana de vecinos por posición con cierre del anillo
  // (opción del Sprint 3; por defecto neighbor_index_window = 0, igual que el Sprint 2).
  const std::vector<ViewPair> pairs =
      unionPairs(selectNeighborPairs(orientations, params.max_angle_deg, params.max_neighbors),
                 selectIndexWindowPairs(orientations, params.neighbor_index_window, params.ring_closure));
  std::map<int, std::size_t> position;
  for (std::size_t k = 0; k < views.size(); ++k) position[views[k].report.index] = k;
  say(options, std::to_string(pairs.size()) + " pares vecinos de " +
                   std::to_string(views.size() * (views.size() > 0 ? views.size() - 1 : 0) / 2) + " posibles");

  // 3. Emparejamiento y geometría por par (HU-09, HU-11, HU-12).
  std::vector<PairWork> work;
  work.reserve(pairs.size());
  for (const ViewPair& vp : pairs) {
    const LoadedView& vi = views[position.at(vp.i)];
    const LoadedView& vj = views[position.at(vp.j)];
    PairWork w = processPair(vi, vj, vp.angle_deg, params);
    if (w.report.accepted) {
      w.report.inliers_file = inliersFileName(vp.i, vp.j);
      writeInliers(session_dir / w.report.inliers_file, inlierCorrespondences(w));
    }
    r.matching_ms += w.report.match_ms + w.report.geometry_ms;
    std::string line = "par " + std::to_string(vp.i) + "-" + std::to_string(vp.j) + ": " +
                       std::to_string(w.report.n_inliers) + "/" + std::to_string(w.report.n_matches) + " inliers";
    if (w.report.accepted) {
      line += ", aceptado";
    } else {
      line += ", rechazado (";
      for (std::size_t k = 0; k < w.report.reasons.size(); ++k) line += (k ? ", " : "") + w.report.reasons[k];
      line += ")";
    }
    say(options, line);
    r.pairs.push_back(w.report);
    work.push_back(std::move(w));
  }

  // 4. Depuración: el par aceptado más débil y el rechazado con más inliers.
  if (options.debug) {
    const fs::path debug_dir = session_dir / "debug";
    fs::remove_all(debug_dir);
    fs::create_directories(debug_dir);
    const PairWork* weakest_accepted = nullptr;
    const PairWork* best_rejected = nullptr;
    for (const PairWork& w : work) {
      if (w.report.accepted && (!weakest_accepted || w.report.inlier_ratio < weakest_accepted->report.inlier_ratio))
        weakest_accepted = &w;
      if (!w.report.accepted && (!best_rejected || w.report.n_inliers > best_rejected->report.n_inliers))
        best_rejected = &w;
    }
    PairWork far_pair;  // si no hubo rechazados: el par más lejano, solo para mirar
    if (!best_rejected && views.size() >= 2) {
      double worst = -1;
      std::size_t a = 0, b = 1;
      for (std::size_t x = 0; x < views.size(); ++x)
        for (std::size_t y = x + 1; y < views.size(); ++y) {
          const double angle = quaternionAngleDeg(orientations[x].q, orientations[y].q);
          if (angle > worst) worst = angle, a = x, b = y;
        }
      far_pair = processPair(views[a], views[b], worst, params);
      r.warnings.push_back("depuración: no hubo pares vecinos rechazados; el par lejano " +
                           std::to_string(far_pair.report.i) + "-" + std::to_string(far_pair.report.j) +
                           format(" (%.1f grados) dio %.0f/%.0f inliers y quedó ", worst, far_pair.report.n_inliers,
                                  far_pair.report.n_matches) +
                           (far_pair.report.accepted ? "aceptado" : "rechazado") + " (no entra al reporte de pares)");
      writeDebugImage(debug_dir / ("far_" + pairTag(far_pair.report.i, far_pair.report.j) + ".jpg"), session_dir,
                      views[a], views[b], far_pair);
    }
    auto viewOf = [&](int index) -> const LoadedView& { return views[position.at(index)]; };
    if (weakest_accepted)
      writeDebugImage(debug_dir / ("accepted_" + pairTag(weakest_accepted->report.i, weakest_accepted->report.j) + ".jpg"),
                      session_dir, viewOf(weakest_accepted->report.i), viewOf(weakest_accepted->report.j),
                      *weakest_accepted);
    if (best_rejected)
      writeDebugImage(debug_dir / ("rejected_" + pairTag(best_rejected->report.i, best_rejected->report.j) + ".jpg"),
                      session_dir, viewOf(best_rejected->report.i), viewOf(best_rejected->report.j), *best_rejected);
  }

  r.total_ms = msSince(t_start);
  writeMatchingReport(session_dir / "matching_report.json", r);
  return r;
}
