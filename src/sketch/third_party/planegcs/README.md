# PlaneGCS (vendored)

Origin: FreeCAD `src/Mod/Sketcher/App/planegcs/`, revision `703986732cf5` (main, 2026-10-19).
License: LGPL-2.1-or-later (headers kept in every file; FreeCAD's full license text is the
GNU Library General Public License v2, same as FabriCAD's `LICENSE`).
Authors: Konstantinos Poulios and FreeCAD contributors.

Files: `GCS.cpp/.h`, `Constraints.cpp/.h`, `Geo.cpp/.h`, `SubSystem.cpp/.h`, `qp_eq.cpp/.h`, `Util.h`.

## Local modifications (all marked with a `// FabriCAD:` comment)

* `GCS.h`, `Geo.h`, `Constraints.h`: `#include "../../SketcherGlobal.h"` -> `#include "SketcherGlobal.h"`
  (resolved from `compat/`).
* `GCS.cpp`: `std::unreachable()` -> `__builtin_unreachable()` so the file builds as C++20.

## compat/

Stand-ins for FreeCAD / boost headers that PlaneGCS includes but barely uses
(`SketcherGlobal.h`, `FCConfig.h`, `Base/Console.h`, `Base/Tools.h`,
`boost/math/constants/constants.hpp`, `boost/graph/graph_concepts.hpp`,
`boost/graph/connected_components.hpp`, `boost_graph_adjacency_list.hpp` with a 25-line
DFS connected-components replacement). Written for FabriCAD (LGPL-2.1-or-later).

## Build

Compiled into `fabricad_sketch` with `-w -include cassert -DEIGEN_NO_DEBUG`
(see `src/sketch/CMakeLists.txt`). Only `SketchSolver.cpp` includes `GCS.h`.
