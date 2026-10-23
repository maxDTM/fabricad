// FabriCAD part I/O: lazy loader of the interop plugin (src/interop): the OCCT IGES / OBJ / glTF
// toolkits are loaded on the first IGES, OBJ or glTF use instead of at application start.
// Search order: $FABRICAD_INTEROP_PLUGIN, <exe>/../lib/fabricad, <exe>/../lib, <exe>, the build tree.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <string>

class TDocStd_Document;
class TopoDS_Shape;

namespace fabricad::part::io {

struct InteropApi {
  bool (*writeCaf)(TDocStd_Document*, const char*, int, bool, const char*, const char*, char*, int) = nullptr;
  bool (*writeIges)(const TopoDS_Shape*, int, const char*, bool, char*, int) = nullptr;
  bool (*readIges)(const char*, TopoDS_Shape*, int*, int*, char*, int) = nullptr;
};

/// The plugin's entry points (loaded once); null with `error` set when it cannot be loaded.
const InteropApi* interopApi(std::string* error = nullptr);

}  // namespace fabricad::part::io
