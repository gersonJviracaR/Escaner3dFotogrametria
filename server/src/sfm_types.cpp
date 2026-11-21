#include "sfm_types.h"

int Reconstruction::registeredCount() const {
  int n = 0;
  for (const auto& [index, cam] : cameras) n += cam.registered ? 1 : 0;
  return n;
}

void Reconstruction::reindexTracks() {
  point_of_track.assign(tracks.size(), -1);
  for (std::size_t k = 0; k < points.size(); ++k)
    if (points[k].track >= 0) point_of_track[static_cast<std::size_t>(points[k].track)] = static_cast<int>(k);
}
