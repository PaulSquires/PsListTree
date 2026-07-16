# CListBox

A reusable, owner-drawn listbox control for FreeBASIC / Win32, built on AfxNova.
Any number of instances can coexist; each owns all of its state.

- Flat, one-level **collapsible groups** (header rows + their items)
- **Model-based selection** — single / extended (Shift+Ctrl) / multi (toggle) — that
  survives collapsing and can include hidden rows
- Distinct **selected / hover / focus** visual states, drawn entirely by your callback
- Full **keyboard navigation** with ensure-visible
- **On-demand per-row tooltips** (`TTN_GETDISPINFO`), so long text isn't truncated
- Cached per-row rendering (no GDI churn), correct wheel scrolling
- An owner-drawn **vertical scrollbar** that the control creates, positions and auto-hides

## Files

| File | Purpose |
|---|---|
| `CListBox.bi` / `.inc` | The control. `CListBox.bi` is the documented public header. |
| `CVScrollBar.bi` / `.inc` | Owner-drawn vertical scrollbar (standalone; CListBox drives it) |
| `clsDoubleBuffer.bi` / `.inc` | Flicker-free drawing helper used by both |
| `main.bas`, `frmMain.bi` / `.inc` | Demo / test harness (two instances) |

Include order matters — `CVScrollBar` before `CListBox`:

```freebasic
#include once "clsDoubleBuffer.inc"
#include once "CVScrollBar.inc"
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

## Concepts worth knowing

**The control handle is the container window.** `CListBox_Create` returns the parent that
hosts the listbox and scrollbar children. Pass *that* to every `CListBox_*` call — never a
child handle. It stays a real `HWND` deliberately, so you can `SetWindowPos` it.

**Row indices are model indices.** Rows are numbered in the order you added them.
Collapsing a group never renumbers anything, and hidden rows still work with every row API
including selection. Everything the control hands back — `PAINTINFO.itemID`,
`MESSAGEINFO.idx`, `GetCurSel`, `GetSelItems` — is a model index.

**Groups are flat.** A row is a header or an item; items belong to the nearest preceding
header. One level, no nesting. Headers are selectable and *are* returned by `GetSelItems`,
so use `CListBox_IsHeader()` to tell them apart.

**Selection lives on the rows**, not in the listbox's own selection bits, which is why it
survives collapse/expand. `GetSelItems` reports every selected row, hidden ones included.

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
| `SetScrollBarPaintCallback` | Optional; the scrollbar has a working default paint. |

## Scrollbar

Created, positioned, ranged and **auto-hidden** by CListBox — it appears only while the
rows overflow, and the listbox reclaims the width otherwise. You never wire it up; the
`CListBox_SetScrollBar*` calls are only for theming. `CVScrollBar` itself is generic
(`SetRange(total, page, pos)` + a scroll callback) and knows nothing about listboxes, so
it can be reused standalone.

## Building

```
fbc -i <path-containing-AfxNova> main.bas
```

Built and tested with FreeBASIC 1.10.1 (64-bit) against AfxNova.

## Design notes

The `REFACTOR_PLAN.md` file records why the control is shaped this way, phase by phase —
including several subtle Win32 traps that are easy to reintroduce: `TME_LEAVE` is not
reliably delivered (both controls poll as a safety net), swallowing `WM_LBUTTONUP` leaks
the mouse capture, and a scrollbar that decides its own visibility from its own size
deadlocks (never sized → never needed → never shown).
