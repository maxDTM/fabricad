// FabriCAD app: the layout and appearance settings of the redesign (docs/plans/ui-redesign.md
// section 6.1). One QObject for every `ui/*` key: values are validated against the allowed set,
// stored in QSettings and announced with changed(key), so the shell applies them live.
//
//   ui/theme          dark | light | system          (applied by ThemeManager)
//   ui/toolbarArea    top | left | right             (applied live; vertical MainToolbar)
//   ui/sidebarSide    left | right                   (stored; applied in phase 2)
//   ui/cardPlacement  floatRight | floatLeft | sidebar | free   (stored; phase 3)
//   ui/cardRememberDrag  bool                        (stored; phase 3)
//   ui/cardFreePos    string (anchor + offset)       (written by the card, phase 3)
//   ui/quickToolbar   off | left | right             (applied live; QuickToolbar in the viewport)
//   ui/orientation    viewCube | hexDial             (default viewCube since 2026-10-23)
//   ui/timelineStyle  featureList | strip            (stored; phase 2)
//   ui/fontScale      90 .. 130 (%)                  (applied live)
//   ui/glow, ui/scanlines  bool                      (decision 10; stored)
//   ui/designFilter   part | assembly                (toolbar filter of Design, decision 2)
//   ui/commandLine    bool                           (command box visible)
//   sketch/typedDimensions  bool                     (typed sketch values create dimensions; read by the sketch tools)
//
// The keys are read once at start-up (main() applies the theme before the window shows) and
// afterwards through this object. Tests isolate QSettings (QStandardPaths test mode).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>

#include <vector>

namespace fabricad::app {

class UiSettings : public QObject {
  Q_OBJECT
 public:
  struct Key {
    QString key;          ///< "ui/toolbarArea"
    QVariant defaultValue;
    QStringList choices;  ///< allowed strings; empty for bool / int keys
    int min = 0, max = 0; ///< int keys (min < max)
    bool appliedInPhase1 = false;  ///< false: stored now, the layout follows in a later phase
  };

  static UiSettings& instance();
  static const std::vector<Key>& keys();
  static const Key* find(const QString& key);

  /// Validated value (the default when unset or invalid).
  QVariant value(const QString& key) const;
  QString string(const QString& key) const { return value(key).toString(); }
  bool flag(const QString& key) const { return value(key).toBool(); }
  int number(const QString& key) const { return value(key).toInt(); }
  /// Store a value; false when the key is unknown or the value not allowed. Emits changed(key)
  /// when the stored value changes. ui/theme and ui/fontScale are applied right away.
  bool set(const QString& key, const QVariant& value);
  /// Back to the default value.
  void reset(const QString& key);
  /// Re-announce a key after something else wrote it (ThemeManager's THEME command).
  void notify(const QString& key) { emit changed(key); }

 signals:
  void changed(const QString& key);

 private:
  UiSettings() = default;
  static bool valid(const Key& k, const QVariant& v, QVariant* normalized);
};

}  // namespace fabricad::app
