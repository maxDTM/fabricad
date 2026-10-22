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

---

## Done (branch fn/G1-doc)

Screenshots: `docs/tests/functional/fixes/G1-doc/` in the main checkout (GUI runs used the worktree binary on display :41 with a private XDG_DATA_HOME / XDG_CONFIG_HOME, so the real recovery folder and settings were not touched). Repro scripts are in `fixes/G1-doc/scripts/`. Full ctest (QT_QPA_PLATFORM=offscreen, -j2): 56/56 pass.

- **DOC-01** (MainWindow.cpp/.h, README.md): recovery files are now `recovery/<sessionUuid>/<stem>-<docId8>.fcad`. The id is kept per Document (a Qt dynamic property), so a design and its drawing share one file. Each instance locks its folder with a `QLockFile session.lock` (stale only when the pid is dead). The folder is created lazily on the first autosave and recreated, with the lock taken again, if it was deleted. `orphanedRecoveryFiles()` skips the own folder and folders whose lock is held, and includes old flat `recovery/*.fcad` files. `offerRecovery()` lists every orphan in one Yes/No and recovers each into its own tab. "No" deletes only the orphans. A clean close removes the files, releases the lock and removes the empty folder. New public helpers: `sessionRecoveryDirectory()`, `orphanedRecoveryFiles()`, `recoverFile()`. Test: `recoveryPerDocumentAndSession` covers two Untitled docs, a live lock, a stale lock, a legacy file and a damaged file. GUI check: a second instance started while the first had an autosave showed no prompt (doc01-second-instance-no-prompt). After `kill -9` of the first and a restart, the prompt listed the file (doc01-recovery-prompt).
- **DOC-02**: `recoverFile()` marks the document modified and autosaves it into the current session right away. The orphan is deleted only when that write succeeds. A file that cannot be read is deleted, and the error is shown in the status bar. GUI check: after accepting, the old session folder was gone and the new one held `plate-xxxx.fcad` (doc02-recovered).
- **DOC-05** (Document.cpp, Commands.cpp): `Document::fromJson` no longer clears the modified flag; `load()` does. `DocumentSnapshotCommand::redo/undo` set modified=true. I did not do the optional "clear '*' when undo returns to the saved point". Test: in `documentLifecycleFixes`.
- **DOC-03** (MainWindow.cpp): a `canonicalPath()` helper resolves symlinks and `..`. For a file that does not exist yet, it resolves the folder and appends the name. It is used in `saveTo`, `openFile` (duplicate check and load path) and the recent list. Core `Document::save/load` are unchanged. The existing round-trip test now compares the recent entry with `canonicalFilePath()`. Test: a symlinked folder and a `link/../link/` path both find the open tab.
- **DOC-04** (Feature.cpp): only step 1 shipped. "Re-matched by geometry" is reported only when the geometric fallback lands on a *different* sub-shape index than the reference had. A face that just moved is no longer flagged. **Deviation:** step 2 (a fingerprint cache, or restoring references on undo) was not done. I could not reproduce the tester's persistent warnings headless or by script: a box with 5 cut-holes on the top face, then editing a hole sketch dimension, the base sketch, a cut depth or the base height, each followed by undo, showed no warnings on either the old or new binary (scripts `edit*.fcs`). New part test `holeEditKeepsDownstreamRefsQuiet` (test_dressup) shows history names survive Hole edits: the Hole carries `Extrude1/End`, and the fillet stays Ok over diameter and height edits. Step 2 is still open if a GUI repro turns up. The likely remaining case is a fallback where the index also changes, which step 1 still warns about (once per change, and it persists until the next recompute).
- **DOC-06** (MainWindow::saveTo): the name follows the new file when it is empty, "Untitled" or equal to the old file's stem. **Deviation:** no Document flag. "Name equals the old file stem" stands for "automatic", which keeps core and the file format unchanged. A user-chosen name is kept. The rename still happens before `save`, so the file is saved clean. Test included.
- **DOC-07** (Document::load, MainWindow::openFile): load errors read "the file is empty", "the file is damaged or truncated (at byte N of M)" or "not a FabriCAD design". After a load, a status-bar warning names the features that failed to compute (unknown types included). Tests included; GUI: doc07-damaged-file.
- **DOC-08** (MainWindow): `reportNewFailures()` runs after each recompute and after a tool finishes. It is skipped while a non-idle tool (preview) is active, and only a baseline is taken after load. It shows "<Feature> failed: <message>" for the first newly failed feature. The per-document baseline is dropped when the document closes. No BrowserTree edits. Test `failedFeatureShowsMessage`; GUI: doc08-failed-message.
- **DOC-09** (AppActions.cpp/.h, MainWindow::deleteRefs): `DeletePlan::dependents` lists later features that are not being deleted, in timeline order. Delete asks whenever bodies or dependents are involved, from the timeline, browser or Delete key. The wording is "may fail", because dependentsOf is conservative: body-touching features count. Test `deletePlanListsDependents`; GUI: doc09-delete-prompt.
- **DOC-10** (TimelineWidget.cpp): the transparent background rule is scoped to `#TimelineScroll, #TimelineViewport, #TimelineStrip`, so the strip's QMenu and tooltips get the theme again. The strip tooltip is hidden on undo/redo. theme-light.qss needed no change. GUI: doc10-timeline-menu and doc10-timeline-tooltip are readable, and the timeline background is still light. doc10-tooltip-hidden-after-undo.
- **DOC-11** (MainWindow::refreshTabBar, Open Recent): same-named tabs with a file get " (folder)", but only on a collision. Tooltips refresh after open and save. Open Recent adds the folder to names that occur twice and shows the full-path tooltips (`setToolTipsVisible`). The tab part is tested; the recent menu labels were not checked in the GUI.
- **DOC-12** (MainWindow::eventFilter): reproduced on the old binary. With File > Open Recent hovered, Ctrl+O did nothing, because the menu had the keyboard and its entries only show the shortcut as text. A click on the viewport did close it on this harness. Fix: a Ctrl/Alt key typed into one of this window's menus closes the menus and goes through `handleKeyPress` as a canvas key. If a click or key reaches the main window while a QMenu is still open (no grab), the menus close, and Escape is consumed. Plain Escape inside a menu keeps Qt's one-level behaviour. No ShortcutRouter or handleKeyPress changes. GUI: doc12-ctrl-o-from-menu (the Open dialog appears).

Harness note: during these runs the host compositor resized the rootful Xwayland window (945x990, then 945x490 or 466x490), so some dialogs had to be moved or answered from the keyboard.
