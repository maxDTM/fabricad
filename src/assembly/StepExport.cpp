// FabriCAD assembly: XCAF STEP export implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/StepExport.h"

#include "assembly/ComponentOps.h"
#include "core/Log.h"
#include "core/ShapeUtils.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <Standard_Failure.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopLoc_Location.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_ColorType.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <QFileInfo>
#include <QString>

#include <functional>
#include <map>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::ObjectId;

namespace {

void setName(const TDF_Label& label, const std::string& name) {
  if (!name.empty()) TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str(), true));
}

struct Exporter {
  const Document& doc;
  const StepExportOptions& opt;
  Handle(XCAFDoc_ShapeTool) shapes;
  Handle(XCAFDoc_ColorTool) colors;
  std::map<ObjectId, TDF_Label> definitionLabels;  ///< definition component -> its (part or assembly) label
  StepExportStats stats;
  std::string rootName;  ///< name for an untitled root (the file's base name)

  /// Part label for the bodies a definition owns (null if none).
  TDF_Label partLabel(ObjectId def, const std::string& name) {
    std::vector<ObjectId> bodies = displayedBodies(doc, def);
    std::vector<TopoDS_Shape> parts;
    std::vector<ObjectId> used;
    for (ObjectId b : bodies) {
      const core::Body* body = doc.body(b);
      if (!body || (opt.visibleOnly && !body->visible)) continue;
      parts.push_back(doc.bodyShape(b));
      used.push_back(b);
    }
    if (parts.empty()) return TDF_Label();
    // A body carrying a TopLoc_Location (Move Body shares geometry and sets a location) would be
    // added by XCAF as an instance of an unnamed prototype, so the STEP PRODUCT lost the part name
    // ("Open CASCADE STEP translator ..."). Bake the location into the geometry first.
    for (TopoDS_Shape& p : parts)
      if (!p.Location().IsIdentity())
        p = BRepBuilderAPI_Transform(p.Located(TopLoc_Location()), p.Location().Transformation(), Standard_True).Shape();
    TopoDS_Shape shape = parts.size() == 1 ? parts.front() : core::makeCompound(parts);
    TDF_Label label = shapes->AddShape(shape, false, false);
    setName(label, name);
    TDF_Label proto;
    if (XCAFDoc_ShapeTool::GetReferredShape(label, proto)) setName(proto, name);
    ++stats.parts;
    stats.bodies += static_cast<int>(parts.size());
    if (opt.colors) {
      for (size_t i = 0; i < used.size(); ++i) {
        const core::Body* body = doc.body(used[i]);
        Quantity_ColorRGBA rgba(body->color[0], body->color[1], body->color[2], body->color[3]);
        TDF_Label target = label;
        if (parts.size() > 1) {
          target = shapes->AddSubShape(label, parts[i]);
          if (!target.IsNull() && opt.names) setName(target, body->name);
        }
        if (!target.IsNull()) colors->SetColor(target, rgba, XCAFDoc_ColorSurf);
      }
    }
    return label;
  }

  /// Label describing a definition component (shared by all its occurrences).
  TDF_Label labelFor(ObjectId def) {
    auto it = definitionLabels.find(def);
    if (it != definitionLabels.end()) return it->second;
    const core::Component* c = doc.component(def);
    if (!c) return TDF_Label();
    std::vector<ObjectId> children;
    for (ObjectId ch : c->children) {
      const core::Component* cc = doc.component(ch);
      if (cc && (!opt.visibleOnly || cc->visible)) children.push_back(ch);
    }
    TDF_Label result;
    // the root is the design: an unsaved "Untitled" document is named after the STEP file
    const std::string name = c->isRoot() && (c->name.empty() || c->name == "Untitled") && !rootName.empty() ? rootName : c->name;
    if (children.empty()) {
      result = partLabel(def, name);
    } else {
      // assembly node: child instances + (optionally) own bodies as one more component
      TDF_Label asm_ = shapes->NewShape();
      setName(asm_, name);
      bool any = false;
      TDF_Label own = partLabel(def, name + " bodies");
      if (!own.IsNull()) {
        TDF_Label inst = shapes->AddComponent(asm_, own, TopLoc_Location());
        setName(inst, name + " bodies");
        any = true;
      }
      for (ObjectId ch : children) {
        const core::Component* cc = doc.component(ch);
        TDF_Label childDef = labelFor(definitionOf(doc, ch));
        if (childDef.IsNull()) continue;
        TDF_Label inst = shapes->AddComponent(asm_, childDef, TopLoc_Location(cc->placement));
        if (opt.names) setName(inst, componentLabel(doc, ch));
        ++stats.instances;
        any = true;
      }
      result = any ? asm_ : TDF_Label();
    }
    definitionLabels[def] = result;
    return result;
  }
};

}  // namespace

bool exportAssemblyStep(const Document& doc, const std::string& path, const StepExportOptions& opt, std::string* error,
                        StepExportStats* statsOut) {
  try {
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) xdoc;
    app->NewDocument("BinXCAF", xdoc);
    Exporter ex{doc, opt, XCAFDoc_DocumentTool::ShapeTool(xdoc->Main()), XCAFDoc_DocumentTool::ColorTool(xdoc->Main()), {}, {},
                QFileInfo(QString::fromStdString(path)).completeBaseName().toStdString()};
    ObjectId root = doc.component(opt.root) ? opt.root : core::kRootComponentId;
    TDF_Label top = ex.labelFor(definitionOf(doc, root));
    if (top.IsNull()) {
      if (error) *error = "nothing to export (no visible bodies)";
      return false;
    }
    ex.shapes->UpdateAssemblies();
    Interface_Static::SetCVal("write.step.schema", opt.schema.c_str());
    Interface_Static::SetCVal("write.step.unit", "MM");
    STEPCAFControl_Writer writer;
    writer.SetColorMode(opt.colors);
    writer.SetNameMode(opt.names);
    if (!writer.Transfer(xdoc, STEPControl_AsIs)) {
      if (error) *error = "STEP transfer failed";
      return false;
    }
    IFSelect_ReturnStatus st = writer.Write(path.c_str());
    if (st != IFSelect_RetDone) {
      if (error) *error = "cannot write " + path;
      return false;
    }
    if (statsOut) *statsOut = ex.stats;
    FC_INFO("STEP export: {} parts, {} instances, {} bodies -> {}", ex.stats.parts, ex.stats.instances, ex.stats.bodies, path);
    return true;
  } catch (const Standard_Failure& e) {
    if (error) *error = std::string("OCCT: ") + (e.GetMessageString() ? e.GetMessageString() : "failure");
    return false;
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    return false;
  }
}

}  // namespace fabricad::assembly
