
#pragma once

#include once "clsDoubleBuffer.bi"

' Polling timer that guarantees hot-tracking is cleared when the mouse leaves the
' control. WM_MOUSELEAVE (TME_LEAVE) is not reliably delivered on fast exits, so a
' periodic cursor check acts as a safety net. Timer IDs are per-window, so every
' instance can share this id. Value is deliberately unusual to avoid colliding with
' any timer the standard listbox uses internally.
#define IDT_CLISTBOX_HOTTRACK   &hCB01
#define CLISTBOX_HOTTRACK_MS    100

type CLISTBOX_PAINTINFO
    itemID          as integer                ' MODEL row index (not the visible/listbox index)
    b               as clsDoubleBuffer ptr    ' points to the caller's buffer (no copy)
    rc              as RECT
    isHot           as boolean                ' mouse is hovering this row
    isSelected      as boolean                ' row is part of the selection
    isFocused       as boolean                ' row has the keyboard focus (caret)
    isHeader        as boolean                ' this row is a group header
    isCollapsed     as boolean                ' header only: its child items are hidden
    wszCaption      as DWSTRING
end type

type CLISTBOX_MESSAGEINFO
    hList           as HWND
    uMsg            as UINT
    wParam          as WPARAM
    lParam          as LPARAM
    idx             as integer    ' MODEL row index under the mouse (-1 if none)
    isCtrl          as boolean
    isShift         as boolean
end type


type CLISTBOX_ROWINFO
    IsHeader        as boolean = false
    bCollapsed      as boolean = false
    selected        as boolean = false    ' selection is stored in the model, not the listbox
    Text            as DWSTRING
    itemData        as integer
end type

type PaintCallbackSub as sub( byval p as CLISTBOX_PAINTINFO ptr )
type MessageCallbackFunc as function( byval m as CLISTBOX_MESSAGEINFO ptr ) as boolean
' Return the tooltip text for a MODEL row on demand (empty = no tooltip).
type TooltipCallbackFunc as function( byval hListControl as HWND, byval row as integer ) as DWSTRING

type CLISTBOX
    hWin            as HWND
    hToolTip        as HWND
    wszTooltip      as DWSTRING
    ' --- Model: rows() is the backing store (capacity = ubound+1); rowCount is
    '     the number of logical rows and the single source of truth for "count". ---
    rows(any)       as CLISTBOX_ROWINFO
    rowCount        as integer = 0
    ' --- View: visibleMap(v) -> model row index, for v = 0..visibleCount-1.
    '     Rebuilt on any change / collapse-expand. The listbox (LBS_NODATA) count
    '     is always set to visibleCount. ---
    visibleMap(any) as integer
    visibleCount    as integer = 0
    updateDepth     as integer = 0        ' BeginUpdate/EndUpdate nesting (defers refresh)
    RowHeight       as integer = 22
    idc_ListBox     as integer = 1000
    accumDelta      as integer = 0        ' mousewheel
    HoverTime       as integer = 250
    nLastHotIdx     as integer = -1       ' last VISIBLE row the mouse was over (hover tracking)
    hotTimerOn      as boolean = false    ' is the hot-tracking safety-net timer running?
    focusRow        as integer = -1       ' MODEL row with the keyboard focus/caret (-1 = none)
    anchorRow       as integer = -1       ' MODEL row anchoring a shift-range selection
    ExtendSel       as boolean = false
    MultipleSel     as boolean = false
    BackColor       as COLORREF
    hFont           as HFONT              ' caller-supplied font for row text (caller owns it)
    PaintCallback   as PaintCallbackSub
    MessageCallback as MessageCallbackFunc
    TooltipCallback as TooltipCallbackFunc    ' optional; defaults to the row's Text
    ' --- Reusable one-row back buffer, so WM_DRAWITEM doesn't create/destroy a
    '     compatible DC + bitmap for every row on every repaint. ---
    cacheDC         as HDC
    cacheBmp        as HBITMAP
    cacheOldBmp     as HBITMAP
    cacheW          as integer = 0
    cacheH          as integer = 0

    declare destructor()
    declare function EnsureCache( byval refDC as HDC, byval w as integer, byval h as integer ) as HDC
    declare sub      FreeCache()
    declare function GetCount() as integer                                  ' model row count
    declare function GetVisibleCount() as integer
    declare function AddRow() as CLISTBOX_ROWINFO ptr                       ' append
    declare function InsertRowAt( byval modelRow as integer ) as CLISTBOX_ROWINFO ptr
    declare function DeleteRowAt( byval modelRow as integer ) as boolean
    declare sub      Clear()
    declare function GetRow( byval row as integer ) as CLISTBOX_ROWINFO ptr
    declare function IsValidRow( byval row as integer ) as boolean
    declare function ModelToVisible( byval modelRow as integer ) as integer ' -1 if hidden/invalid
    declare function VisibleToModel( byval visRow as integer ) as integer   ' -1 if invalid
    declare function IsRowSelected( byval modelRow as integer ) as boolean
    declare sub      SetRowSelected( byval modelRow as integer, byval state as boolean )
    declare sub      ClearSelection()
    declare sub      SelectOnly( byval modelRow as integer )
    declare sub      SelectRange( byval a as integer, byval b as integer )
    declare function GetSelCount() as integer
    declare sub      RebuildVisibleMap()
    declare sub      BeginUpdate()
    declare sub      EndUpdate()
    declare sub      NotifyChange()
    declare sub      Refresh()
end type

destructor CLISTBOX()
    this.FreeCache()
end destructor

' Return a cached memDC whose selected bitmap is at least w x h, (re)creating it
' only when the requested size changes. Bitmap is compatible with refDC.
function CLISTBOX.EnsureCache( byval refDC as HDC, byval w as integer, byval h as integer ) as HDC
    if (this.cacheDC <> 0) andalso (this.cacheW = w) andalso (this.cacheH = h) then
        return this.cacheDC
    end if
    this.FreeCache()
    this.cacheDC     = CreateCompatibleDC( refDC )
    this.cacheBmp    = CreateCompatibleBitmap( refDC, w, h )
    this.cacheOldBmp = SelectObject( this.cacheDC, this.cacheBmp )
    this.cacheW      = w
    this.cacheH      = h
    return this.cacheDC
end function

sub CLISTBOX.FreeCache()
    if this.cacheDC then
        if this.cacheOldBmp then SelectObject( this.cacheDC, this.cacheOldBmp )
        DeleteDC( this.cacheDC )
        this.cacheDC = 0
    end if
    if this.cacheBmp then
        DeleteObject( this.cacheBmp )
        this.cacheBmp = 0
    end if
    this.cacheOldBmp = 0
    this.cacheW = 0
    this.cacheH = 0
end sub

function CLISTBOX.GetCount() as integer
    return this.rowCount
end function

function CLISTBOX.GetVisibleCount() as integer
    return this.visibleCount
end function

function CLISTBOX.IsValidRow( byval row as integer ) as boolean
    return (row >= 0) andalso (row < this.rowCount)
end function

function CLISTBOX.GetRow( byval row as integer ) as CLISTBOX_ROWINFO ptr
    if this.IsValidRow(row) = false then return null
    return @this.rows(row)
end function

' Insert a fresh (reset) row at modelRow, shifting later rows up. Grows the
' backing store by doubling so bulk inserts are amortized O(1), not O(n^2).
function CLISTBOX.InsertRowAt( byval modelRow as integer ) as CLISTBOX_ROWINFO ptr
    if modelRow < 0 then modelRow = 0
    if modelRow > this.rowCount then modelRow = this.rowCount

    dim as integer cap = ubound(this.rows) + 1
    if this.rowCount >= cap then
        dim as integer newcap = iif( cap = 0, 16, cap * 2 )
        redim preserve this.rows( 0 to newcap - 1 )
    end if

    ' shift [modelRow .. rowCount-1] up by one (no-op when appending)
    for i as integer = this.rowCount to modelRow + 1 step -1
        this.rows(i) = this.rows(i - 1)
    next

    ' reset the new slot (frees any DWSTRING left in a recycled capacity slot)
    with this.rows(modelRow)
        .IsHeader   = false
        .bCollapsed = false
        .Text       = ""
        .itemData   = 0
    end with

    this.rowCount += 1
    this.NotifyChange()
    return @this.rows(modelRow)
end function

function CLISTBOX.AddRow() as CLISTBOX_ROWINFO ptr
    return this.InsertRowAt( this.rowCount )
end function

function CLISTBOX.DeleteRowAt( byval modelRow as integer ) as boolean
    if this.IsValidRow(modelRow) = false then return false
    ' shift [modelRow+1 .. rowCount-1] down by one
    for i as integer = modelRow to this.rowCount - 2
        this.rows(i) = this.rows(i + 1)
    next
    this.rows(this.rowCount - 1).Text = ""   ' free the vacated last slot's string
    this.rowCount -= 1
    this.NotifyChange()
    return true
end function

sub CLISTBOX.Clear()
    for i as integer = 0 to this.rowCount - 1
        this.rows(i).Text = ""
    next
    this.rowCount = 0
    this.NotifyChange()
end sub

' Rebuild the visible map from the model (flat, one-level grouping: an item is
' hidden iff its nearest preceding header is collapsed) and push the visible
' count into the LBS_NODATA listbox.
sub CLISTBOX.RebuildVisibleMap()
    this.visibleCount = 0
    if this.rowCount > 0 then
        redim this.visibleMap( 0 to this.rowCount - 1 )
        dim as integer vis = 0
        dim as boolean collapsed = false
        for i as integer = 0 to this.rowCount - 1
            if this.rows(i).IsHeader then
                collapsed = this.rows(i).bCollapsed
                this.visibleMap(vis) = i : vis += 1
            elseif collapsed = false then
                this.visibleMap(vis) = i : vis += 1
            end if
        next
        this.visibleCount = vis
    else
        erase this.visibleMap
    end if
    dim as HWND hList = GetDlgItem( this.hWin, this.idc_ListBox )
    if hList then SendMessage( hList, LB_SETCOUNT, this.visibleCount, 0 )
end sub

function CLISTBOX.ModelToVisible( byval modelRow as integer ) as integer
    for v as integer = 0 to this.visibleCount - 1
        if this.visibleMap(v) = modelRow then return v
    next
    return -1
end function

function CLISTBOX.VisibleToModel( byval visRow as integer ) as integer
    if (visRow < 0) orelse (visRow >= this.visibleCount) then return -1
    return this.visibleMap(visRow)
end function

' --- Selection is stored per-row in the model, so it survives collapse/expand
'     index shifts and can include hidden rows and headers. ---
function CLISTBOX.IsRowSelected( byval modelRow as integer ) as boolean
    if this.IsValidRow(modelRow) = false then return false
    return this.rows(modelRow).selected
end function

sub CLISTBOX.SetRowSelected( byval modelRow as integer, byval state as boolean )
    if this.IsValidRow(modelRow) then this.rows(modelRow).selected = state
end sub

sub CLISTBOX.ClearSelection()
    for i as integer = 0 to this.rowCount - 1
        this.rows(i).selected = false
    next
end sub

sub CLISTBOX.SelectOnly( byval modelRow as integer )
    this.ClearSelection()
    if this.IsValidRow(modelRow) then this.rows(modelRow).selected = true
end sub

sub CLISTBOX.SelectRange( byval a as integer, byval b as integer )
    if a > b then swap a, b
    if a < 0 then a = 0
    if b > this.rowCount - 1 then b = this.rowCount - 1
    for i as integer = a to b
        this.rows(i).selected = true
    next
end sub

function CLISTBOX.GetSelCount() as integer
    dim as integer n = 0
    for i as integer = 0 to this.rowCount - 1
        if this.rows(i).selected then n += 1
    next
    return n
end function

sub CLISTBOX.BeginUpdate()
    this.updateDepth += 1
end sub

sub CLISTBOX.EndUpdate()
    if this.updateDepth > 0 then this.updateDepth -= 1
    if this.updateDepth = 0 then this.Refresh()
end sub

' Called by every model mutator. Coalesces into a single Refresh when a
' BeginUpdate/EndUpdate batch is active.
sub CLISTBOX.NotifyChange()
    if this.updateDepth = 0 then this.Refresh()
end sub

sub CLISTBOX.Refresh()
    this.RebuildVisibleMap()
    dim as HWND hList = GetDlgItem( this.hWin, this.idc_ListBox )
    if hList = 0 then exit sub
    ShowWindow( hList, SW_SHOW )
    ' Repaint WITH background erase so the vacated region below the last row is
    ' cleared when the list shrinks (delete / collapse).
    InvalidateRect( hList, NULL, TRUE )
end sub


' ----------------------------------------------------------------------------------------
' PUBLIC API
' Every CListBox_* function takes the control handle returned by CListBox_Create() -- this
' is the parent container window that hosts the owner-drawn LISTBOX child. Internally the
' functions resolve the child listbox via GetDlgItem() as needed. Do NOT pass the child
' listbox handle directly.
' ----------------------------------------------------------------------------------------
declare function CListBox_Create( byval hWndParent as HWND, byval CtrlID as integer ) as HWND
declare function CListBox_GetBackColor( byval hListControl as HWND ) as COLORREF
declare function CListBox_SetBackColor( byval hListControl as HWND, byval clr as COLORREF ) as COLORREF 
declare function CListBox_AddString( byval hListControl as HWND, byval Text as DWSTRING, byval itemData as integer = 0 ) as integer
declare function CListBox_AddHeader( byval hListControl as HWND, byval Text as DWSTRING, byval itemData as integer = 0 ) as integer
declare function CListBox_InsertString( byval hListControl as HWND, byval row as integer, byval Text as DWSTRING, byval itemData as integer = 0 ) as integer
declare function CListBox_DeleteString( byval hListControl as HWND, byval row as integer ) as boolean
declare sub      CListBox_Clear( byval hListControl as HWND )
declare function CListBox_IsHeader( byval hListControl as HWND, byval row as integer ) as boolean
declare function CListBox_IsCollapsed( byval hListControl as HWND, byval row as integer ) as boolean
declare function CListBox_CollapseRow( byval hListControl as HWND, byval row as integer ) as boolean
declare function CListBox_ExpandRow( byval hListControl as HWND, byval row as integer ) as boolean
declare function CListBox_ToggleRow( byval hListControl as HWND, byval row as integer ) as boolean
declare sub      CListBox_BeginUpdate( byval hListControl as HWND )
declare sub      CListBox_EndUpdate( byval hListControl as HWND )
declare function CListBox_GetVisibleCount( byval hListControl as HWND ) as integer
declare function CListBox_GetText( byval hListControl as HWND, byval row as integer ) as DWSTRING
declare function CListBox_SetText( byval hListControl as HWND, byval row as integer, byval Text as DWSTRING ) as boolean
declare function CListBox_GetItemData( byval hListControl as HWND, byval row as integer ) as integer
declare function CListBox_SetItemData( byval hListControl as HWND, byval row as integer, byval itemData as integer ) as boolean
declare function CListBox_GetCurSel( byval hListControl as HWND ) as integer
declare function CListBox_SetCurSel( byval hListControl as HWND, byval row as integer ) as integer
declare function CListBox_GetSel( byval hListControl as HWND, byval row as integer ) as boolean
declare function CListBox_SetSel( byval hListControl as HWND, byval row as integer, byval state as boolean ) as boolean
declare function CListBox_GetSelCount( byval hListControl as HWND ) as integer
declare function CListBox_GetSelItems( byval hListControl as HWND, selItems() as integer ) as integer
declare sub      CListBox_SelectAll( byval hListControl as HWND, byval state as boolean )
declare function CListBox_SetMultiSelect( byval hListControl as HWND, byval enable as boolean ) as boolean
declare function CListBox_SetExtendedSelect( byval hListControl as HWND, byval enable as boolean ) as boolean
declare function CListBox_GetRowHeight( byval hListControl as HWND ) as integer
declare function CListBox_SetRowHeight( byval hListControl as HWND, byval height as integer ) as integer
declare function CListBox_GetFont( byval hListControl as HWND ) as HFONT
declare function CListBox_SetFont( byval hListControl as HWND, byval hFont as HFONT ) as boolean
declare function CListBox_GetCount( byval hListControl as HWND ) as integer
declare function CListBox_SetMessageCallback( byval hListControl as HWND, byval userfunc as MessageCallbackFunc ) as boolean
declare sub      CListBox_SetHoverTime( byval hListControl as HWND, byval milliseconds as integer )
declare sub      CListBox_SetTooltipCallback( byval hListControl as HWND, byval userfunc as TooltipCallbackFunc )
declare sub      CListBox_Refresh( byval hListControl as HWND )
declare sub      CListBox_SetPaintCallback( byval hListControl as HWND, byval usersub as PaintCallbackSub )
