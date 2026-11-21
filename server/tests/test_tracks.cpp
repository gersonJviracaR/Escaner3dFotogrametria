// Pruebas de las pistas (tracks) multivista del Epic 3.
#include "tracks.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <utility>

namespace {

InlierCorrespondence corr(int a, int b) {
  InlierCorrespondence c;
  c.index_i = a;
  c.index_j = b;
  c.point_i = {float(a), float(a) + 0.5f};
  c.point_j = {float(b), float(b) + 0.5f};
  return c;
}

PairMatches pair(int i, int j, std::vector<InlierCorrespondence> inliers) { return {i, j, std::move(inliers)}; }

}  // namespace

TEST(Tracks, ChainsJoinIntoMultiviewTracks) {
  // Punto 10 de la foto 1 = punto 20 de la 2 = punto 30 de la 3; otro punto solo en 1-2.
  TrackStats stats;
  const std::vector<Track> tracks =
      buildTracks({pair(1, 2, {corr(10, 20), corr(11, 21)}), pair(2, 3, {corr(20, 30)})}, &stats);
  ASSERT_EQ(tracks.size(), 2u);
  const Track& longest = tracks[0].observations.size() == 3 ? tracks[0] : tracks[1];
  ASSERT_EQ(longest.observations.size(), 3u);
  EXPECT_EQ(longest.observations[0].image, 1);
  EXPECT_EQ(longest.observations[0].point, 10);
  EXPECT_EQ(longest.observations[2].image, 3);
  EXPECT_EQ(longest.observations[2].point, 30);
  EXPECT_DOUBLE_EQ(longest.observations[1].xy.y, 20.5);
  EXPECT_EQ(stats.tracks, 2);
  EXPECT_EQ(stats.observations, 5);
  EXPECT_EQ(stats.discarded_inconsistent, 0);
}

TEST(Tracks, RedundantPairsDoNotDuplicateObservations) {
  const std::vector<Track> tracks =
      buildTracks({pair(1, 2, {corr(10, 20)}), pair(2, 3, {corr(20, 30)}), pair(1, 3, {corr(10, 30)})});
  ASSERT_EQ(tracks.size(), 1u);
  EXPECT_EQ(tracks[0].observations.size(), 3u);
}

TEST(Tracks, TrackWithTwoPointsInSamePhotoIsDiscarded) {
  // 1:10 -> 2:20 -> 3:30 -> 1:12: la foto 1 aparece con dos puntos distintos.
  TrackStats stats;
  const std::vector<Track> tracks = buildTracks(
      {pair(1, 2, {corr(10, 20), corr(50, 60)}), pair(2, 3, {corr(20, 30)}), pair(1, 3, {corr(12, 30)})}, &stats);
  ASSERT_EQ(tracks.size(), 1u);  // solo sobrevive 1:50 - 2:60
  EXPECT_EQ(tracks[0].observations[0].point, 50);
  EXPECT_EQ(stats.discarded_inconsistent, 1);
}

TEST(Tracks, EachObservationBelongsToOneTrack) {
  std::vector<InlierCorrespondence> a, b;
  for (int k = 0; k < 40; ++k) {
    a.push_back(corr(k, 100 + k));
    b.push_back(corr(100 + k, 200 + (k % 20) * 2 + k / 20 * 1000));
  }
  const std::vector<Track> tracks = buildTracks({pair(1, 2, a), pair(2, 3, b)});
  std::set<std::pair<int, int>> seen;
  for (const Track& t : tracks)
    for (const Observation& o : t.observations) EXPECT_TRUE(seen.insert({o.image, o.point}).second);
  EXPECT_EQ(tracks.size(), 40u);
}
