// ViewerPalette (docs/plans/ui-redesign.md 4.5): every viewport colour comes from the tokens, the
// preview roles resolve to the two-tone live / cut / new-body colours, manipulators use the axis
// colours and gold, scene items take their palette colours, and the halo rule keeps accent lines
// readable on the shaded body in both themes.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/SceneModel.h"
#include "viewer/ViewerPalette.h"

#include "core/Body.h"
#include "theme/ColorMath.h"

#include <QtTest/QtTest>

using namespace fabricad;
using viewer::SceneStyle;
using viewer::ViewerPalette;
using theme::Theme;
using theme::Tok;

namespace {
theme::Rgba cm(const ViewerPalette::Rgba& c) { return {c[0], c[1], c[2], c[3]}; }
bool same(const ViewerPalette::Rgba& a, const ViewerPalette::Rgba& b) {
  for (int i = 0; i < 4; ++i)
    if (std::abs(a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]) > 1e-5f) return false;
  return true;
}
}  // namespace

class ViewerPaletteTest : public QObject {
  Q_OBJECT
 private slots:
  void fromTokensBothThemes() {
    for (Theme th : {Theme::Dark, Theme::Light}) {
      const ViewerPalette p = ViewerPalette::fromTokens(th);
      QCOMPARE(p.theme, th);
      QVERIFY(same(p.bgTop, theme::rgba(Tok::VpBgTop, th)));
      QVERIFY(same(p.bgBottom, theme::rgba(Tok::VpBgBottom, th)));
      QVERIFY(same(p.gridMinor, theme::rgba(Tok::VpGridMinor, th)));
      QVERIFY(same(p.bodyDefault, theme::rgba(Tok::BodyDefault, th)));
      QVERIFY(same(p.faceEdge, theme::rgba(Tok::BodyEdge, th)));
      QVERIFY(same(p.select, theme::rgba(Tok::SelEdge, th)));
      QVERIFY(same(p.selFill, theme::rgba(Tok::SelFill, th)));
      QVERIFY(same(p.preselect, theme::rgba(Tok::PreEdge, th)));
      QVERIFY(same(p.halo, theme::rgba(Tok::Halo, th)));
      QVERIFY(same(p.axis[0], theme::rgba(Tok::AxisX, th)));
      QVERIFY(same(p.axis[2], theme::rgba(Tok::AxisZ, th)));
      QVERIFY(same(p.manipHandle, theme::rgba(Tok::ManipHandle, th)));
      QVERIFY(same(p.skFree, theme::rgba(Tok::SkFree, th)));
      QVERIFY(same(p.profileFill, theme::rgba(Tok::SkProfileFill, th)));
      QVERIFY(same(p.cubeBox, theme::rgba(Tok::VpCubeFace, th)));
      QVERIFY(same(p.cubeEdge, theme::rgba(Tok::VpCubeEdge, th)));
      // selection is a tint, never an opaque wash (p1 L8: the selected profile looked white)
      QVERIFY(p.selFill[3] < 0.3f);
      QVERIFY(p.profileFill[3] < 0.15f);
      // light borders are heavier (4.5: 2.5 px selection in light)
      QVERIFY(th == Theme::Light ? p.selWidth > 2.4f : p.selWidth < 2.4f);
    }
    // the light palette really differs (ViewCube contrast fix, p0/p1 M1)
    QVERIFY(!same(ViewerPalette::fromTokens(Theme::Dark).cubeBox, ViewerPalette::fromTokens(Theme::Light).cubeBox));
  }

  void lightViewCubeHasContrast() {
    // p1 M1: the light cube was white on the near-white viewport. Its outline (edges, corners)
    // now reads at >= 3:1 on the gradient top, and its labels at >= 4.5:1 on the face.
    for (Theme th : {Theme::Dark, Theme::Light}) {
      const ViewerPalette p = ViewerPalette::fromTokens(th);
      QVERIFY2(theme::contrastRatio(cm(p.cubeEdge), cm(p.bgTop)) >= 3.0, theme::themeName(th));
      QVERIFY2(theme::contrastRatio(cm(p.cubeText), cm(p.cubeBox)) >= 4.5, theme::themeName(th));
    }
  }

  void haloRuleOverTheBody() {
    // 4.5: purple and gold on the grey body are ~1:1; with the halo the accent stroke has a
    // contrasting neighbour on both sides
    for (Theme th : {Theme::Dark, Theme::Light}) {
      const ViewerPalette p = ViewerPalette::fromTokens(th);
      QVERIFY2(theme::contrastRatio(cm(p.select), cm(p.halo)) >= 3.0, theme::themeName(th));
      QVERIFY2(theme::contrastRatio(cm(p.live.edge), cm(p.live.halo)) >= 3.0, theme::themeName(th));
      QVERIFY(p.haloWidth >= 1.0f);
      QVERIFY(p.live.haloOn && p.live.glowOn);
    }
  }

  void previewRoles() {
    const ViewerPalette p = ViewerPalette::fromTokens(Theme::Dark);
    // operations: the preset colour is only a headless fallback
    QVERIFY(same(p.preview(core::PreviewStyle::join()).fill, p.live.fill));
    QVERIFY(same(p.preview(core::PreviewStyle::join()).edge, theme::rgba(Tok::PreviewLiveEdge, Theme::Dark)));
    QVERIFY(same(p.preview(core::PreviewStyle::cut()).edge, p.cut.edge));
    QVERIFY(p.preview(core::PreviewStyle::cut()).dashed);  // the dash is the cut cue
    QVERIFY(same(p.preview(core::PreviewStyle::newBody()).fill, p.newBody.fill));
    // intersect: a recoloured join keeps the live role
    core::PreviewStyle inter = core::PreviewStyle::join();
    inter.color = {0.9f, 0.75f, 0.2f, 0.5f};
    QVERIFY(same(p.preview(inter).fill, p.live.fill));
    // neutral roles: the palette colour, unless the tool picked its own
    QVERIFY(same(p.preview(core::PreviewStyle::sketchLine()).edge, p.skFree));
    QVERIFY(same(p.preview(core::PreviewStyle::inference()).edge, p.inference));
    core::PreviewStyle own = core::PreviewStyle::inference();
    own.color = {0.1f, 0.2f, 0.3f, 1.0f};
    QVERIFY(same(p.preview(own).edge, own.color));
    // no role: the style's colour, alpha on the fill
    core::PreviewStyle plain;
    plain.color = {0.5f, 0.25f, 0.75f, 0.4f};
    QVERIFY(same(p.preview(plain).fill, plain.color));
    QCOMPARE(p.preview(plain).edge[3], 1.0f);
    // light and dark resolve differently (light edge is the dark ochre, with a white halo)
    const ViewerPalette l = ViewerPalette::fromTokens(Theme::Light);
    QVERIFY(!same(l.preview(core::PreviewStyle::join()).edge, p.preview(core::PreviewStyle::join()).edge));
  }

  void manipulatorColours() {
    using core::ManipulatorHandle;
    using core::ManipulatorKind;
    const ViewerPalette p = ViewerPalette::fromTokens(Theme::Dark);
    QVERIFY(same(p.manipulatorColor(ManipulatorKind::Linear, ManipulatorHandle::MoveZ, false, false), p.manipHandle));
    QVERIFY(same(p.manipulatorColor(ManipulatorKind::Linear, ManipulatorHandle::MoveZ, true, false), p.manipBright));
    QVERIFY(same(p.manipulatorColor(ManipulatorKind::Linear, ManipulatorHandle::MoveZ, false, true), p.manipGuide));
    QVERIFY(same(p.manipulatorColor(ManipulatorKind::Triad, ManipulatorHandle::MoveX, false, false), p.axis[0]));
    QVERIFY(same(p.manipulatorColor(ManipulatorKind::Triad, ManipulatorHandle::RotY, false, false), p.axis[1]));
    QVERIFY(same(p.manipulatorColor(ManipulatorKind::Triad, ManipulatorHandle::MoveZ, false, false), p.axis[2]));
    QVERIFY(p.manipulatorColor(ManipulatorKind::Triad, ManipulatorHandle::PlaneXY, false, false)[3] < 0.6f);
  }

  void sceneColours() {
    for (Theme th : {Theme::Dark, Theme::Light}) {
      const ViewerPalette p = ViewerPalette::fromTokens(th);
      QVERIFY(same(viewer::styleColor(SceneStyle::SketchFree, p), p.skFree));
      QVERIFY(same(viewer::styleColor(SceneStyle::SketchDefined, p), p.skConstrained));
      QVERIFY(same(viewer::styleColor(SceneStyle::SketchConstruction, p), p.skConstruction));
      QVERIFY(same(viewer::styleColor(SceneStyle::Profile, p), p.profilePick));
      QVERIFY(same(viewer::styleColor(SceneStyle::ProfileShade, p), p.profileFill));
      // the document's default body colour becomes body.default; a chosen colour stays
      QVERIFY(same(viewer::bodyColor(core::Body().color, p), p.bodyDefault));
      const ViewerPalette::Rgba red{1.0f, 0.0f, 0.0f, 1.0f};
      QVERIFY(same(viewer::bodyColor(red, p), red));
    }
  }
};

QTEST_GUILESS_MAIN(ViewerPaletteTest)
#include "test_viewer_palette.moc"
