// Mallas sintéticas para las pruebas del Epic 4.
#pragma once

#include "meshing.h"

namespace testmesh {

// Cubo unitario [0,1]^3 cerrado, 8 vértices y 12 triángulos con normales hacia afuera.
inline Mesh cube() {
  Mesh m;
  m.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  m.faces = {{0, 2, 1}, {0, 3, 2},   // z = 0 (normal -z)
             {4, 5, 6}, {4, 6, 7},   // z = 1 (+z)
             {0, 1, 5}, {0, 5, 4},   // y = 0 (-y)
             {3, 7, 6}, {3, 6, 2},   // y = 1 (+y)
             {0, 4, 7}, {0, 7, 3},   // x = 0 (-x)
             {1, 2, 6}, {1, 6, 5}};  // x = 1 (+x)
  return m;
}

// Cuadrado unitario en z = 0 (abierto), normal +z.
inline Mesh plane() {
  Mesh m;
  m.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  m.faces = {{0, 1, 2}, {0, 2, 3}};
  return m;
}

}  // namespace testmesh
