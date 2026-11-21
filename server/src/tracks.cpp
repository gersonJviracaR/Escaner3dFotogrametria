#include "tracks.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <utility>

namespace {

class UnionFind {
 public:
  int add() {
    parent_.push_back(static_cast<int>(parent_.size()));
    return parent_.back();
  }
  int find(int x) {
    while (parent_[x] != x) {
      parent_[x] = parent_[parent_[x]];  // compresión de caminos a la mitad
      x = parent_[x];
    }
    return x;
  }
  void unite(int a, int b) {
    a = find(a);
    b = find(b);
    if (a != b) parent_[std::max(a, b)] = std::min(a, b);
  }

 private:
  std::vector<int> parent_;
};

}  // namespace

std::vector<Track> buildTracks(const std::vector<PairMatches>& pairs, TrackStats* stats) {
  UnionFind uf;
  std::map<std::pair<int, int>, int> node_of;  // (foto, punto) -> nodo
  std::vector<Observation> nodes;
  auto nodeFor = [&](int image, int point, const cv::Point2f& xy) {
    const auto [it, inserted] = node_of.try_emplace({image, point}, 0);
    if (inserted) {
      it->second = uf.add();
      nodes.push_back({image, point, cv::Point2d(xy.x, xy.y)});
    }
    return it->second;
  };
  for (const PairMatches& p : pairs)
    for (const InlierCorrespondence& c : p.inliers)
      uf.unite(nodeFor(p.i, c.index_i, c.point_i), nodeFor(p.j, c.index_j, c.point_j));

  // Agrupar por raíz; node_of está ordenado por (foto, punto), así que las pistas salen
  // en un orden determinista y sus observaciones ordenadas por foto.
  std::map<int, std::vector<int>> groups;
  for (const auto& [key, node] : node_of) groups[uf.find(node)].push_back(node);

  std::vector<Track> tracks;
  int discarded = 0;
  for (const auto& [root, members] : groups) {
    Track t;
    bool consistent = true;
    for (int n : members) {
      if (!t.observations.empty() && t.observations.back().image == nodes[n].image) consistent = false;
      t.observations.push_back(nodes[n]);
    }
    if (!consistent) {
      ++discarded;
      continue;
    }
    if (t.observations.size() >= 2) tracks.push_back(std::move(t));
  }
  if (stats) {
    stats->observations = static_cast<int>(nodes.size());
    stats->tracks = static_cast<int>(tracks.size());
    stats->discarded_inconsistent = discarded;
  }
  return tracks;
}
