// Spike SP-0: comprueba que OpenCV (lectura de imagen + Laplaciano) y Ceres
// (problema trivial de mínimos cuadrados) compilan, enlazan y funcionan.
#include <ceres/ceres.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <iostream>
#include <string>

namespace {

struct DistanceToFive {
  template <typename T>
  bool operator()(const T* const x, T* residual) const {
    residual[0] = x[0] - T(5.0);
    return true;
  }
};

double laplacianVariance(const cv::Mat& gray) {
  cv::Mat lap;
  cv::Laplacian(gray, lap, CV_64F);
  cv::Scalar mean, stddev;
  cv::meanStdDev(lap, mean, stddev);
  return stddev[0] * stddev[0];
}

}  // namespace

int main(int argc, char** argv) {
  std::string path = argc > 1 ? argv[1] : "spike_tablero.png";
  if (argc <= 1) {
    // Genera un tablero de ajedrez sintético y lo escribe para leerlo de vuelta.
    cv::Mat board(400, 400, CV_8UC1);
    for (int y = 0; y < board.rows; ++y)
      for (int x = 0; x < board.cols; ++x)
        board.at<uchar>(y, x) = ((x / 50 + y / 50) % 2) ? 255 : 0;
    if (!cv::imwrite(path, board)) {
      std::cerr << "ERROR: no se pudo escribir " << path << "\n";
      return 1;
    }
  }

  cv::Mat gray = cv::imread(path, cv::IMREAD_GRAYSCALE);
  if (gray.empty()) {
    std::cerr << "ERROR: no se pudo leer " << path << "\n";
    return 1;
  }
  std::cout << "OpenCV " << CV_VERSION << ": " << path << " (" << gray.cols << "x"
            << gray.rows << "), varianza del Laplaciano = " << laplacianVariance(gray)
            << "\n";

  double x = 0.5;
  ceres::Problem problem;
  problem.AddResidualBlock(
      new ceres::AutoDiffCostFunction<DistanceToFive, 1, 1>(new DistanceToFive), nullptr, &x);
  ceres::Solver::Options options;
  options.logging_type = ceres::SILENT;
  ceres::Solver::Summary summary;
  ceres::Solve(options, &problem, &summary);
  std::cout << "Ceres: minimizar (x - 5)^2 desde x = 0.5 -> x = " << x << " ("
            << summary.BriefReport() << ")\n";

  return (std::abs(x - 5.0) < 1e-6) ? 0 : 1;
}
