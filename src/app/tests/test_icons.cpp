// Icon set tests: every registered tool id and feature type resolves to real SVG artwork (no
// generated badge), every SVG follows the style rules of resources/icons/README.md, renders
// legibly at 16/24/32 px, and the contact sheet docs/screenshots/icons.png is reproducible.
//
// Test shape (fixed so runs are repeatable and failures are comparable between runs):
//   Inputs   - the tool registry and feature factory exactly as the application builds them
//              (MainWindow registers sketch, part, assembly, drawing, then the app tools),
//              the joint types 0..kJointTypeCount-1, the fixed browser/timeline/UI icon list
//              kUiIcons below, and every *.svg under resources/icons/{dark,light} (source tree, found
//              relative to this file) and :/icons/{dark,light} (compiled qrc).
//   Order    - all collections are sorted by id/name before checking, so messages and the
//              contact sheet never depend on registration or hash order.
//   Checks   - each check collects ALL offenders and fails once with the full sorted list.
//   Rendering- QSvgRenderer into ARGB32_Premultiplied QImages, transparent background, no
//              QIcon pixmap cache or platform theme involved; sizes kRenderSizes; thresholds
//              are the named constants below.
//   Output   - the contact sheets icons-dark.png / icons-light.png are written to
//              $FABRICAD_ICON_SHEET_DIR if set (use docs/screenshots to refresh the committed
//              copies), else next to the test binary. It is rendered twice and
//              must be pixel-identical (labels use a fixed pixel-size font).
// No QSettings, documents, timing or network are involved; the test needs only
// QT_QPA_PLATFORM=offscreen.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/Icons.h"
#include "app/MainWindow.h"
#include "assembly/JointFeature.h"
#include "assembly/JointTypes.h"
#include "core/Feature.h"
#include "core/Registry.h"
#include "core/Tool.h"
#include "theme/Tokens.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <QtTest>

#include <map>
#include <set>

using namespace fabricad;

namespace {

/// Sizes every icon is rendered at (menu, timeline, toolbar).
constexpr int kRenderSizes[] = {16, 24, 32};
/// Minimum share of pixels with alpha >= kInkAlpha at each size (blank / hairline guard).
constexpr double kMinInk = 0.04;
/// Maximum share of covered pixels (alpha > 0): line icons must stay open, not blobs.
constexpr double kMaxCover = 0.80;
constexpr int kInkAlpha = 128;
/// The single stroke width of the set (resources/icons/README.md).
const QString kStrokeWidth = "1.5";
/// Files exempt from the line-icon style rules: the application logo (window icon) is a
/// filled, gradient badge by design. It must still parse and render.
const QSet<QString> kStyleExempt = {"fabricad.svg"};

constexpr theme::Theme kThemes[] = {theme::Theme::Dark, theme::Theme::Light};
/// Minimum WCAG contrast (non-text, 3:1) of every colour an icon draws marks with, against the
/// surfaces icons sit on: toolbar / ribbon (header), sidebar (panel) and raised cards.
constexpr double kMinMarkContrast = 3.0;
constexpr theme::Tok kIconSurfaces[] = {theme::Tok::SurfaceHeader, theme::Tok::SurfacePanel, theme::Tok::SurfaceRaised};

/// The icon palette of a theme (tokens icon.*; resources/icons/README.md). Anything else is a
/// style violation. Fills only (paper, body) may be low-contrast: they are always outlined.
std::set<QString> paletteOf(theme::Theme th) {
  using T = theme::Tok;
  std::set<QString> s;
  for (T t : {T::IconInk, T::IconGrey, T::IconPaper, T::IconBody, T::IconBadgeBlue, T::IconOrange, T::IconBadgeGreen,
              T::IconBadgeAmber, T::IconBadgeViolet, T::IconBadgeCyan, T::IconBadgeTeal, T::IconBadgeRed})
    s.insert(theme::color(t, th).name().toUpper());
  return s;
}
/// Palette colours that are fills only.
std::set<QString> fillOnlyOf(theme::Theme th) {
  return {theme::color(theme::Tok::IconPaper, th).name().toUpper(), theme::color(theme::Tok::IconBody, th).name().toUpper()};
}

double luminance(const QColor& c) {
  auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
  return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}
double contrast(const QColor& a, const QColor& b) {
  double la = luminance(a), lb = luminance(b);
  if (la < lb) std::swap(la, lb);
  return (la + 0.05) / (lb + 0.05);
}

QString themeDirName(theme::Theme th) { return QString::fromLatin1(theme::themeName(th)); }
/// Source / compiled directory of a theme's SVGs.
QString qrcDir(theme::Theme th) { return ":/icons/" + themeDirName(th); }

/// Icons the shell uses directly (browser, timeline, nav bar, app bar, menus, dialogs).
const QStringList kUiIcons = {
    // browser
    "document", "folder", "body", "component", "origin", "plane", "axis", "point", "sketch", "feature", "eye",
    "eye-off", "visibility", "error", "warning", "delete",
    // timeline
    "timeline-first", "timeline-prev", "timeline-next", "timeline-last",
    // app bar / file menu / ribbon
    "fabricad", "file-menu", "new", "open", "save", "save-as", "undo", "redo", "help", "search", "settings",
    "workspace-design", "workspace-drawing", "finish-sketch", "parameters", "compute", "command-line",
    // nav bar / view menu
    "orbit", "pan", "zoom", "fit", "home", "look-at", "views", "display", "grid",
    // selection filters
    "select", "select-body", "select-component", "select-face", "select-edge", "select-vertex",
    // dialogs
    "ok", "cancel", "joint",
    // command card segmented options (part PropertyPanel segmentIcons)
    "option-one-side", "option-symmetric", "option-two-sides", "option-join", "option-cut", "option-intersect",
    "option-new-body",
};

QString sourceIconDir() {
  // this file is src/app/tests/test_icons.cpp
  return QFileInfo(QStringLiteral(__FILE__)).absoluteDir().absoluteFilePath("../../../resources/icons");
}

QString joinSorted(std::set<QString> s) {
  QStringList l(s.begin(), s.end());
  return l.join("\n  ");
}

QString normColor(QString c) {
  c = c.trimmed();
  if (c.startsWith('#') && c.size() == 4) c = QString("#%1%1%2%2%3%3").arg(c[1]).arg(c[2]).arg(c[3]);
  return c.toUpper();
}

/// Style violations of one SVG source (empty = ok).
QStringList lintSvg(const QByteArray& data, theme::Theme th) {
  const std::set<QString> kPalette = paletteOf(th);
  const QString ink = theme::color(theme::Tok::IconInk, th).name().toUpper();
  QStringList bad;
  QXmlStreamReader x(data);
  bool root = true;
  while (!x.atEnd()) {
    if (x.readNext() != QXmlStreamReader::StartElement) continue;
    const QString tag = x.name().toString();
    const QXmlStreamAttributes a = x.attributes();
    if (root) {
      root = false;
      if (tag != "svg") bad << "root element is not <svg>";
      if (a.value("viewBox") != QLatin1String("0 0 24 24")) bad << "viewBox must be \"0 0 24 24\"";
      if (a.value("width") != QLatin1String("24") || a.value("height") != QLatin1String("24"))
        bad << "width/height must be 24";
      if (a.value("stroke-width") != kStrokeWidth) bad << "root stroke-width must be " + kStrokeWidth;
      if (a.value("fill") != QLatin1String("none")) bad << "root fill must be none";
      if (normColor(a.value("stroke").toString()) != ink) bad << "root stroke must be icon.ink " + ink;
      if (a.value("stroke-linecap") != QLatin1String("round") || a.value("stroke-linejoin") != QLatin1String("round"))
        bad << "root needs round caps and joins";
    }
    static const QSet<QString> kTags = {"svg", "g", "path", "circle", "ellipse", "rect", "line", "polyline", "polygon"};
    if (!kTags.contains(tag)) bad << "element <" + tag + "> not allowed (no text, images, gradients, defs)";
    for (const QXmlStreamAttribute& at : a) {
      const QString n = at.name().toString();
      const QString v = at.value().toString();
      if (n == "style" || n == "class") bad << "attribute '" + n + "' not allowed (use presentation attributes)";
      if (n == "stroke-width" && v != kStrokeWidth) bad << "<" + tag + "> stroke-width=" + v;
      if (n == "stroke" || n == "fill") {
        if (v == "none") continue;
        if (!kPalette.count(normColor(v))) bad << "<" + tag + "> " + n + "=" + v + " not in palette";
      }
      if (n == "opacity" || n == "fill-opacity" || n == "stroke-opacity") {
        bool ok = false;
        const double o = v.toDouble(&ok);
        if (!ok || o < 0 || o > 1) bad << n + "=" + v;
      }
      if (n == "href" || v.contains("url(")) bad << "external/paint-server references not allowed";
    }
  }
  if (x.hasError()) bad << "XML error: " + x.errorString();
  bad.removeDuplicates();
  return bad;
}

QImage renderSvg(QSvgRenderer& r, int size) {
  QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  r.render(&p, QRectF(0, 0, size, size));
  p.end();
  return img;
}

struct Coverage {
  double ink = 0, cover = 0;
};

Coverage coverage(const QImage& img) {
  int ink = 0, cover = 0;
  for (int y = 0; y < img.height(); ++y)
    for (int xx = 0; xx < img.width(); ++xx) {
      const int al = qAlpha(img.pixel(xx, y));
      if (al > 0) ++cover;
      if (al >= kInkAlpha) ++ink;
    }
  const double n = double(img.width()) * img.height();
  return {ink / n, cover / n};
}

struct SheetEntry {
  QString label;  ///< tool id / feature type / ui name
  QString path;   ///< resolved resource path
};

/// Colours an SVG draws marks with (strokes, and fills of elements without a stroke), uppercase.
std::set<QString> markColours(const QByteArray& data) {
  std::set<QString> out;
  QXmlStreamReader x(data);
  bool root = true;
  while (!x.atEnd()) {
    if (x.readNext() != QXmlStreamReader::StartElement) continue;
    const QXmlStreamAttributes a = x.attributes();
    if (root) {
      root = false;
      out.insert(normColor(a.value("stroke").toString()));
      continue;
    }
    const QString st = a.value("stroke").toString(), fi = a.value("fill").toString();
    if (!st.isEmpty() && st != "none") out.insert(normColor(st));
    if (!fi.isEmpty() && fi != "none" && st == "none") out.insert(normColor(fi));
  }
  return out;
}

/// Deterministic contact sheet: sections in the given order, entries as given (callers sort),
/// 4 columns of 262x44 cells, each with the 32 px and 16 px rendering and the label.
QImage contactSheet(const std::vector<std::pair<QString, std::vector<SheetEntry>>>& sections, theme::Theme th) {
  constexpr int kCols = 4, kCellW = 262, kCellH = 44, kHeadH = 30, kMargin = 12;
  int rows = 0;
  for (const auto& s : sections) rows += int((s.second.size() + kCols - 1) / kCols);
  const int w = kMargin * 2 + kCols * kCellW;
  const int h = kMargin * 2 + int(sections.size()) * kHeadH + rows * kCellH;
  QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
  img.fill(theme::color(theme::Tok::SurfacePanel, th));
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  QFont head;
  head.setFamily("DejaVu Sans");
  head.setPixelSize(14);
  head.setBold(true);
  QFont label;
  label.setFamily("DejaVu Sans");
  label.setPixelSize(11);
  int y = kMargin;
  for (const auto& [title, entries] : sections) {
    p.setFont(head);
    p.setPen(theme::color(theme::Tok::TextPrimary, th));
    p.drawText(QRect(kMargin, y, w - 2 * kMargin, kHeadH), Qt::AlignLeft | Qt::AlignVCenter,
               QString("%1 (%2)").arg(title).arg(entries.size()));
    p.setPen(theme::color(theme::Tok::BorderDefault, th));
    p.drawLine(kMargin, y + kHeadH - 3, w - kMargin, y + kHeadH - 3);
    y += kHeadH;
    for (size_t i = 0; i < entries.size(); ++i) {
      const int cx = kMargin + int(i % kCols) * kCellW;
      const int cy = y + int(i / kCols) * kCellH;
      QSvgRenderer r(app::themedIconPath(entries[i].path, th));
      r.render(&p, QRectF(cx + 4, cy + 6, 32, 32));
      r.render(&p, QRectF(cx + 42, cy + 14, 16, 16));
      p.setFont(label);
      p.setPen(theme::color(theme::Tok::TextPrimary, th));
      p.drawText(QRect(cx + 64, cy, kCellW - 68, kCellH), Qt::AlignLeft | Qt::AlignVCenter, entries[i].label);
    }
    y += int((entries.size() + kCols - 1) / kCols) * kCellH;
  }
  p.end();
  return img;
}

}  // namespace

class TestIcons : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<app::MainWindow> w_;
  /// id -> ToolInfo copy, sorted by id.
  std::map<std::string, core::ToolInfo> tools_;
  /// feature type -> (iconName, folder), plus "Joint:<type>" variants.
  std::map<std::string, std::pair<std::string, std::string>> features_;

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_icons");
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
    w_ = std::make_unique<app::MainWindow>();  // registers every module + the app tools
    w_->setInteractive(false);
    for (core::ITool* t : w_->tools().tools()) tools_[t->info().id] = t->info();
    QVERIFY2(tools_.size() > 100, "expected the full application tool set");
    auto& ff = core::FeatureFactory::instance();
    for (const std::string& type : ff.types()) {
      auto f = ff.create(type);
      QVERIFY(f);
      features_[type] = {f->iconName(), f->browserFolder()};
    }
    for (int i = 0; i < assembly::kJointTypeCount; ++i) {
      assembly::JointFeature j;
      j.setJointType(static_cast<assembly::JointType>(i));
      features_[std::string("Joint:") + assembly::jointTypeName(j.jointType())] = {j.iconName(), j.browserFolder()};
    }
  }

  void cleanupTestCase() {
    theme::setCurrent(theme::Theme::Dark);
    w_.reset();
  }

  /// The badge fallback itself still works (plug-in tools without artwork).
  void badgeFallback() {
    core::ToolInfo i;
    i.id = "plugin.does-not-exist";
    i.name = "Frobnicate Widget";
    QVERIFY(app::toolIconPath(i).isEmpty());
    QVERIFY(!app::toolIcon(i).isNull());
    QCOMPARE(app::badgeText("Frobnicate Widget"), QString("FW"));
  }

  void everyToolHasAnIcon() {
    std::set<QString> missing;
    for (const auto& [id, info] : tools_)
      if (app::toolIconPath(info).isEmpty())
        missing.insert(QString::fromStdString(id) + "  (expected resources/icons/" +
                       QString::fromStdString(id).replace('.', '-') + ".svg)");
    QVERIFY2(missing.empty(), qPrintable("tools without an icon:\n  " + joinSorted(missing)));
  }

  /// Every tool resolves to its own file, so no two commands look the same.
  void toolIconsAreDistinct() {
    std::map<QString, std::set<QString>> byHash;
    for (const auto& [id, info] : tools_) {
      const QString path = app::toolIconPath(info);
      if (path.isEmpty()) continue;  // reported by everyToolHasAnIcon
      QFile f(app::themedIconPath(path, theme::Theme::Dark));
      QVERIFY(f.open(QIODevice::ReadOnly));
      byHash[QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex()].insert(
          QString::fromStdString(id));
    }
    std::set<QString> dup;
    for (const auto& [h, ids] : byHash)
      if (ids.size() > 1) dup.insert(QStringList(ids.begin(), ids.end()).join(", "));
    QVERIFY2(dup.empty(), qPrintable("tools sharing identical artwork:\n  " + joinSorted(dup)));
  }

  void everyFeatureTypeHasAnIcon() {
    std::set<QString> missing;
    for (const auto& [type, v] : features_) {
      const std::string t = type.substr(0, type.find(':'));
      if (app::featureIconPath(v.first, t, v.second).isEmpty())
        missing.insert(QString::fromStdString(type) + "  (iconName '" + QString::fromStdString(v.first) + "')");
    }
    QVERIFY2(missing.empty(), qPrintable("feature types without an icon:\n  " + joinSorted(missing)));
    QVERIFY(features_.count("Extrude") && features_.count("Sketch") && features_.count("Joint:Revolute"));
  }

  void uiIconsExist() {
    std::set<QString> missing;
    for (const QString& n : kUiIcons)
      if (app::resourceIconPath(n).isEmpty()) missing.insert(n);
    // literal resourceIcon("name") uses anywhere in src/app
    const QDir appDir(QFileInfo(QStringLiteral(__FILE__)).absoluteDir().absoluteFilePath(".."));
    const QRegularExpression re(R"re(resourceIcon\("([a-z0-9\-]+)"\))re");
    for (const QString& fn : appDir.entryList({"*.cpp"}, QDir::Files, QDir::Name)) {
      QFile f(appDir.absoluteFilePath(fn));
      QVERIFY(f.open(QIODevice::ReadOnly));
      for (auto it = re.globalMatch(QString::fromUtf8(f.readAll())); it.hasNext();) {
        const QString n = it.next().captured(1);
        if (app::resourceIconPath(n).isEmpty()) missing.insert(n + "  (" + fn + ")");
      }
    }
    QVERIFY2(missing.empty(), qPrintable("UI icons missing:\n  " + joinSorted(missing)));
  }

  /// resources/icons/<theme>/*.svg and the compiled :/icons/<theme> are the same set (qrc kept in
  /// sync), and both themes have exactly the same icon names.
  void qrcMatchesSourceTree() {
    std::map<theme::Theme, std::set<QString>> names;
    for (theme::Theme th : kThemes) {
      std::set<QString> src, qrc;
      for (const QString& f : QDir(sourceIconDir() + "/" + themeDirName(th)).entryList({"*.svg"}, QDir::Files)) src.insert(f);
      for (const QString& f : QDir(qrcDir(th)).entryList({"*.svg"}, QDir::Files)) qrc.insert(f);
      QVERIFY2(!src.empty(), qPrintable("no SVGs found in " + sourceIconDir() + "/" + themeDirName(th)));
      std::set<QString> onlySrc, onlyQrc;
      std::set_difference(src.begin(), src.end(), qrc.begin(), qrc.end(), std::inserter(onlySrc, onlySrc.end()));
      std::set_difference(qrc.begin(), qrc.end(), src.begin(), src.end(), std::inserter(onlyQrc, onlyQrc.end()));
      QVERIFY2(onlySrc.empty(), qPrintable("not listed in resources/fabricad.qrc (" + themeDirName(th) + "):\n  " + joinSorted(onlySrc)));
      QVERIFY2(onlyQrc.empty(), qPrintable("in the qrc but not in resources/icons (" + themeDirName(th) + "):\n  " + joinSorted(onlyQrc)));
      names[th] = src;
    }
    std::set<QString> diff;
    std::set_symmetric_difference(names[theme::Theme::Dark].begin(), names[theme::Theme::Dark].end(),
                                  names[theme::Theme::Light].begin(), names[theme::Theme::Light].end(),
                                  std::inserter(diff, diff.end()));
    QVERIFY2(diff.empty(), qPrintable("dark and light sets differ:\n  " + joinSorted(diff)));
    QVERIFY2(QFile::exists(":/icons/fabricad.svg"), "logo missing");
    QVERIFY2(QDir(":/icons").entryList({"*.svg"}, QDir::Files).size() == 1, "only the logo may sit at the :/icons root");
  }

  void svgStyleRules() {
    std::set<QString> bad;
    for (theme::Theme th : kThemes) {
      QDirIterator it(qrcDir(th), {"*.svg"}, QDir::Files);
      while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList issues = lintSvg(f.readAll(), th);
        if (!issues.isEmpty()) bad.insert(themeDirName(th) + "/" + QFileInfo(path).fileName() + ": " + issues.join("; "));
      }
    }
    QVERIFY2(bad.empty(), qPrintable("SVG style violations:\n  " + joinSorted(bad)));
  }

  /// Every colour an icon draws marks with reads against toolbar, sidebar and card surfaces
  /// (3:1), in both themes. Paper and body are fills that are always outlined, so they are exempt.
  void markColoursContrastWithSurfaces() {
    std::set<QString> bad;
    for (theme::Theme th : kThemes) {
      const std::set<QString> fillOnly = fillOnlyOf(th);
      std::set<QString> used;
      QDirIterator it(qrcDir(th), {"*.svg"}, QDir::Files);
      while (it.hasNext()) {
        QFile f(it.next());
        QVERIFY(f.open(QIODevice::ReadOnly));
        for (const QString& c : markColours(f.readAll())) used.insert(c);
      }
      for (const QString& c : used) {
        if (fillOnly.count(c)) continue;
        for (theme::Tok surf : kIconSurfaces) {
          const double r = contrast(QColor(c), theme::color(surf, th));
          if (r < kMinMarkContrast)
            bad.insert(QString("%1: %2 on %3 = %4:1").arg(themeDirName(th), c, theme::color(surf, th).name()).arg(r, 0, 'f', 2));
        }
      }
    }
    QVERIFY2(bad.empty(), qPrintable("icon colours below 3:1 on UI surfaces:\n  " + joinSorted(bad)));
  }

  /// The icons handed out by Icons.cpp follow theme::current() at paint time: the same QIcon
  /// object yields different pixels after setCurrent(), with no setIcon / reload.
  void iconFollowsThemeAtPaintTime() {
    const QIcon ic = app::resourceIcon("save");
    QVERIFY(!ic.isNull());
    theme::setCurrent(theme::Theme::Dark);
    const QImage dark = ic.pixmap(32, 32).toImage();
    theme::setCurrent(theme::Theme::Light);
    const QImage light = ic.pixmap(32, 32).toImage();
    QVERIFY2(dark != light, "same pixels in both themes");
    theme::setCurrent(theme::Theme::Dark);
    QCOMPARE(ic.pixmap(32, 32).toImage(), dark);
    QCOMPARE(app::themedIconPath(":/icons/save.svg", theme::Theme::Light), QString(":/icons/light/save.svg"));
    QCOMPARE(app::themedIconPath(":/icons/fabricad.svg", theme::Theme::Light), QString(":/icons/fabricad.svg"));
    // the logical path (what ToolInfo / tests see) still resolves
    QCOMPARE(app::resourceIconPath("save"), QString(":/icons/save.svg"));
  }

  void rendersLegiblyAtAllSizes() {
    std::set<QString> bad;
    for (theme::Theme th : kThemes) {
      const QStringList files = QDir(qrcDir(th)).entryList({"*.svg"}, QDir::Files, QDir::Name);
      for (const QString& fn : files) {
        const QString label = themeDirName(th) + "/" + fn;
        QSvgRenderer r(qrcDir(th) + "/" + fn);
        if (!r.isValid()) {
          bad.insert(label + ": QSvgRenderer cannot parse it");
          continue;
        }
        for (int s : kRenderSizes) {
          const QImage a = renderSvg(r, s);
          const Coverage c = coverage(a);
          if (c.ink < kMinInk)
            bad.insert(QString("%1 @%2px: too faint (ink %3 < %4)").arg(label).arg(s).arg(c.ink, 0, 'f', 3).arg(kMinInk));
          if (c.cover > kMaxCover)
            bad.insert(QString("%1 @%2px: too solid (cover %3 > %4)").arg(label).arg(s).arg(c.cover, 0, 'f', 3).arg(kMaxCover));
          if (renderSvg(r, s) != a) bad.insert(QString("%1 @%2px: rendering not deterministic").arg(label).arg(s));
        }
      }
    }
    QSvgRenderer logo(QStringLiteral(":/icons/fabricad.svg"));
    QVERIFY2(logo.isValid(), "logo does not parse");
    QVERIFY2(bad.empty(), qPrintable("rendering problems:\n  " + joinSorted(bad)));
  }

  void contactSheet() {
    std::vector<std::pair<QString, std::vector<SheetEntry>>> sections;
    std::map<QString, std::vector<SheetEntry>> byModule;
    for (const auto& [id, info] : tools_) {
      const QString qid = QString::fromStdString(id);
      byModule[qid.section('.', 0, 0)].push_back({qid, app::toolIconPath(info)});
    }
    for (auto& [m, e] : byModule) sections.push_back({"Tools: " + m, e});
    std::vector<SheetEntry> feats;
    for (const auto& [type, v] : features_)
      feats.push_back({QString::fromStdString(type),
                       app::featureIconPath(v.first, type.substr(0, type.find(':')), v.second)});
    sections.push_back({"Feature types (browser / timeline)", feats});
    std::vector<SheetEntry> ui;
    QStringList uiSorted = kUiIcons;
    uiSorted.sort();
    for (const QString& n : uiSorted) ui.push_back({n, app::resourceIconPath(n)});
    sections.push_back({"Browser, timeline and shell", ui});
    // Plain-text inventory next to the test binary: one "tool\t<id>\t<name>\t<tab>/<group>\t<icon>"
    // line per tool, then "feature\t<type>\t<icon>"; sorted, so runs and branches can be diffed.
    // Written before the checks below so it also lists what is missing.
    QFile inv(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("icons-inventory.txt"));
    QVERIFY(inv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    for (const auto& [id, info] : tools_)
      inv.write(QString("tool\t%1\t%2\t%3/%4\t%5\n")
                    .arg(QString::fromStdString(id), QString::fromStdString(info.name), QString::fromStdString(info.tab),
                         QString::fromStdString(info.group), app::toolIconPath(info))
                    .toUtf8());
    for (const SheetEntry& e : feats) inv.write(QString("feature\t%1\t%2\n").arg(e.label, e.path).toUtf8());
    inv.close();
    for (auto& s : sections)
      for (const SheetEntry& e : s.second) QVERIFY2(!e.path.isEmpty(), qPrintable(e.label + " has no icon"));

    // one sheet per theme: icons-dark.png / icons-light.png in $FABRICAD_ICON_SHEET_DIR (else next to the binary)
    QString dir = qEnvironmentVariable("FABRICAD_ICON_SHEET_DIR");
    if (dir.isEmpty()) dir = QCoreApplication::applicationDirPath();
    for (theme::Theme th : kThemes) {
      const QImage a = ::contactSheet(sections, th);
      const QImage b = ::contactSheet(sections, th);
      QVERIFY2(a == b, "contact sheet rendering is not deterministic");
      const QString out = QDir(dir).absoluteFilePath("icons-" + themeDirName(th) + ".png");
      QVERIFY2(a.save(out), qPrintable("cannot write " + out));
      qInfo("contact sheet: %s (%dx%d, %zu tools, %zu feature types)", qPrintable(out), a.width(), a.height(),
            tools_.size(), features_.size());
    }
  }
};

QTEST_MAIN(TestIcons)
#include "test_icons.moc"
