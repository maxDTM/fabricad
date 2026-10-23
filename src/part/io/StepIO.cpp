// FabriCAD part I/O: STEP / IGES import options and report, STEP schema, product properties.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/io/StepIO.h"

#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS_Compound.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <Interface_Static.hxx>
#include <STEPCAFControl_Controller.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <StepBasic_Product.hxx>
#include <StepBasic_ProductDefinition.hxx>
#include <StepBasic_ProductDefinitionFormation.hxx>
#include <StepData_StepModel.hxx>
#include <StepRepr_CharacterizedDefinition.hxx>
#include <StepRepr_DescriptiveRepresentationItem.hxx>
#include <StepRepr_HArray1OfRepresentationItem.hxx>
#include <StepRepr_PropertyDefinition.hxx>
#include <StepRepr_PropertyDefinitionRepresentation.hxx>
#include <StepRepr_Representation.hxx>
#include <StepRepr_RepresentationContext.hxx>
#include <StepRepr_RepresentedDefinition.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <XSControl_WorkSession.hxx>

#include <QSettings>

#include <format>

namespace fabricad::part::io {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string str(const Handle(TCollection_HAsciiString)& h) { return h.IsNull() ? std::string() : std::string(h->ToCString()); }
Handle(TCollection_HAsciiString) hs(const std::string& s) { return new TCollection_HAsciiString(s.c_str()); }

bool onOff(const std::string& v) { return !(v == "off" || v == "0" || v == "no" || v == "false"); }

/// The property keys written as PROPERTY_DEFINITIONs (product id / description / formation id
/// carry part number, description and revision too).
const char* const kKeys[] = {"partNumber", "description", "material", "revision"};

std::string& field(ProductProperties& p, const std::string& key) {
  if (key == "partNumber" || key == "part number" || key == "part_number") return p.partNumber;
  if (key == "description") return p.description;
  if (key == "material") return p.material;
  return p.revision;
}

}  // namespace

std::string ImportReport::text() const {
  std::string t = std::format("Import report: {} ({})\n", file, format);
  if (!schema.empty()) t += std::format("  schema: {}\n", schema);
  t += std::format("  entities: {}\n  roots: {}\n  shapes: {}\n  solids: {}\n  shells: {}\n  faces: {}\n", entities, roots, shapes, solids,
                   shells, faces);
  t += std::format("  open shells (surface bodies): {}\n", openShells);
  t += std::format("  failed faces: {}\n  BRepCheck: {}\n", failedFaces, valid ? "valid" : "INVALID");
  t += std::format("  sew: {}  fix: {}  unify: {}\n", sewn ? "applied" : "off", fixed ? "applied" : "off", unified ? "applied" : "off");
  for (const auto& n : notes) t += "  note: " + n + "\n";
  return t;
}

void reportModel(const Handle(XSControl_WorkSession)& ws, ImportReport& r) {
  if (ws.IsNull() || ws->Model().IsNull()) return;
  r.entities = ws->Model()->NbEntities();
}

void healShapes(std::vector<TopoDS_Shape>& shapes, const ImportOptions& opt, ImportReport& r) {
  std::vector<TopoDS_Shape> out;
  for (TopoDS_Shape s : shapes) {
    if (s.IsNull()) continue;
    if (opt.sew) {
      // loose faces / open shells: sew, then closed shells become solids
      bool hasSolid = false;
      for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) hasSolid = true;
      if (!hasSolid) {
        BRepBuilderAPI_Sewing sew(opt.sewTolerance);
        sew.Add(s);
        sew.Perform();
        TopoDS_Shape sewn = sew.SewedShape();
        if (!sewn.IsNull()) {
          std::vector<TopoDS_Shape> solids, open;
          for (TopExp_Explorer ex(sewn, TopAbs_SHELL); ex.More(); ex.Next()) {
            const TopoDS_Shell& sh = TopoDS::Shell(ex.Current());
            BRepCheck_Analyzer ca(sh);
            if (BRep_Tool::IsClosed(sh)) {
              BRepBuilderAPI_MakeSolid ms(sh);
              if (ms.IsDone()) {
                solids.push_back(ms.Solid());
                continue;
              }
            }
            open.push_back(sh);
          }
          if (!solids.empty() || !open.empty()) {
            BRep_Builder b;
            TopoDS_Compound c;
            b.MakeCompound(c);
            for (auto& x : solids) b.Add(c, x);
            for (auto& x : open) b.Add(c, x);
            s = (solids.size() + open.size() == 1) ? (solids.empty() ? open.front() : solids.front()) : TopoDS_Shape(c);
          } else {
            s = sewn;
          }
          r.sewn = true;
        }
      }
    }
    if (opt.fix) {
      ShapeFix_Shape fix(s);
      fix.Perform();
      if (!fix.Shape().IsNull()) s = fix.Shape();
      r.fixed = true;
    }
    if (opt.unify) {
      ShapeUpgrade_UnifySameDomain u(s, Standard_True, Standard_True, Standard_False);
      u.Build();
      if (!u.Shape().IsNull()) s = u.Shape();
      r.unified = true;
    }
    out.push_back(s);
  }
  shapes = std::move(out);
  r.shapes = static_cast<int>(shapes.size());
  r.solids = r.shells = r.faces = r.failedFaces = r.openShells = 0;
  r.valid = true;
  for (const TopoDS_Shape& s : shapes) {
    for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) ++r.solids;
    for (TopExp_Explorer ex(s, TopAbs_SHELL); ex.More(); ex.Next()) {
      ++r.shells;
      if (!BRep_Tool::IsClosed(ex.Current())) ++r.openShells;
    }
    for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
      ++r.faces;
      BRepCheck_Analyzer fa(ex.Current(), Standard_False);
      if (!fa.IsValid()) ++r.failedFaces;
    }
    BRepCheck_Analyzer a(s);
    if (!a.IsValid()) r.valid = false;
  }
}

bool applyImportOption(const std::string& word, ImportOptions& o) {
  const std::string w = lower(word);
  const size_t eq = w.find('=');
  const std::string key = eq == std::string::npos ? w : w.substr(0, eq);
  const bool v = eq == std::string::npos || onOff(w.substr(eq + 1));
  if (key == "sew") o.sew = v;
  else if (key == "fix") o.fix = v;
  else if (key == "unify") o.unify = v;
  else if (key == "nosew") o.sew = false;
  else if (key == "nofix") o.fix = false;
  else return false;
  return true;
}

std::string normalizeStepSchema(const std::string& text) {
  std::string t = lower(text);
  if (t.starts_with("ap")) t = t.substr(2);
  if (t == "203") return "AP203";
  if (t == "214" || t == "214is") return "AP214";
  if (t == "242" || t == "242dis") return "AP242";
  return {};
}

std::string stepSchemaDefault() {
  const std::string s = normalizeStepSchema(QSettings().value("export/step/schema", "AP214").toString().toStdString());
  return s.empty() ? "AP214" : s;
}

void setStepSchemaDefault(const std::string& schema) {
  const std::string s = normalizeStepSchema(schema);
  QSettings().setValue("export/step/schema", QString::fromStdString(s.empty() ? "AP214" : s));
}

bool applyStepSchemaOption(const std::string& word, std::string& schema) {
  std::string w = lower(word);
  if (w.starts_with("schema=")) w = w.substr(7);
  else if (!w.starts_with("ap")) return false;
  const std::string s = normalizeStepSchema(w);
  if (s.empty()) return false;
  schema = s;
  return true;
}

void setStepWriterSchema(const std::string& schema) {
  // the statics exist once the STEP controller is initialised (normally by the first writer)
  STEPCAFControl_Controller::Init();
  const std::string s = normalizeStepSchema(schema);
  if (s == "AP203") Interface_Static::SetIVal("write.step.schema", 3);
  else if (s == "AP242") Interface_Static::SetIVal("write.step.schema", 5);
  else Interface_Static::SetCVal("write.step.schema", "AP214");  // what FabriCAD always wrote
}

int writeProductProperties(const Handle(StepData_StepModel)& model, const std::map<std::string, ProductProperties>& byName) {
  if (model.IsNull() || byName.empty()) return 0;
  Handle(StepRepr_RepresentationContext) context;
  std::vector<Handle(StepBasic_ProductDefinition)> defs;
  const int n = model->NbEntities();
  for (int i = 1; i <= n; ++i) {
    Handle(Standard_Transient) e = model->Value(i);
    if (context.IsNull()) context = Handle(StepRepr_RepresentationContext)::DownCast(e);
    if (auto pd = Handle(StepBasic_ProductDefinition)::DownCast(e)) defs.push_back(pd);
  }
  if (context.IsNull()) return 0;
  int updated = 0;
  for (const auto& pd : defs) {
    Handle(StepBasic_ProductDefinitionFormation) form = pd->Formation();
    if (form.IsNull() || form->OfProduct().IsNull()) continue;
    Handle(StepBasic_Product) product = form->OfProduct();
    auto it = byName.find(str(product->Name()));
    if (it == byName.end() || it->second.empty()) continue;
    const ProductProperties& p = it->second;
    if (!p.partNumber.empty()) product->SetId(hs(p.partNumber));
    if (!p.description.empty()) product->SetDescription(hs(p.description));
    if (!p.revision.empty()) form->SetId(hs(p.revision));
    ProductProperties copy = p;
    for (const char* key : kKeys) {
      const std::string& value = field(copy, key);
      if (value.empty()) continue;
      StepRepr_CharacterizedDefinition cd;
      cd.SetValue(pd);
      Handle(StepRepr_PropertyDefinition) prop = new StepRepr_PropertyDefinition;
      prop->Init(hs(key), Standard_True, hs(""), cd);
      Handle(StepRepr_DescriptiveRepresentationItem) item = new StepRepr_DescriptiveRepresentationItem;
      item->Init(hs(key), hs(value));
      Handle(StepRepr_HArray1OfRepresentationItem) items = new StepRepr_HArray1OfRepresentationItem(1, 1);
      items->SetValue(1, item);
      Handle(StepRepr_Representation) rep = new StepRepr_Representation;
      rep->Init(hs(key), items, context);
      StepRepr_RepresentedDefinition rd;
      rd.SetValue(prop);
      Handle(StepRepr_PropertyDefinitionRepresentation) pdr = new StepRepr_PropertyDefinitionRepresentation;
      pdr->Init(rd, rep);
      model->AddWithRefs(pdr);
    }
    ++updated;
  }
  return updated;
}

std::map<std::string, ProductProperties> readProductProperties(const Handle(StepData_StepModel)& model) {
  std::map<std::string, ProductProperties> out;
  if (model.IsNull()) return out;
  const int n = model->NbEntities();
  for (int i = 1; i <= n; ++i) {
    Handle(Standard_Transient) e = model->Value(i);
    if (auto pd = Handle(StepBasic_ProductDefinition)::DownCast(e)) {
      Handle(StepBasic_ProductDefinitionFormation) form = pd->Formation();
      if (form.IsNull() || form->OfProduct().IsNull()) continue;
      Handle(StepBasic_Product) product = form->OfProduct();
      ProductProperties& p = out[str(product->Name())];
      // product id / description / formation id: only when they say something beyond the name
      const std::string id = str(product->Id()), name = str(product->Name()), desc = str(product->Description());
      if (p.partNumber.empty() && !id.empty() && id != name) p.partNumber = id;
      if (p.description.empty() && !desc.empty() && desc != name) p.description = desc;
      const std::string rev = str(form->Id());
      if (p.revision.empty() && !rev.empty() && rev != "" && rev != name && rev != "1" && rev != "None" && rev != "ANY") p.revision = rev;
      continue;
    }
    auto pdr = Handle(StepRepr_PropertyDefinitionRepresentation)::DownCast(e);
    if (pdr.IsNull()) continue;
    auto prop = Handle(StepRepr_PropertyDefinition)::DownCast(pdr->Definition().Value());
    if (prop.IsNull()) continue;
    Handle(StepBasic_ProductDefinition) pd = prop->Definition().ProductDefinition();
    if (pd.IsNull() || pd->Formation().IsNull() || pd->Formation()->OfProduct().IsNull()) continue;
    const std::string key = str(prop->Name());
    if (key != "partNumber" && key != "description" && key != "material" && key != "revision" && key != "part number") continue;
    Handle(StepRepr_Representation) rep = pdr->UsedRepresentation();
    if (rep.IsNull() || rep->Items().IsNull()) continue;
    for (int k = rep->Items()->Lower(); k <= rep->Items()->Upper(); ++k) {
      auto item = Handle(StepRepr_DescriptiveRepresentationItem)::DownCast(rep->Items()->Value(k));
      if (item.IsNull()) continue;
      field(out[str(pd->Formation()->OfProduct()->Name())], key) = str(item->Description());
      break;
    }
  }
  for (auto it = out.begin(); it != out.end();) it = it->second.empty() ? out.erase(it) : std::next(it);
  return out;
}

}  // namespace fabricad::part::io
