#include "pairing.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace {

constexpr double kPi = 3.14159265358979323846;

Quaternion normalized(const Quaternion& q) {
  const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (!(n > 0)) throw std::invalid_argument("cuaternión de norma 0");
  return {q[0] / n, q[1] / n, q[2] / n, q[3] / n};
}

}  // namespace

double quaternionAngleDeg(const Quaternion& a, const Quaternion& b) {
  const Quaternion p = normalized(a);
  const Quaternion q = normalized(b);
  const double dot = std::abs(p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3] * q[3]);
  return 2.0 * std::acos(std::min(1.0, dot)) * 180.0 / kPi;
}

std::vector<ViewPair> selectNeighborPairs(const std::vector<ViewOrientation>& views, double max_angle_deg,
                                          int max_neighbors) {
  std::map<std::pair<int, int>, double> unique;  // (i, j) con i < j -> ángulo
  for (const ViewOrientation& v : views) {
    std::vector<std::pair<double, int>> candidates;  // (ángulo, índice de la otra vista)
    for (const ViewOrientation& w : views) {
      if (w.index == v.index) continue;
      const double angle = quaternionAngleDeg(v.q, w.q);
      if (angle <= max_angle_deg) candidates.emplace_back(angle, w.index);
    }
    std::sort(candidates.begin(), candidates.end());
    if (static_cast<int>(candidates.size()) > max_neighbors) candidates.resize(static_cast<std::size_t>(max_neighbors));
    for (const auto& [angle, other] : candidates) unique[{std::min(v.index, other), std::max(v.index, other)}] = angle;
  }

  std::vector<ViewPair> pairs;
  pairs.reserve(unique.size());
  for (const auto& [key, angle] : unique) pairs.push_back({key.first, key.second, angle});
  return pairs;
}
