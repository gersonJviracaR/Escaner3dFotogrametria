#include "bundle_adjustment.h"

#include <ceres/ceres.h>
#include <ceres/rotation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <thread>

namespace {

// Error de reproyección con el modelo de la sección 3.3 (distorsión radial k1, k2).
struct ReprojectionResidual {
  ReprojectionResidual(double u, double v, double cx, double cy) : u_(u), v_(v), cx_(cx), cy_(cy) {}

  template <typename T>
  bool operator()(const T* q, const T* t, const T* X, const T* intr, T* residual) const {
    T p[3];
    ceres::QuaternionRotatePoint(q, X, p);  // q = [w, x, y, z]
    p[0] += t[0];
    p[1] += t[1];
    p[2] += t[2];
    const T a = p[0] / p[2];
    const T b = p[1] / p[2];
    const T r2 = a * a + b * b;
    const T d = T(1.0) + intr[1] * r2 + intr[2] * r2 * r2;
    residual[0] = intr[0] * d * a + T(cx_) - T(u_);
    residual[1] = intr[0] * d * b + T(cy_) - T(v_);
    return true;
  }

  static ceres::CostFunction* create(const cv::Point2d& obs, double cx, double cy) {
    return new ceres::AutoDiffCostFunction<ReprojectionResidual, 2, 4, 3, 3, 3>(
        new ReprojectionResidual(obs.x, obs.y, cx, cy));
  }

  double u_, v_, cx_, cy_;
};

bool sparseAvailable(ceres::SparseLinearAlgebraLibraryType& lib) {
  for (ceres::SparseLinearAlgebraLibraryType candidate : {ceres::SUITE_SPARSE, ceres::EIGEN_SPARSE})
    if (ceres::IsSparseLinearAlgebraLibraryTypeAvailable(candidate)) {
      lib = candidate;
      return true;
    }
  return false;
}

int firstRegistered(const Reconstruction& rec, std::size_t position) {
  std::size_t seen = 0;
  for (int img : rec.registration_order)
    if (rec.cameras.at(img).registered && seen++ == position) return img;
  return -1;
}

}  // namespace

std::string bundleSolverName() {
  ceres::SparseLinearAlgebraLibraryType lib;
  return sparseAvailable(lib) ? "SPARSE_SCHUR" : "DENSE_SCHUR";
}

double reprojectionRms(const Reconstruction& rec) {
  double sum = 0;
  long long n = 0;
  for (const Point3D& p : rec.points)
    for (const Observation& o : p.observations) {
      const auto it = rec.cameras.find(o.image);
      if (it == rec.cameras.end() || !it->second.registered) continue;
      const double e = reprojectionError(rec.camera, it->second.pose, p.xyz, o.xy);
      sum += e * e;
      ++n;
    }
  return n > 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

BundleReport bundleAdjust(Reconstruction& rec, const BundleOptions& options) {
  const auto t0 = std::chrono::steady_clock::now();
  BundleReport report;
  report.solver = bundleSolverName();
  report.rms_before_px = reprojectionRms(rec);

  // Copias de los parámetros en bloques contiguos (Ceres trabaja sobre double*).
  std::map<int, std::array<double, 4>> q;
  std::map<int, std::array<double, 3>> t;
  for (const auto& [img, cam] : rec.cameras)
    if (cam.registered) {
      q[img] = cam.pose.q;
      t[img] = {cam.pose.t[0], cam.pose.t[1], cam.pose.t[2]};
    }
  std::vector<std::array<double, 3>> X(rec.points.size());
  for (std::size_t k = 0; k < rec.points.size(); ++k)
    X[k] = {rec.points[k].xyz[0], rec.points[k].xyz[1], rec.points[k].xyz[2]};
  std::array<double, 3> intr = {rec.camera.f, rec.camera.k1, rec.camera.k2};

  ceres::Problem problem;
  for (std::size_t k = 0; k < rec.points.size(); ++k)
    for (const Observation& o : rec.points[k].observations) {
      const auto it = rec.cameras.find(o.image);
      if (it == rec.cameras.end() || !it->second.registered) continue;
      if (it->second.pose.toCamera(rec.points[k].xyz)[2] <= 0) continue;  // detrás: lo descarta HU-17
      problem.AddResidualBlock(ReprojectionResidual::create(o.xy, rec.camera.cx, rec.camera.cy),
                               new ceres::HuberLoss(options.huber_delta_px), q[o.image].data(), t[o.image].data(),
                               X[k].data(), intr.data());
      ++report.n_observations;
    }
  if (report.n_observations == 0) {
    report.rms_after_px = report.rms_before_px;
    report.termination = "SIN_OBSERVACIONES";
    return report;
  }
  report.n_points = static_cast<int>(rec.points.size());

  for (auto& [img, block] : q) {
    if (!problem.HasParameterBlock(block.data())) continue;
    problem.SetManifold(block.data(), new ceres::QuaternionManifold());
    ++report.n_cameras;
  }

  // Calibre: pose fija de una cámara y una componente de la traslación de otra.
  const int fixed = options.fixed_camera >= 0 ? options.fixed_camera : firstRegistered(rec, 0);
  const int scale = options.scale_camera >= 0 ? options.scale_camera : firstRegistered(rec, 1);
  if (fixed >= 0 && problem.HasParameterBlock(q[fixed].data())) {
    problem.SetParameterBlockConstant(q[fixed].data());
    problem.SetParameterBlockConstant(t[fixed].data());
  }
  if (scale >= 0 && scale != fixed && problem.HasParameterBlock(t[scale].data())) {
    const std::array<double, 3>& ts = t[scale];
    int largest = 0;
    for (int k = 1; k < 3; ++k)
      if (std::abs(ts[k]) > std::abs(ts[largest])) largest = k;
    problem.SetManifold(t[scale].data(), new ceres::SubsetManifold(3, {largest}));
  }

  if (!options.refine_focal && !options.refine_distortion) {
    problem.SetParameterBlockConstant(intr.data());
  } else if (!options.refine_focal) {
    problem.SetManifold(intr.data(), new ceres::SubsetManifold(3, {0}));
  } else if (!options.refine_distortion) {
    problem.SetManifold(intr.data(), new ceres::SubsetManifold(3, {1, 2}));
  }

  ceres::Solver::Options so;
  ceres::SparseLinearAlgebraLibraryType lib;
  if (sparseAvailable(lib)) {
    so.linear_solver_type = ceres::SPARSE_SCHUR;
    so.sparse_linear_algebra_library_type = lib;
  } else {
    so.linear_solver_type = ceres::DENSE_SCHUR;
  }
  so.max_num_iterations = options.max_iterations;
  so.num_threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
  so.logging_type = ceres::SILENT;
  so.minimizer_progress_to_stdout = false;
  ceres::Solver::Summary summary;
  ceres::Solve(so, &problem, &summary);

  for (auto& [img, cam] : rec.cameras) {
    if (!cam.registered) continue;
    Quat qn = q[img];
    const double n = quaternionNorm(qn);
    for (double& v : qn) v /= n;  // el manifold ya la conserva; se normaliza por redondeo
    cam.pose.q = qn;
    cam.pose.t = cv::Vec3d(t[img][0], t[img][1], t[img][2]);
  }
  for (std::size_t k = 0; k < rec.points.size(); ++k) rec.points[k].xyz = cv::Vec3d(X[k][0], X[k][1], X[k][2]);
  rec.camera.f = intr[0];
  rec.camera.k1 = intr[1];
  rec.camera.k2 = intr[2];

  report.iterations = static_cast<int>(summary.iterations.size()) - 1;  // la iteración 0 es la evaluación inicial
  report.initial_cost = summary.initial_cost;
  report.final_cost = summary.final_cost;
  report.termination = ceres::TerminationTypeToString(summary.termination_type);
  report.rms_after_px = reprojectionRms(rec);
  report.time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return report;
}
