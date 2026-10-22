# G1-doc fix plan: document lifecycle, autosave/recovery, app shell

Status: plan only, nothing implemented. Sources: round1/doc-core.md, part-core.md (PART-CORE-5/6/7), asm-core.md (ASM-CORE-8), code at 8e20c27.
G1 owns MainWindow.cpp/.h, DocumentTab.cpp, TimelineWidget.cpp, Theme.cpp, AppActions.cpp, core Document/UndoStack/Feature/Json, plus Commands.cpp (DOC-05) and resources/theme-light.qss (DOC-10).
Headless suite to run after each step: test_main_window (autosaveAndRecovery, open/save tests at about line 445), test_app_logic (planDelete), core tests.

## Fix order (data loss first)
DOC-01, DOC-02, DOC-05, DOC-03, DOC-04, then the rest.

---

## DOC-01 (high) Autosave file keyed by document name
- **Root cause:** `MainWindow::recoveryFilePath(tab)` (MainWindow.cpp:579-582) returns `recoveryDirectory()/sanitizedFileStem(doc.name()).fcad`. Every "Untitled" tab, and every running instance (they share one AppDataLocation/recovery dir), writes the same file. `offerRecovery()` (631+) reads only `files.front()` (the newest file), and "No" deletes *all* files, including those of live instances.
- **Fix:**
  - Give each DocumentTab a `recoveryId` (QUuid, created once per tab). File name: `<stem>-<uuid8>.fcad`, which stays readable. Stop deriving the path from the name, so the "renamed document" cleanup in autosave (MainWindow.cpp:604) becomes a no-op. Keep `lastRecoveryFile`.
  - Add a per-instance session: at startup create `recovery/<sessionUuid>/` holding a `QLockFile session.lock`, and write that instance's files into it. offerRecovery scans the other session dirs, skips any whose lock `tryLock()` fails (a live instance), and treats a stale lock (QLockFile handles a dead pid) as orphaned.
  - offerRecovery lists **every** orphaned file (name, original path, time) and recovers each into its own tab. Either one Yes/No for all of them or one question per file; keep it a single askYesNo when there is only one file. "No" deletes only the orphaned files, and removes empty session dirs.
  - Migration: still accept legacy `recovery/*.fcad` (flat) files as orphaned.
- **Possible effects:**
  - test_main_window `autosaveAndRecovery` only uses `recoveryFilePath()` and `startsWith(recoveryDirectory())`, so it still passes if the session dir sits under recoveryDirectory(). Its `removeRecursively()` would also delete the live lock: recreate the session dir lazily in autosave (`mkpath`) and re-take the lock if it was lost.
  - Drawing tabs share the design's Document. autosaveNow dedups by document, but recoveryId is per tab. Key the id by Document (store it on the design tab, and look up the design tab for a drawing tab) so a design and its drawing never produce two files.
  - `clearRecoveryFile` on clean close must also remove the session dir and release the lock.
  - FABRICAD_NO_RECOVERY and non-interactive runs must behave as before.

## DOC-02 (medium) Accepted recovery deletes the autosave
- **Root cause:** offerRecovery calls `QFile::remove(newest)` right after loading (MainWindow.cpp:676). setModified(true) is called, but autosave only runs from `UndoStack::changed` -> scheduleAutosave (742). The `undoStack().clear()` at 672 runs before setModified(true), so scheduleAutosave sees an unmodified document and does not arm the timer. Nothing is written until the next undoable edit.
- **Fix:** after a successful recovery, set modified, then call `autosave(*active_)` immediately (this writes the file under the new session/id), and only then remove the orphaned file. If the new autosave fails, keep the old file.
- **Possible effects:** if a second crash happens before the user does anything, the file is offered again, which is intended. Make sure recovery does not loop on a file that fails to parse: keep the current "delete if unparsable" behaviour, but show a message.

## DOC-03 (medium) Already open file opens a duplicate tab
- **Root cause:** the dedup check (MainWindow.cpp:1177-1182) compares the stored `filePath()` with `QFileInfo(path).absoluteFilePath()`. `saveTo` (1232) stores the path exactly as the dialog or command line typed it (it can be relative, and may get ".fcad" appended), load stores the absolute path, and symlinks (/tmp, ~ links) are never resolved.
- **Fix:** add a helper `canonicalDocPath(QString)` (canonicalFilePath when the file exists, otherwise absoluteFilePath with QDir::cleanPath). Use it in saveTo before `document_->save`, in openFile before load, and on both sides of the dedup compare. Use the same canonical path in `recent_.add`, so Open Recent does not show duplicates either.
- **Possible effects:** test at line 455 (opening the same path again activates the existing tab) still holds. Window title and tooltip now show the resolved path (/tmp may resolve to a different real path), which is acceptable. Core Document::save/load stay path-agnostic: do not canonicalize in core, because core tests compare filePath().

## DOC-04 (medium) "Re-matched by geometry" warnings on unrelated features persist after undo
- **Root cause:**
  - Warnings are per-recompute (a fresh RecomputeContext for each feature in Document::recompute, Document.cpp:368), so the flag is not stale state. It comes back because the re-match **mutates the stored ref**: Feature.cpp:300-305 rewrites `ref.index`, `ref.signature` (on a non-exact match) and `ref.name` during recompute, outside the undo stack.
  - After an edit, downstream refs learn the edited geometry's fingerprint. Undo restores the upstream feature but not those learned fingerprints, so the downstream refs now mismatch the original geometry and re-match again, giving warnings on about 12 features.
  - The layer-1 history lookup (Feature.cpp:273-285) misses after an unrelated Hole change, most likely because `namedSubShape(owner, name, indexOf(self))` uses the name map of the *last* feature that touched the body, and the Hole regenerates names (or `ref.name` was never learned, which is the fallback at 305).
- **Fix:**
  1. Count only a re-match that actually changes the resolved sub-shape index (or a signature distance above a small epsilon) as a warning. When the history name resolves, it is exact, so no warning.
  2. Make the learned ref data undo-safe. Preferred: do not persist the drift into the feature's saved refs during recompute; keep it in a per-feature resolve cache that is rebuilt from the original pick on every full recompute. Alternative: snapshot the downstream refs in the edit command and restore them on undo.
  3. Investigate the Hole case with a headless test: box, sketch on its top face, extrude, hole on another face, then edit the hole diameter. Assert that downstream refs resolve exactly through layer 1, and that undo leaves no warnings.
- **Possible effects:**
  - Feature.cpp and GeometryRefs.cpp affect every part, sketch-on-face and assembly feature (G2, G3, G4). Run the whole core, part and sketch test suites. G2 must not edit these files; G4 only reads resolvePlane.
  - Removing the signature drift reintroduces the problem the comment at 302 describes (two successive small edits drift past the threshold). Keep the drift in the cache, which is rebuilt per recompute chain, so the behaviour within one session is unchanged.
  - This changes saved files, because they would no longer store drifted signatures. Old files still load because the fields are unchanged.
  - This is the riskiest item. Ship (1) on its own first if (2) gets large.

## DOC-05 (low) Dirty "*" lost after undoing a component delete
- **Root cause:** component delete uses `DocumentSnapshotCommand` (AppActions.cpp makeDeleteCommand). Its undo and redo call `doc_.fromJson(before_/after_)` (Commands.cpp:147,150), and `Document::fromJson` ends with `setModified(false)` (Document.cpp:898). Every snapshot undo/redo therefore clears the dirty flag. UndoStack cleanIndex_ is not involved, and the modified flag is not derived from it.
- **Fix:** in DocumentSnapshotCommand::undo/redo, restore and then `doc_.setModified(true)`. Better: add a `fromJson(root, err, keepModified)` overload, or move `setModified(false)` out of fromJson into `load()` (load is the only caller that needs it; check with grep, since recovery already sets true afterwards). Optionally make modified follow `undo_->isClean()` (connect cleanChanged): returning to the saved index clears '*', as in Fusion.
- **Possible effects:** this affects every snapshot command (G3 DeleteComponentCommand, PasteNew, joints). Coordinate with G3: G1 changes core, and G3 should not call setModified itself. If modified follows cleanChanged, then undoing to the save point clears '*', but autosave/recovery must still cover non-undo modifications (setName, setUnits, touchModified). Keep touchModified and only *add* "clean when index == cleanIndex". Check the test_main_window tests that assert isModified after undo.

## DOC-06 (low) Document name does not follow the file name on Save As
- **Root cause:** saveTo (1235) and openFile (1196) rename only when the name is "Untitled" or empty. An import sets the name from the STEP/STL file, so Save As keeps that name.
- **Fix:** in saveTo, rename to `completeBaseName(newPath)` when the name is "Untitled", or equals the completeBaseName of the old filePath, or equals the import source base name. Add `Document::nameIsAuto()` (a flag set by clear/import/rename-from-file, and cleared by an explicit user rename through the browser/timeline rename command), so an explicit user name is kept.
- **Possible effects:** setName also renames the root component and calls touchModified. Do it before `document_->save` (as now), so the file is saved clean. If the flag is persisted, it needs a JSON field (optional, defaulting to auto when name == file stem). Drawing tab titles follow automatically.

## DOC-07 (low) Raw parser messages for invalid .fcad; failing features load silently
- **Root cause:** the parser is QJsonDocument, not nlohmann. `Document::load` (Document.cpp:928-932) returns `perr.errorString()` ("illegal value", "unterminated object"). After load, nothing reports features with Error status or PlaceholderFeature (unknown type, FC_WARN only).
- **Fix:**
  - In load, map errors to user text: empty file gives "the file is empty", a parse error gives "the file is damaged or truncated (offset N)", and a non-object root gives "not a FabriCAD design". Keep the raw message in the log.
  - In MainWindow::openFile, after a successful load, count features with Error status (placeholders included) and show a warning such as "Opened with 2 failed features: Fillet1 (radius too large), Foo (unknown feature type 'X')". Use showMessage with Warning level, not a modal dialog, so headless runs are unaffected.
- **Possible effects:** check that no test asserts the raw Qt string (grep found none). Recovery and DocumentSnapshotCommand use fromJson, not load, so they are unaffected.

## DOC-08 (low) Failed downstream feature shows only a red icon
- **Root cause:** Document::recompute sets the Error status (Document.cpp:380). MainWindow connects only `recomputeFinished -> scheduleStateRefresh` (737), so the message appears only in the hover tooltip.
- **Fix:** in connectTabSignals, on `recomputeStarted` snapshot the set of ids in Error. On `recomputeFinished`, for features newly in Error, call `showMessage("<name> failed: <msg> (+N more)", Error)`. Do this only for committed changes: skip it while a FeatureTool preview is running (`manager_->isActive()` with a preview) and during load (DOC-07 covers that case).
- **Possible effects:** previews recompute the document repeatedly and would spam the status bar. The guard above prevents that. A message can overwrite another message (for example "Saved"), which is acceptable. No BrowserTree.cpp edits (G3). The timeline tooltip is unchanged.

## DOC-09 (low) Timeline delete of a feature with dependents has no warning
- **Root cause:** the timeline does route through `deleteRefs` (MainWindow.cpp:768-770), but deleteRefs asks only when `plan.bodyNotes` is non-empty (1291-1295). `DeletePlan::summary` already counts dependents ("N dependent features will fail", AppActions.cpp:66-72), but nothing asks the user.
- **Fix:** add `int dependents` (or `std::vector<std::string> dependentNames`) to DeletePlan in planDelete, and in deleteRefs ask when `!bodyNotes.empty() || dependents > 0`: "Delete Extrude1? 3 dependent features will fail: Fillet1, Chamfer1, Hole1".
- **Possible effects:** this also adds the prompt to browser and Delete-key deletes, which is consistent with Fusion. Non-interactive askYesNo returns the default (true), so headless tests are unaffected. test_app_logic planDelete still passes; add a check for the new field. Sketch-mode deletes go through SketchSession first, so they are unaffected.

## DOC-10 (low) Timeline context menu and tooltips black
- **Root cause:** not the theme. The QSS already styles QMenu and QToolTip (theme-light.qss:4,38). `scroll_->setStyleSheet("background: transparent;")` (TimelineWidget.cpp:259) has no selector, so it cascades to every descendant, including `QMenu menu(this)` in TimelineStrip::contextMenuEvent (201) and the tooltips shown with `QToolTip::showText(..., this)` (113, 115, 164). A transparent top-level popup renders black without a compositor.
- **Fix:** scope the rule, for example `scroll_->setObjectName("TimelineScroll")` plus a stylesheet like `#TimelineScroll, #TimelineScroll > QWidget { background: transparent; }`. Alternatively set `viewport()->setAutoFillBackground(false)` and move the rule to theme-light.qss (#Timeline section). Also call QToolTip::hideText() on undo/timeline rebuild (part-core: tooltip stayed after Ctrl+Z).
- **Possible effects:** the timeline background must stay #F7F7F8 (check with a screenshot). Apart from the hideText call, this only affects the strip's popups and tooltips.

## DOC-11 (low) Same-named tabs and Open Recent entries indistinguishable
- **Root cause:** tab tooltips exist (DocumentTab::tooltip, set at 426/519), but refreshTabBar runs only on modifiedChanged/nameChanged. After a load into a pristine tab where neither signal fires (name equal, modified already false), or after a Save As to the same name, the tooltip stays "Not saved yet". Open Recent shows `QFileInfo(p).fileName()` only (MainWindow.cpp:297).
- **Fix:** call refreshTabBar() in afterDocumentReplaced and in saveTo. When two tabs have the same title, append " — <parent folder>" to their tab text. In Open Recent, when basenames collide, label each entry "name.fcad — <parent dir>" (and keep the full path in the tooltip; QMenu shows action tooltips only with `setToolTipsVisible(true)`).
- **Possible effects:** test line 474 compares tabText with "bracket", so append the folder only on a collision. findTab by title strips " *" (407-410), so also strip the suffix or match on the document name (it already falls back to the name).

## DOC-12 (low) App menu stays open and swallows keys after hovering Open Recent
- **Root cause (to confirm):** the File menu is a standard `QToolButton::InstantPopup` (MainWindow.cpp:220-223). Under Hyprland/XWayland, a click on the native OCCT viewer window does not reach Qt's popup auto-close, so the popup (and its submenu) stays the active popup and receives all keys. `eventFilter` (1541) ignores keys whose `window() != this`, which is correct while a popup is open, but Escape and Ctrl+O never act because the popup has no keyboard grab.
- **Fix:** in MainWindow::eventFilter (already installed on qApp, line 129):
  - On MouseButtonPress, if `QApplication::activePopupWidget()` exists and the press lands outside every open QMenu, close all popups (loop `while (auto* p = activePopupWidget()) p->close();`).
  - On KeyPress while a QMenu popup is active: Escape closes it. A key that is a global shortcut (Ctrl+O/N/S...) closes the popups and then goes through handleKeyPress.
  - Reproduce under Hyprland first to confirm. Also try `fileMenu->setAttribute(Qt::WA_NoMouseReplay)` and check whether the viewer has WA_NativeWindow.
- **Possible effects:** this touches global event filtering, so do not close combo-box popups, CommandSearch or the MarkingMenu (check `qobject_cast<QMenu*>` only, and skip MarkingMenu/CommandSearch). Keyboard routing in handleKeyPress belongs to G4 (ShortcutRouter): only add the popup pre-check in the filter and do not change routeKey. Run the test_main_window keyboard tests.

## Cross-group notes
- core Feature.cpp/GeometryRefs.cpp (DOC-04) and Document.cpp/Commands.cpp (DOC-05, DOC-07) are shared by every group. Make these changes in small separate commits and run the full ctest after each one (-j2).
- DOC-05 and G3: G3's component delete/paste should rely on the snapshot command keeping the modified flag, and not call setModified.
- DOC-08 and DOC-09 stay in MainWindow/AppActions/TimelineWidget. No BrowserTree.cpp edits.
- DOC-12 and G4: no edits to ShortcutRouter.cpp, and MainWindow::handleKeyPress is unchanged.
