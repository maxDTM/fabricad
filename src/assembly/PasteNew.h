// FabriCAD assembly: Paste New (Fusion Edit > Paste New, Ctrl+Shift+V). Unlike Paste, which
// inserts another occurrence of the same definition, Paste New makes an independent
// component: its sub-tree is copied and every timeline feature that belongs to the copied
// components (sketches, solid features, construction geometry) is duplicated with new feature,
// body and component ids, so the copy has its own editable history. Joints and component
// markers are not copied. The copy sits at the source's world placement; one undo step.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Tool.h"
#include "core/Types.h"

#include <map>
#include <string>
#include <vector>

namespace fabricad::assembly {

struct DeepCopyResult {
  core::ObjectId component = core::kInvalidId;       ///< the new top component
  std::map<core::ObjectId, core::ObjectId> components;  ///< old -> new component ids (sub-tree)
  std::map<core::ObjectId, core::ObjectId> features;    ///< old -> new feature ids
  std::map<core::ObjectId, core::ObjectId> bodies;      ///< old -> new body ids
};

/// Deep-copy `source` (its definition's geometry for an occurrence) under `parent`, keeping the
/// world placement. Appends the copied features at the rollback marker and recomputes. No undo
/// (callers wrap it, see pasteNewComponents). Returns an empty result when `source` is unknown
/// or the root.
DeepCopyResult deepCopyComponent(core::Document& doc, core::ObjectId source, core::ObjectId parent);

/// "assembly.paste-new": one independent copy per component on the clipboard, under the active
/// component (or the source's parent when the active component is inside the source), as one
/// undo step; the copies become the selection and Move starts when `startMove`.
std::vector<core::ObjectId> pasteNewComponents(core::ToolContext& ctx, bool startMove = true);

/// Replace feature names inside a history name ("{Extrude1/Side/c1|Extrude1/End}") using `names`
/// (old -> new); only whole names in front of '/' are replaced. Exposed for tests.
std::string renameHistoryName(const std::string& historyName, const std::map<std::string, std::string>& names);

}  // namespace fabricad::assembly
