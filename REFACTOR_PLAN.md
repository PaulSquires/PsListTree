# CListBox Refactor Plan

Reusable, multiple-instance owner-drawn listbox control for the Tiko project.

Scope decisions (confirmed 2026-07-16):
- **Fonts**: caller supplies `HFONT` handles; the control stores no references to host-app globals.
- **Selection**: distinct selected / hover / focus visual states, **plus** working multi-selection.
- **Headers**: design for collapsible header/group rows now. **Flat groups — one level of nesting
  only** (header rows + their direct child items; no nested headers). Header rows **are
  selectable** and participate in keyboard navigation like any other row.
- **Selection storage**: tracked **in the model** (per-row selected flag), not delegated to the
  listbox's own selection bits.
- **Keyboard**: full keyboard navigation built into the control.

Because collapsible headers change how many rows are visible, and `LBS_NODATA` requires the
listbox item count to equal the number of *visible* rows, this refactor requires a **model/view
split**, not just bug fixes. Phases are ordered so each is independently testable.

---

## Phase 0 — Decouple from host-app globals

Goal: the control + double buffer compile and run without any tiko-specific globals.

- `clsDoubleBuffer` currently references `ghFont()`, `HWND_FRMMAIN` (clsDoubleBuffer.inc:89),
  and the `GUIFONT_10` enum (clsDoubleBuffer.bi:32). Remove all three.
- Change `clsDoubleBuffer.SetFont` to take an `HFONT` directly instead of an index into a global
  array. The caller (control or paint callback) owns the fonts.
- Add font storage to the control: `CListBox_SetFont( hCtl, HFONT )` (and/or let the paint
  callback select a font per row). Control never creates or owns fonts it wasn't given.
- Add a destructor to `clsDoubleBuffer` (RAII) so an early return between Begin/End can't leak
  the memDC/bitmap. Keep `EndDoubleBuffer` for explicit use.
- Verify `clsDoubleBuffer.bi/.inc` includes only `windows.bi` + AfxNova — no app headers.

Test: control compiles in an empty host that defines none of tiko's globals.

---

## Phase 1 — Multi-instance correctness & API bug fixes

Goal: two instances on screen behave independently; existing API is correct. No new features.

- **Move shared statics into the instance.** `static m` (CListBox.inc:422) and `static nLastIdx`
  (CListBox.inc:426) in the subclass proc are shared across all instances — move both into the
  per-instance `CLISTBOX`.
- **Fix selection targeting.** `CListBox_GetCurSel`/`SetCurSel` (CListBox.inc:163-173) send
  `LB_GETCURSEL`/`LB_SETCURSEL` to the form handle, not the child listbox. Route through
  `GetDlgItem(hWin, idc_ListBox)` like `GetCount` does. Verify against the demo's `SetCurSel(0)`.
- **Null-check callbacks** before calling `PaintCallback` (CListBox.inc:357) and
  `MessageCallback` (subclass) so an instance without callbacks set doesn't fault.
- **Fix `SetMessageCallback` return** (CListBox.inc:104-108) — add the success return.
- **Make `SetRowHeight` effective post-creation** — issue `LB_SETITEMHEIGHT` (or force a
  re-measure) rather than only updating the field (CListBox.inc:189-194).
- **Normalize boolean returns** — `SetText`/`SetItemData` currently return `true` for *error*;
  pick one convention (recommend `true` = success) and apply it across the API.
- **Clarify the handle.** The public `hListControl` param is actually the parent form handle.
  Rename/document, or introduce an opaque handle type, so callers aren't misled.
- **Fix divide-by-zero in `GetListBoxEmptyClientArea`** (CListBox.inc:32). `ItemsPerPage` is
  computed as `rc.bottom \ itemHeight` *before* the `NumItems > 0` guard; on an empty list
  `LB_GETITEMRECT(0)` fails, `itemHeight = 0`, and the integer divide crashes. Currently masked
  only because `Refresh()` hides the listbox when empty (CListBox.inc:60) — fragile. That line-32
  assignment is also dead code (overwritten at line 37): drop the initializer (`dim as long
  ItemsPerPage`), which removes the crash *and* makes the empty-list path correctly return the
  full client rect (whole background painted), so the hide-when-empty hack is no longer required.

Test: side-by-side two-instance harness; hover/selection in one must not affect the other; an
empty listbox that is visible erases cleanly (no crash) instead of relying on being hidden.

---

## Phase 2 — Data model rework (enables headers + fixes count duplication)

Goal: one source of truth; support insert/delete/clear; support collapsible groups.

- **Model vs. view split.** Keep `rows()` as the full model (all rows, including collapsed
  children). Add a `visibleIndex()` map (visible position -> model row) rebuilt on
  expand/collapse. Set `LB_SETCOUNT` to the *visible* count. `WM_DRAWITEM.itemID` indexes the
  visible map, not the model directly.
- **Flat grouping (one level).** A row is either a header or an item; items belong to the nearest
  preceding header (or to an implicit top-level group if none). No nested headers. Collapsing a
  header hides its direct child items only. This keeps the model/view map a simple linear scan.
  - **Uniform row height required.** The empty-area calc in `GetListBoxEmptyClientArea` (and the
    whole `LBS_OWNERDRAWFIXED` approach) assumes every row — headers included — is the same fixed
    height. Keep header rows the same height as items. Taller headers would force
    `LBS_OWNERDRAWVARIABLE` and a rewrite of the empty-area math to sum actual row heights.
- **Repaint the vacated region on shrink.** When rows are deleted or a header is collapsed, the
  visible count drops and the now-empty lower region must be erased. Ensure the count-change /
  `Refresh` path forces a redraw **with** background erase (so `WM_ERASEBKGND` →
  `GetListBoxEmptyClientArea` runs) over the vacated area, or stale rows linger.
- **Single source of truth for count.** Derive the listbox count from the model/view; never let
  the `rows()` ubound and the listbox count drift (today `AddRow` maintains both, CListBox.inc:69-76).
- **Add operations:** `Clear/Reset`, `InsertRow`, `DeleteRow`, plus header/group rows using the
  existing `IsHeader`/`bCollapsed` fields (CListBox.bi:26-27) with `Expand`/`Collapse`/`Toggle`.
- **Bulk-load performance.** Replace one-at-a-time `redim preserve` (CListBox.inc:70-71, O(n^2))
  with chunked growth, and add `BeginUpdate`/`EndUpdate` to defer `LB_SETCOUNT`/repaint during
  bulk inserts.

Resolved design decisions:
- Header rows **are selectable** and behave like items for keyboard nav and selection.
- **Flat groups, one level of nesting** — no nested headers.

Test: collapse/expand toggles visible count correctly; bulk-add of several thousand rows is fast;
selecting a header row works the same as selecting an item.

---

## Phase 3 — Selection states, multi-select, keyboard nav

Goal: the visual + input model tiko needs.

- **Distinct paint states.** Replace the single `isHot` (CListBox.inc:339-342) in
  `CLISTBOX_PAINTINFO` with separate `isSelected`, `isHot` (hover), `isFocused` so the paint
  callback can style each independently.
- **Multi-select, model-based selection.** Selection is stored in the model as a per-row
  `selected` flag on `CLISTBOX_ROWINFO`, **not** in the listbox's own selection bits — this
  survives collapse/expand index shifts and lets header rows be selected. The underlying listbox
  is driven from the model for painting; do not rely on `LB_GETSEL`/`LB_SETSEL` as the source of
  truth. Add `CListBox_SetMultiSelect`/`SetExtendedSelect` that take effect at creation (today
  `ExtendSel`/`MultipleSel` are read from defaults that can't be changed, CListBox.inc:584-585),
  plus `GetSelCount`/`GetSelItems`/`GetSel`/`SetSel` operating on the model flags.
  - Collapsing a header does not clear its children's selection flags; selection is retained in
    the model even while hidden. **`GetSelItems` reports ALL selected model rows, including
    hidden-but-selected rows under a collapsed header** (not just visible ones).
- **Header vs. item must be distinguishable to callers.** Since header rows are selectable and
  `GetSelItems` returns them alongside normal items, the caller needs to tell them apart. Add a
  query API — e.g. `CListBox_IsHeader( hCtl, row ) as boolean` — and/or return the row-type flag
  as part of the `GetSelItems` result, so the caller can act differently on a selected header vs.
  a selected item. Back it with the existing `CLISTBOX_ROWINFO.IsHeader` field.
- **Full keyboard nav** in the subclass `WM_KEYDOWN`: Up/Down, PageUp/PageDown, Home/End with
  ensure-visible; Left/Right to collapse/expand header rows; Space / Ctrl+click / Shift+click for
  multi-select; optional type-to-search.
- **Hot-tracking: single source of truth.** The current design decides "hot" twice — `nLastIdx`
  drives which rows to invalidate on `WM_MOUSEMOVE` (CListBox.inc:466-495), while the hot *look*
  is re-derived at paint time via `isMouseOverRECT` (a `GetCursorPos`+`MapWindowPoints`+`PtInRect`
  per row, per paint) in `WM_DRAWITEM` (CListBox.inc:340). Unify these:
  - Store the hovered index on the instance as `pList->nHotIdx` (fixing the shared-`static`
    multi-instance bug from Phase 1 at the same time), set it where `nLastIdx` is set today.
  - In `CListBox_OnDrawItem`, compute `isHot = (lpdis->itemID = pList->nHotIdx)` and drop the
    `isMouseOverRECT` call. Removes the per-row cursor hit-test and a paint-time race where the
    cursor can lag the invalidation. `WM_MOUSELEAVE` then just sets `nHotIdx = -1` and repaints.
  - Guard the `-1` case before `ListBox_GetItemRect(hWin, nLastIdx, ...)` (right after a leave,
    `nLastIdx = -1` fails the call and leaves a stale rect that gets invalidated twice).
  - Pass `FALSE` (no erase) to `InvalidateRect` for the row rects — the double-buffer fully
    repaints the row and `WM_ERASEBKGND` is swallowed, so an erase cycle is wasted work.
  - **Verify (runtime):** with fewer rows than fill the client (`LBS_NOINTEGRALHEIGHT` leaves
    blank space below the last row), confirm hovering that blank area reports HIWORD=1 and does
    not leave the last row stuck hot. Add an explicit `GetCount()` bounds check if it does.
- **Per-row tooltips via `TTN_GETDISPINFO` (replace the push model).** Today the tooltip tool is
  registered over the whole listbox and the host pushes text on `WM_MOUSEHOVER` via
  `CListBox_SetTooltip` (whose child-vs-form handle bug was fixed in Phase 1). This push model has
  a structural flaw: a bubble that is already visible does not refresh or reposition as the mouse
  moves between rows. Move to an on-demand callback model:
  - **Register with `LPSTR_TEXTCALLBACK`.** In `AfxAddTooltip`/tool setup, set the tool text to
    `LPSTR_TEXTCALLBACK` so the tooltip queries text on demand instead of holding a fixed string.
    Keep the single whole-listbox tool (`TTF_IDISHWND`); do NOT register per-item tool rects —
    that scales badly under `LBS_NODATA` with thousands of rows.
  - **Answer `TTN_GETDISPINFOW`.** The tooltip sends its notifications to the tool's owner window
    = the CListBox form (`tti.hwnd = GetParent(hList)`), so handle `WM_NOTIFY` /
    `TTN_GETDISPINFOW` in `CListBox_WndProc` (it currently handles SIZE/PAINT/MEASUREITEM/
    DRAWITEM/NCDESTROY/ERASEBKGND — add NOTIFY). Fill `lpszText` with the text for the current
    hot row, `pList->nHotIdx` (reusing the unified hot index above). Point `lpszText` at a buffer
    with instance lifetime (a `DWSTRING` field on `CLISTBOX`), not a local.
  - **Refresh on row change.** In the `WM_MOUSEMOVE` hot-tracking block, when `nHotIdx` changes,
    `TTM_POP` the current tip (and/or `TTM_UPDATE`) so the next show re-queries `TTN_GETDISPINFOW`
    for the new row — this is what fixes the stale/misplaced bubble.
  - **Text source — decide.** Recommended: a host-supplied `TooltipCallback( pList, row )` that
    returns a `DWSTRING` (consistent with the Paint/Message callbacks; lets an explorer show a
    full path while the row shows a truncated name). Default to the model row's `Text` when no
    callback is set. Keep `CListBox_SetTooltip` only as an optional manual override, or retire it.
  - **Multi-instance:** each instance owns its own tooltip control and its own form WndProc, so
    `TTN_GETDISPINFOW` resolves to the right `CLISTBOX` via the notified form — no shared state.

Test: keyboard-only navigation reaches every row; multi-select count matches; selection survives
collapse/expand; hovering blank space below the last row clears the hot state; tooltips appear on
hover and update to the correct text as the mouse moves between rows (including across two
instances without cross-talk).

---

## Phase 4 — Rendering performance & polish

- **Stop per-row bitmap churn.** `clsDoubleBuffer` allocates+frees a compatible DC and bitmap on
  every `WM_DRAWITEM` (clsDoubleBuffer.inc:35-41, 62-69) — i.e., per row per repaint. Cache a
  per-control memDC/bitmap sized to one row (or the client) and reuse.
- **Mouse wheel:** respect `SPI_GETWHEELSCROLLLINES` instead of the hardcoded 3 lines
  (CListBox.inc:452), and preserve the delta remainder rather than resetting to 0.
- **Fix `ClassStyle` timing/comment.** It's set after `Create` with a comment that misdescribes
  `CS_DBLCLKS` (CListBox.inc:572-573). Set it correctly at creation and fix the comment.
- **Harden `GetListBoxEmptyClientArea` height source.** Derive row height from `LB_GETITEMHEIGHT`
  (or the stored `pList->RowHeight`, DPI-scaled) instead of `LB_GETITEMRECT(0)` (CListBox.inc:29-30),
  which fails when item 0 doesn't exist. Use integer `\` at line 37 instead of `/` (float→long
  rounding) to state intent, and update the stale "mod of lineheight / partial line" comments
  (CListBox.inc:24-26, 537-539) — `LBS_NOINTEGRALHEIGHT` already draws the partial bottom row via
  owner-draw, so there is no partial-line gap to special-case.

---

## Phase 5 — API surface & documentation

Done:
- **Documented public header.** `CListBox.bi`'s API section is reorganised into labelled groups
  (creation / rows / counts / contents / groups / selection / appearance / scrollbar / callbacks)
  with the contracts stated up front: the handle convention, that all row indices are *model*
  indices, the one-level group rule, and font/lifetime ownership.
- **Callback contracts** documented on the type declarations themselves — including the trap that
  `MessageCallbackFunc`'s result is ignored for `WM_LBUTTONUP` (swallowing it strands the
  listbox's mouse capture).
- **Naming consistency.** `SetMessageCallback` was the odd one out (a `function as boolean` while
  the other three callback setters were `sub`s) — now a `sub`. `AddString`/`InsertString`/
  `DeleteString` were kept deliberately: they mirror `LB_ADDSTRING`/`LB_INSERTSTRING`/
  `LB_DELETESTRING` and read naturally in Win32 code.
- **`README.md`** — quick start, concepts, input reference, callback table.
- **Test harness.** `main.bas`/`frmMain.inc` runs two instances covering *both* selection modes
  (A extended, B multi-toggle), groups with collapse, and prints a checklist of the interactive
  behaviours that can only be verified by hand.

Decisions:
- **No opaque handle type.** The plan said "consider"; rejected. The control handle must remain a
  real `HWND` because callers legitimately treat the control as a window (the demo positions it
  with `SetWindowPos`). An opaque wrapper would buy slight type-safety and break that.
- **Folding into tiko is done manually** by the author and is deliberately not part of this phase.

---

## Phase 6 — Owner-drawn vertical scrollbar

Goal: a thin owner-drawn vertical scrollbar living immediately to the right of the listbox,
replacing the `frmVScrollBar.inc` sketch (which is a single-instance extract from tiko and does
not compile here — `ghPanel`, `CPanelWindow`, `SCROLLBAR_WIDTH_PANEL`, `HWND_FRMPANEL` and
`pWindowPanel` are all undefined in this project).

Resolved design decisions:
- **Self-contained control, owned by CListBox.** It gets its own window class, its own
  per-instance state in `UserData(0)`, and its own paint callback — i.e. structurally a
  standalone control — but `CListBox` creates, positions and drives it, so the thumb can never
  go stale and the host writes zero glue. Exposing a public `CVScrollBar_Create` later is then a
  one-line change.
- **Thumb only** — no arrow buttons.
- **Hover highlight** on the thumb.
- **Auto-repeat** paging while the mouse is held on the track.
- **Auto-hide** when the content fits.
- **Paint callback** like CListBox, with a working default so it renders out of the box.

### Structure

- New `CVScrollBar.bi` / `CVScrollBar.inc` following the CListBox conventions; delete
  `frmVScrollBar.inc`.
- Per-instance `CVSCROLLBAR` on the heap in `pWindow->UserData(0)` (fetched via a
  `GetVScrollBarPointer` helper). **Every** global/static in the sketch becomes an instance field:
  `gPanelVScroll` (frmVScrollBar.inc:10), `HWND_FRMPANEL`/`HWND_VSCROLLBAR`/`bDragActive`
  (13-15), and `static prev_pt` (65) — the last is the same shared-static bug class fixed in
  Phase 1.
- Fields: range (`nTotal`, `nPage`, `nPos`), geometry (`rcThumb`, `thumbHeight`), interaction
  (`isDragging`, `dragOffset`, `isHot`, `hotTimerOn`, `repeatTimerOn`, `repeatDir`), colors, and
  `PaintCallback`.

### Generic range model (what keeps it reusable)

The control knows nothing about listboxes: `CVScrollBar_SetRange( total, page, pos )` plus
`GetPos/SetPos`. CListBox is simply the thing that calls it.

### Corrected geometry (the sketch's math, fixed)

```
' guards FIRST -- no division until the divisors are known safe
if (nTotal <= 0) orelse (nPage <= 0) orelse (track <= 0) then no thumb
if (nTotal <= nPage) then no thumb                     ' everything fits
thumbH = max( (nPage / nTotal) * track, MIN_THUMB )    ' stays grabbable on huge lists
span   = track - thumbH
maxPos = nTotal - nPage
thumbY = iif( maxPos > 0, (nPos / maxPos) * span, 0 )
```

This fixes, specifically:
- **Divide-by-zero on an empty list** — `thumbHeight = (itemsPerPage / numItems) * listBoxHeight`
  (frmVScrollBar.inc:39) divides by `numItems` *before* the `numItems < itemsPerPage` guard at
  line 45. Same bug class as the Phase 1 `GetListBoxEmptyClientArea` crash.
- **Scaling off the wrong height** — line 42 positions the thumb using `listBoxHeight`, but the
  thumb lives in the *scrollbar's* client rect. Works only while the two heights coincide. Both
  size and position must scale off the **track** height.
- **No minimum thumb height** — a proportional thumb collapses to 1-2 px on large lists.
- **Inverted return value** — `calcVThumbRect` documents "Returns True if RECT is not empty" but
  returns TRUE when it *empties* the rect (45-48) and 0 otherwise. Return TRUE = a thumb is
  needed.
- **Unclamped `nTopIndex`** (118-120) and an unguarded `(rc.bottom / rc.bottom)` divide.

### Drag without drift

The sketch moves the thumb by pixel delta and derives the index from it (110-122), but
`LB_SETTOPINDEX` snaps to whole rows, so the painted thumb and the real position drift apart and
then jump on the next recalc. Instead: record the grab offset within the thumb on mouse-down,
derive `pos` from the cursor, then **recompute the thumb from `pos`** — the two can never
disagree. (Tradeoff: the thumb snaps to row granularity mid-drag; that is standard behavior.)

### Auto-repeat paging

- Mouse-down on the track (not the thumb): page once in that direction, `SetCapture`, then start
  a repeat timer — initial delay ~400 ms, then repeat every ~60-100 ms while held.
- **Stop repeating once the thumb reaches the cursor** (standard Windows behavior), and on
  `WM_LBUTTONUP` / `WM_CAPTURECHANGED`.
- Needs a repeat timer id distinct from the hot-track poll timer id.

### Auto-hide

- Hide (`SW_HIDE`) when `nTotal <= nPage`; show when it overflows. `CListBox_PositionWindows`
  gives the listbox the **full** client width while hidden, and reserves the strip while shown.
- The visibility decision therefore lives in the sync path and must trigger a re-layout when it
  flips.
- **No oscillation risk:** hiding only changes the listbox *width*, while `itemsPerPage` depends
  on *height* — so the overflow decision can't feed back on itself.
- Row width changes on reflow, which the Phase 4 buffer cache keys on (`EnsureCache(w,h)`), so it
  rebuilds automatically — nothing extra to do.
- Clear the scrollbar's hot state when hiding, or a stale highlight reappears on re-show.

### Hover + reliable leave

Thumb highlights on hover. Reuse the **`TrackMouseEvent` + ~100 ms poll-timer safety net** built
for CListBox — `TME_LEAVE` will be exactly as unreliable here, so the fix carries over for free.

### Robustness the sketch lacks

- `WM_CAPTURECHANGED` — without it, a stolen capture strands `bDragActive = true` and the thumb
  sticks to the mouse forever.
- Additive `ClassStyle` (the sketch repeats the overwrite bug fixed in Phase 4), and no
  `CS_DBLCLKS` — double-click is meaningless for a scrollbar.
- The creation fragment (158-161) allocates `pWindow` but calls **`pWindowPanel->Create`** (an
  undeclared typo), uses tiko's `CPanelWindow`, and isn't inside a function.

### CListBox integration

- `CListBox_Create` creates the scrollbar child inside the CListBox form.
- `CListBox_PositionWindows` reserves a DPI-scaled strip (`ScaleX`) on the right edge when shown.
- `CListBox_SyncScrollBar()` pushes `(total = visibleCount, page = ItemsPerPage, pos = topIndex)`,
  applies the auto-hide/relayout decision, and invalidates. Call it from `Refresh`, the wheel
  handler, `EnsureVisible`, `MoveFocusVis`, and `WM_SIZE` — every path that can scroll, since the
  listbox has no `WS_VSCROLL` of its own. This is what kills the sketch's worst bug: `WM_PAINT`
  (136-147) paints `gPanelVScroll.rc` but never recalculates it, so wheel/keyboard/EnsureVisible
  scrolling left the thumb frozen and lying about the position.

### Painting

`CListBox_SetScrollBarPaintCallback( hCtl, @sub )` receiving
`{ b as clsDoubleBuffer ptr, rcClient, rcThumb, isHot, isDragging }`. Default paint uses settable
colors so it works immediately; the callback overrides entirely. The demo's `theme` already has
`BackColorScrollBar` / `ForeColorScrollBar`.

Test: two instances scroll independently; thumb tracks wheel/keyboard/EnsureVisible scrolling;
drag stays glued to the cursor with no drift; holding the track auto-repeats and stops when the
thumb arrives; the bar hides when a group collapses enough to fit and reappears on expand; hover
highlight clears reliably when the mouse leaves quickly.

---

## Suggested sequencing

Phases 0 and 1 are safe, high-value, and unblock everything else — do them first and independently.
Phase 2 is the architectural core (model/view) and should land before 3, since selection and
keyboard nav both depend on the visible-index mapping. Phase 4/5 are polish once behavior is right.
Phase 6 (scrollbar) depends on Phase 2's visible-count model and Phase 4's cache/timer patterns,
so it slots in after those; it is independent of Phase 5.
