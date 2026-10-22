// FabriCAD part: parametric primitives (Fusion CREATE > Box / Cylinder / Sphere / Torus / Coil).
// Each primitive sits on a plane (origin plane, construction plane, sketch or planar face;
// default XY) at a position given in the plane's 2D coordinates, and joins / cuts /
// intersects bodies or makes a new body like Extrude.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

#include <gp_Ax3.hxx>

#include <optional>

namespace fabricad::part {

class PrimitiveFeature : public PartFeature {
 public:
  enum Direction { OneSide = 0, Symmetric = 1 };
  using PartFeature::PartFeature;
  void compute(core::RecomputeContext& ctx, Outcome& out) final;
  std::string primaryInput() const override { return "plane"; }

  /// Solid in the local frame (plane origin moved to the position, Z = plane normal). Names
  /// are relative ("Top", "Side"...) and get the feature name as prefix. Null + ctx error on failure.
  virtual TopoDS_Shape makeLocal(core::RecomputeContext& ctx, NameMap& names) = 0;

 protected:
  /// plane, position x/y (common head of every primitive's property list)
  void addPlacementProperties();
  /// operation + targets (common tail)
  void addTail();
};

class BoxFeature : public PrimitiveFeature {
 public:
  static constexpr const char* kType = "Box";
  BoxFeature();
  TopoDS_Shape makeLocal(core::RecomputeContext& ctx, NameMap& names) override;
  std::string primaryNumeric() const override { return "height"; }
};

class CylinderFeature : public PrimitiveFeature {
 public:
  static constexpr const char* kType = "Cylinder";
  CylinderFeature();
  TopoDS_Shape makeLocal(core::RecomputeContext& ctx, NameMap& names) override;
  std::string primaryNumeric() const override { return "height"; }
};

class SphereFeature : public PrimitiveFeature {
 public:
  static constexpr const char* kType = "Sphere";
  SphereFeature();
  TopoDS_Shape makeLocal(core::RecomputeContext& ctx, NameMap& names) override;
  std::string primaryNumeric() const override { return "diameter"; }
};

class TorusFeature : public PrimitiveFeature {
 public:
  static constexpr const char* kType = "Torus";
  TorusFeature();
  TopoDS_Shape makeLocal(core::RecomputeContext& ctx, NameMap& names) override;
  std::string primaryNumeric() const override { return "majorDiameter"; }
  /// A new torus always starts valid: the tube is clamped below the ring (PART-04).
  void applyDefaultsForNewFeature(const core::Document& doc) override;
};

/// Coil (helical sweep): Revolution and Height / Revolution and Pitch / Height and Pitch;
/// circular, square or triangular section inside / on / outside the coil diameter; taper angle;
/// clockwise or counter-clockwise.
class CoilFeature : public PrimitiveFeature {
 public:
  static constexpr const char* kType = "Coil";
  enum CoilType { RevolutionHeight = 0, RevolutionPitch = 1, HeightPitch = 2 };
  enum Section { Circular = 0, Square = 1, TriangularExternal = 2, TriangularInternal = 3 };
  enum SectionPosition { Inside = 0, OnCenter = 1, Outside = 2 };
  CoilFeature();
  TopoDS_Shape makeLocal(core::RecomputeContext& ctx, NameMap& names) override;
  void updatePropertyFlags() override;
  std::string primaryNumeric() const override { return "diameter"; }

  /// Resolved helix parameters for the current inputs (after evaluation): turns, pitch, height.
  struct Helix {
    double turns = 0, pitch = 0, height = 0;
  };
  static std::optional<Helix> helixFor(int type, double revolutions, double height, double pitch, std::string* error);
};

}  // namespace fabricad::part
