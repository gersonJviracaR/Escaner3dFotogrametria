// Pruebas de HU-10: selección de vistas vecinas según la orientación registrada.
#include "pairing.h"

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace {

constexpr double kPi = 3.14159265358979323846;

// Rotación de `deg` grados alrededor del eje (ax, ay, az) normalizado, como [w, x, y, z].
Quaternion axisAngle(double deg, double ax, double ay, double az) {
  const double n = std::sqrt(ax * ax + ay * ay + az * az);
  const double h = deg * kPi / 360.0;
  return {std::cos(h), std::sin(h) * ax / n, std::sin(h) * ay / n, std::sin(h) * az / n};
}

// Anillo de 20 vistas a 18° alrededor de un eje inclinado, índices 1..20.
std::vector<ViewOrientation> ring20() {
  std::vector<ViewOrientation> views;
  for (int k = 0; k < 20; ++k) views.push_back({k + 1, axisAngle(18.0 * k, 0.1, 1.0, 0.2)});
  return views;
}

std::map<int, int> degreeOf(const std::vector<ViewPair>& pairs) {
  std::map<int, int> degree;
  for (const ViewPair& p : pairs) {
    ++degree[p.i];
    ++degree[p.j];
  }
  return degree;
}

}  // namespace

TEST(Pairing, AngleBetweenQuaternions) {
  const Quaternion id{1, 0, 0, 0};
  EXPECT_NEAR(quaternionAngleDeg(id, id), 0.0, 1e-9);
  EXPECT_NEAR(quaternionAngleDeg(id, axisAngle(90, 0, 0, 1)), 90.0, 1e-9);
  EXPECT_NEAR(quaternionAngleDeg(axisAngle(30, 1, 0, 0), axisAngle(75, 1, 0, 0)), 45.0, 1e-9);
  const Quaternion q = axisAngle(40, 1, 2, 3);
  EXPECT_NEAR(quaternionAngleDeg(q, {-q[0], -q[1], -q[2], -q[3]}), 0.0, 1e-6);  // q y -q: misma rotación
  EXPECT_NEAR(quaternionAngleDeg({2, 0, 0, 0}, {0, 0, 0, 3}), 180.0, 1e-9);     // se normalizan
}

TEST(Pairing, AngleRejectsZeroQuaternion) {
  EXPECT_THROW(quaternionAngleDeg({0, 0, 0, 0}, {1, 0, 0, 0}), std::invalid_argument);
}

TEST(Pairing, RingOf20Gives40UniquePairsIncludingClosure) {
  const std::vector<ViewPair> pairs = selectNeighborPairs(ring20(), 45.0, 6);
  EXPECT_EQ(pairs.size(), 40u);
  EXPECT_LT(pairs.size(), 190u);  // todos contra todos con 20 fotos

  std::set<std::pair<int, int>> unique;
  for (const ViewPair& p : pairs) {
    EXPECT_LT(p.i, p.j);
    EXPECT_LE(p.angle_deg, 45.0);
    unique.insert({p.i, p.j});
    const int gap = std::min(p.j - p.i, 20 - (p.j - p.i));  // distancia en el anillo
    EXPECT_TRUE(gap == 1 || gap == 2) << p.i << "-" << p.j;
    EXPECT_NEAR(p.angle_deg, 18.0 * gap, 1e-6);
  }
  EXPECT_EQ(unique.size(), pairs.size());  // sin repetidos
  EXPECT_TRUE(unique.count({1, 20}));     // cierre del anillo (18°)
  EXPECT_TRUE(unique.count({1, 19}));     // y a 36°
  EXPECT_TRUE(unique.count({2, 20}));
  for (const auto& [view, degree] : degreeOf(pairs)) EXPECT_EQ(degree, 4) << "vista " << view;
}

TEST(Pairing, MaxNeighborsIsRespected) {
  const std::vector<ViewPair> pairs = selectNeighborPairs(ring20(), 45.0, 2);
  EXPECT_EQ(pairs.size(), 20u);
  for (const ViewPair& p : pairs) EXPECT_NEAR(p.angle_deg, 18.0, 1e-6);
  for (const auto& [view, degree] : degreeOf(pairs)) EXPECT_EQ(degree, 2) << "vista " << view;
}

TEST(Pairing, SmallerMaxAngleKeepsOnlyClosestViews) {
  EXPECT_EQ(selectNeighborPairs(ring20(), 20.0, 6).size(), 20u);
  EXPECT_TRUE(selectNeighborPairs(ring20(), 10.0, 6).empty());
}

TEST(Pairing, PairsUseManifestIndicesAndAreSorted) {
  const std::vector<ViewOrientation> views = {
      {7, axisAngle(0, 0, 1, 0)}, {3, axisAngle(20, 0, 1, 0)}, {12, axisAngle(100, 0, 1, 0)}};
  const std::vector<ViewPair> pairs = selectNeighborPairs(views, 45.0, 6);
  ASSERT_EQ(pairs.size(), 1u);
  EXPECT_EQ(pairs[0].i, 3);
  EXPECT_EQ(pairs[0].j, 7);
  EXPECT_NEAR(pairs[0].angle_deg, 20.0, 1e-9);
}

// ---------- Ventana de vecinos por índice (ampliación autorizada en el Sprint 3) ----------

namespace {

std::vector<ViewOrientation> sequence(const std::vector<int>& indices) {
  std::vector<ViewOrientation> views;
  for (int idx : indices) views.push_back({idx, axisAngle(10.0 * idx, 0, 1, 0)});
  return views;
}

std::vector<int> range(int from, int to) {
  std::vector<int> v;
  for (int k = from; k <= to; ++k) v.push_back(k);
  return v;
}

}  // namespace

TEST(IndexWindowPairs, WindowWithRingClosure) {
  const std::vector<ViewPair> pairs = selectIndexWindowPairs(sequence(range(1, 20)), 3, true);
  EXPECT_EQ(pairs.size(), 60u);  // cada vista con las 3 siguientes, dando la vuelta
  std::set<std::pair<int, int>> unique;
  for (const ViewPair& p : pairs) {
    EXPECT_LT(p.i, p.j);
    unique.insert({p.i, p.j});
  }
  EXPECT_EQ(unique.size(), pairs.size());
  EXPECT_TRUE(unique.count({1, 20}));  // cierre del anillo
  EXPECT_TRUE(unique.count({18, 20}));
  EXPECT_TRUE(unique.count({1, 18}));  // 18 -> 19 -> 20 -> 1: distancia 3
  EXPECT_FALSE(unique.count({1, 17}));
  EXPECT_FALSE(unique.count({1, 5}));
}

TEST(IndexWindowPairs, WindowWithoutClosure) {
  const std::vector<ViewPair> pairs = selectIndexWindowPairs(sequence(range(1, 20)), 3, false);
  EXPECT_EQ(pairs.size(), 54u);  // 17·3 + 2 + 1
  for (const ViewPair& p : pairs) EXPECT_LE(p.j - p.i, 3);
}

TEST(IndexWindowPairs, DistanceIsByPositionAmongUsedViews) {
  // Si las fotos 3 y 5 se excluyeron, 2-4 y 4-6 son vecinas inmediatas.
  const std::vector<ViewPair> pairs = selectIndexWindowPairs(sequence({1, 2, 4, 6, 7}), 1, false);
  ASSERT_EQ(pairs.size(), 4u);
  EXPECT_EQ(pairs[1].i, 2);
  EXPECT_EQ(pairs[1].j, 4);
  EXPECT_NEAR(pairs[1].angle_deg, 20.0, 1e-9);  // el ángulo se sigue calculando con la orientación
}

TEST(IndexWindowPairs, ZeroWindowGivesNoPairs) {
  EXPECT_TRUE(selectIndexWindowPairs(sequence(range(1, 10)), 0, true).empty());
}

TEST(IndexWindowPairs, UnionWithoutDuplicates) {
  const std::vector<ViewPair> a = {{1, 2, 10}, {2, 3, 10}};
  const std::vector<ViewPair> b = {{2, 3, 10}, {1, 4, 30}};
  const std::vector<ViewPair> u = unionPairs(a, b);
  ASSERT_EQ(u.size(), 3u);
  EXPECT_EQ(u[0].i, 1);
  EXPECT_EQ(u[0].j, 2);
  EXPECT_EQ(u[1].j, 4);
  EXPECT_EQ(u[2].i, 2);
}
