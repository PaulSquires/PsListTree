# CListBox

A reusable, owner-drawn listbox control for FreeBASIC / Win32, built on AfxNova.
Any number of instances can coexist; each owns all of its state.

- Flat, one-level **collapsible groups** (header rows + their items)
- **Multiple columns** with an optional **resizable header band** (listview report
  mode): live drag-resize with ESC cancel, divider double-click autosize, column
  click notifications for host-driven sorting, one fill column absorbing spare width
- **Model-based selection** — single / extended (Shift+Ctrl) / multi (toggle) — that
  survives collapsing and can include hidden rows
- Distinct **selected / hover / focus** visual states, drawn entirely by your callback
- Full **keyboard navigation** with ensure-visible
- **On-demand per-row tooltips** (`TTN_GETDISPINFO`), so long text isn't truncated
- **Fully owner-drawn** — no system `LISTBOX` child; the whole visible surface is painted
  in one pass, which is where most of this control's speed comes from
- An owner-drawn **vertical scrollbar** that the control creates, positions and auto-hides

## Files

| File | Purpose |
|---|---|
| `CListBox.bi` / `.inc` | The control. `CListBox.bi` is the documented public header. |
| `CColumnHeader.bi` / `.inc` | The column header band CListBox embeds. Generic — works standalone; `CColumnHeader.bi` documents its own API. |
| `CVScrollBar.bi` / `.inc` | **Vendored build copy** of the scrollbar CListBox embeds. Canonical home: [CVScrollBar](https://github.com/PaulSquires/CVScrollBar) — develop there, sync copies here (the same way tiko vendors its control copies). |
| `clsDoubleBuffer.bi` / `.inc` | Flicker-free drawing helper used by all three |
| `main.bas`, `frmMain.bi` / `.inc` | Demo / test harness (two instances) + `CLISTBOX_SELFTEST=1` geometry self-test |

Include order matters — `CVScrollBar` and `CColumnHeader` before `CListBox`:

```freebasic
#include once "clsDoubleBuffer.inc"
#include once "CVScrollBar.inc"
#include once "CColumnHeader.inc"
#include once "CListBox.inc"
```

## Quick start

```freebasic
' Create, then position it like any window.
dim as HWND hList = CListBox_Create( hWndParent, IDC_MYLIST )
SetWindowPos( hList, 0, x, y, cx, cy, SWP_NOZORDER or SWP_SHOWWINDOW )

CListBox_SetPaintCallback( hList, @MyPaintCallback )   ' required to see anything
CListBox_SetFont( hList, hMyFont )                     ' you keep ownership
CListBox_SetBackColor( hList, BGR(33,37,43) )
CListBox_SetExtendedSelect( hList, true )              ' Shift/Ctrl selection

' Bulk loads: batch them (turns an O(n^2) load into O(n)).
CListBox_BeginUpdate( hList )
CListBox_AddHeader( hList, "Group One" )
for i as integer = 0 to 999
    CListBox_AddString( hList, "item " & i, i )
next
CListBox_EndUpdate( hList )
```

Paint one row:

```freebasic
sub MyPaintCallback( byval p as CLISTBOX_PAINTINFO ptr )
    dim as COLORREF backclr, foreclr
    if p->isSelected then                  ' state priority: selected > hot > normal
        backclr = clrSelect : foreclr = clrSelectText
    elseif p->isHot then
        backclr = clrHot    : foreclr = clrHotText
    else
        backclr = clrBack   : foreclr = clrText
    end if
    p->b->SetForeColors( foreclr, foreclr )
    p->b->SetBackColors( backclr, backclr )
    p->b->PaintRect( @p->rc )

    p->b->SetFont( iif( p->isHeader, hBoldFont, hFont ) )
    p->b->PaintText( p->wszCaption, @p->rc, DT_LEFT )
end sub
```

## Columns and the header band

Everything about columns is optional — define none and the control behaves exactly as it
always did. Column state lives in an embedded `CColumnHeader` child (listbox id =
`CtrlID`, scrollbar `CtrlID+1`, header `CtrlID+2`); the `CListBox_*` column wrappers
delegate to it.

```freebasic
CListBox_AddColumn( hList, "Name", 160 )       ' widths are PIXELS
CListBox_AddColumn( hList, "Size", 70 )
CListBox_AddColumn( hList, "Type" )            ' last column = fill by default
CListBox_SetHeaderPaintCallback( hList, @MyHeaderPaint )
CListBox_ShowHeader( hList )                   ' optional -- columns work without it

dim as integer r = CListBox_AddString( hList, "main.bas" )   ' column 0 = the row text
CListBox_SetCellText( hList, r, 1, "12,405" )
CListBox_SetCellText( hList, r, 2, "FreeBASIC source" )
```

- **Cell storage is sparse and independent of the column definitions.** Column 0 *is*
  the row's text (`SetText`/`SetCellText(…, 0, …)` are interchangeable); higher cells
  read back `""` until set, so populate-then-define and define-then-populate both work.
- **Row painting**: `CLISTBOX_PAINTINFO` gains `columnCount` and a `cells` array
  (per-column rect + text). Background-fill the full `p->rc` first (selection spans the
  row, listview-style), then draw each `cells[i].wszText` inside `cells[i].rc` — the
  control does not clip between cells, so use `DT_END_ELLIPSIS`. `columnCount = 0`
  (no columns, or a **group header row** — those always span) is the classic contract.
- **The fill column** (default: the last; `CCOLHDR_FILL_NONE` opts out) absorbs the
  leftover width, shrinking and growing with the control, never below its minimum. Its
  own divider is not draggable.
- **User resize notifies, programmatic never does** (the family rule).
  `CListBox_SetColumnResizeCallback` fires per move (`bLive=true`) and once on
  commit/cancel (`bLive=false`); `SetColumnWidth` is silent. ESC during a drag restores
  the pre-drag width. Divider double-click asks your
  `CListBox_SetColumnAutoSizeCallback` for a best-fit pixel width (you own the cell
  data and fonts; return <= 0 to decline).
- **Sorting is yours.** A column body click fires `CListBox_SetColumnClickCallback`;
  the control never reorders rows — sort your data, repopulate, and draw the arrow
  glyph in your header paint callback.
- **Callback ownership:** on the embedded header the `HDR_WidthChangedCallback` slot
  belongs to CListBox (its chain hook repaints the rows). Subscribe through
  `CListBox_SetColumnResizeCallback`, never via `CColumnHeader_SetWidthChangedCallback`
  on the child returned by `CListBox_GetHeader`.
- **DPI convention:** column widths and minimums are pixels (drag deltas are pixels);
  `HeaderHeight` and `RowHeight` are unscaled units, scaled at layout. The header's own
  defaults (padding, divider gutter, min-width floor) DPI-scale internally.

## Concepts worth knowing

**The control handle is the container window.** `CListBox_Create` returns the parent that
hosts the row surface, scrollbar and header children. Pass *that* to every `CListBox_*`
call — never a child handle. It stays a real `HWND` deliberately, so you can
`SetWindowPos` it.

**There is no system `LISTBOX` any more.** The rows are painted onto a plain owner-drawn
child — the *surface* — in a single pass per `WM_PAINT`, where the control used to hand
off to the listbox and receive one `WM_DRAWITEM` per visible row. The listbox had been
carrying almost nothing by then: 8 distinct `LB_*` messages, of which 15 of ~22 call sites
were just get/set top index. Selection, focus, keyboard navigation, hot tracking, group
collapse, cell storage, the scrollbar and the header were already this control's own code.

Collapsing N paint cycles into one is **the** performance result of that change, and it is
backend-independent: a 2000-row repaint got 25% faster on GDI and 38% faster on GDI+, as
well as 35× faster on Direct2D. Gone with the listbox: the `WM_DRAWITEM`/`WM_MEASUREITEM`
path, the one-row back-buffer cache (`EnsureCache`/`FreeCache` and five fields),
`WM_CTLCOLORLISTBOX` and its brush, and the empty-client-area helper — the surface fills
its own client whether or not there are rows in it.

**One host-visible contract changed with it.** The row rect (`PAINTINFO.rc`) and each
`cells[i].rc` are now in the **surface's client coordinates**, not a row-sized buffer
starting at `y = 0`. A row painter that derives everything from `p->rc` — which it must
already do, to honour the row's left edge and width — needs no change at all. One that
hardcodes `0` for the top does. Grep your paint callbacks for a literal zero in a row rect
before upgrading.

**Row indices are model indices.** Rows are numbered in the order you added them.
Collapsing a group never renumbers anything, and hidden rows still work with every row API
including selection. Everything the control hands back — `PAINTINFO.itemID`,
`MESSAGEINFO.idx`, `GetCurSel`, `GetSelItems` — is a model index.

**Groups are flat.** A row is a header or an item; items belong to the nearest preceding
header. One level, no nesting. Headers are selectable and *are* returned by `GetSelItems`,
so use `CListBox_IsHeader()` to tell them apart.

**Selection lives on the rows**, which is why it survives collapse/expand. (This was
deliberate back when a `LISTBOX` with its own selection bits was still underneath; now
there is no other place it could live.) `GetSelItems` reports every selected row, hidden
ones included.

## Input

| Mouse | |
|---|---|
| Click item | Select (per mode: single / Ctrl toggle / Shift range / multi toggle) |
| Click header | Folds the group **without changing the selection** |
| Wheel | Scrolls by the system's lines-per-notch |

| Keyboard | |
|---|---|
| Up/Down, PageUp/Dn, Home/End | Move focus (Shift extends, Ctrl moves focus only) |
| Left / Right | Collapse / expand a header; Left from an item jumps to its header |
| Space | Toggle selection — the only way to select a header without the mouse |

## Callbacks

| Callback | Contract |
|---|---|
| `SetPaintCallback` | Draw one row via `p->b`. Required. Called per visible row per repaint — keep it cheap. |
| `SetMessageCallback` | Observe mouse messages; return TRUE to suppress default handling. **Ignored for `WM_LBUTTONUP`** — the listbox releases its mouse capture there, and swallowing it strands the capture. |
| `SetTooltipCallback` | Return per-row text on demand; `""` for none. Defaults to the row's text. |
| `SetSelChangeCallback` | The **user** moved the selection — mouse or keyboard. Silent for `SetCurSel`/`SetSel`/`SelectAll`/`Clear`, and silent when the selection does not actually change. **The only way to observe keyboard navigation**: the control consumes `WM_KEYDOWN` itself, so arrow/Page/Home/End moves never reach `SetMessageCallback`. |
| `SetScrollBarPaintCallback` | Optional; the scrollbar has a working default paint. |
| `SetHeaderPaintCallback` | Draw one header column cell (`CCOLUMNHEADER_PAINTINFO`: hot/pressed/resize-hot/resizing/fill states). Required to see the header band. |
| `SetColumnResizeCallback` | User resized a column: per move (`bLive=true`) + final (`bLive=false`). Programmatic setters are silent. |
| `SetColumnClickCallback` | Completed click on a column body — the sorting hook. |
| `SetColumnAutoSizeCallback` | Divider double-click: return best-fit width in pixels, `<= 0` to decline. |
| `SetHeaderTooltipCallback` | Per-column header tooltip text on demand. |

## Scrollbar

Created, positioned, ranged and **auto-hidden** by CListBox — it appears only while the
rows overflow, and the listbox reclaims the width otherwise. You never wire it up; the
`CListBox_SetScrollBar*` calls are only for theming. `CVScrollBar` itself is generic
(`SetRange(total, page, pos)` + a scroll callback) and knows nothing about listboxes, so
it can be reused standalone.

## Building

```
build.bat gdi | gdiplus | d2d
```

Built and tested with FreeBASIC 1.10.1 (64-bit) against AfxNova. `main.rc` embeds a
manifest that makes the demo **DPI-aware**; that is not cosmetic, since geometry measured
in a DPI-unaware process is a measurement of a stretched bitmap.

## Rendering backend

The vendored `clsDoubleBuffer` builds against **GDI, GDI+ or Direct2D + DirectWrite**.
The control's own source is identical on all three.

Adopting the D2D backend adds host obligations, all of which fail **silently**:

```freebasic
' 1. bracket the message pump, beside AfxGdipInit / AfxGdipShutdown
clsDoubleBuffer_InitD2D()  ...  clsDoubleBuffer_ShutdownD2D()

' 2. in the teardown of EVERY window that paints. Unconditional -- a no-op on GDI/GDI+.
case WM_NCDESTROY
    clsDoubleBuffer_ReleaseTarget( hwnd )

' 3. register private fonts a SECOND time -- DirectWrite cannot see an FR_PRIVATE font,
'    so the group chevrons and the sort arrow would fall back with no error at all.
clsDoubleBuffer_AddPrivateFont( wszFontFile )

' 4. choose the text mode from the display, at startup AND on WM_DPICHANGED. Skip it and
'    the mode stays on the library default rather than the one this display calls for.
DBufD2D_AutoSelectTextMode( hwnd )
```

**Obligation 2 is per-window, and one CListBox owns four painting windows** — container,
row surface, column header and scrollbar. Each releases its own; a host only has to cover
its own forms. `clsDoubleBuffer_ShutdownD2D` counts and reports whatever is still
registered at exit, which is what makes the obligation checkable rather than merely
documented: with all the calls removed, this two-instance demo reports **8** live targets.

## Design notes

The `REFACTOR_PLAN.md` file records why the control is shaped this way, phase by phase —
including several subtle Win32 traps that are easy to reintroduce: `TME_LEAVE` is not
reliably delivered (both controls poll as a safety net), swallowing `WM_LBUTTONUP` leaks
the mouse capture, and a scrollbar that decides its own visibility from its own size
deadlocks (never sized → never needed → never shown).
