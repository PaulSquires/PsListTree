
#pragma once

#include once "clsDoubleBuffer.bi"

type CLISTBOX_PAINTINFO
    itemID          as integer
    b               as clsDoubleBuffer ptr    ' points to the caller's buffer (no copy)
    rc              as RECT
    isHot           as boolean
    wszCaption      as DWSTRING
end type

type CLISTBOX_MESSAGEINFO
    hList           as HWND
    uMsg            as UINT
    wParam          as WPARAM
    lParam          as LPARAM
    idx             as integer    ' selected row
    isCtrl          as boolean
    isShift         as boolean
end type


type CLISTBOX_ROWINFO
    IsHeader        as boolean = false
    bCollapsed      as boolean = false
    Text            as DWSTRING
    itemData        as integer
end type

type PaintCallbackSub as sub( byval p as CLISTBOX_PAINTINFO ptr )
type MessageCallbackFunc as function( byval m as CLISTBOX_MESSAGEINFO ptr ) as boolean

type CLISTBOX
    hWin            as HWND
    hToolTip        as HWND 
    wszTooltip      as DWSTRING
    rows(any)       as CLISTBOX_ROWINFO
    RowHeight       as integer = 22
    idc_ListBox     as integer = 1000
    accumDelta      as integer = 0        ' mousewheel
    HoverTime       as integer = 250
    nLastHotIdx     as integer = -1       ' last row the mouse was over (per-instance hover tracking)
    ExtendSel       as boolean = false
    MultipleSel     as boolean = false
    BackColor       as COLORREF
    hFont           as HFONT              ' caller-supplied font for row text (caller owns it)
    PaintCallback   as PaintCallbackSub
    MessageCallback as MessageCallbackFunc
    
    declare function GetCount() as integer
    declare function AddRow() as CLISTBOX_ROWINFO ptr
    declare function GetRow( byval row as integer ) as CLISTBOX_ROWINFO ptr
    declare function IsValidRow( byval row as integer ) as boolean
    declare sub      Refresh()
end type

sub CLISTBOX.Refresh()
    ' tell the virtual listbox how many rows exist now
    dim as HWND hList = GetDlgItem( this.hWin, this.idc_ListBox )
    ShowWindow( hList, iif(this.GetCount(), SW_SHOW, SW_HIDE) )
    AfxRedrawWindow( hList )
end sub

function CLISTBOX.GetCount() as integer
    dim as HWND hList = GetDlgItem( this.hWin, this.idc_ListBox )
    return ListBox_GetCount( hList )
end function

function CLISTBOX.AddRow() as CLISTBOX_ROWINFO ptr
    dim as integer ub = ubound(this.rows) + 1
    redim preserve this.rows(ub)
    dim as HWND hList = GetDlgItem( this.hWin, this.idc_ListBox )
    dim as integer count = (ubound(this.rows) - lbound(this.rows) + 1)
    SendMessage( hList, LB_SETCOUNT, count, 0 )
    return @this.rows(ub)
end function

function CLISTBOX.IsValidRow( byval row as integer ) as boolean
    if (row >= lbound(this.rows)) andalso (row <= ubound(this.rows)) then
        return true
    end if
    return false    
end function

function CLISTBOX.GetRow( byval row as integer ) as CLISTBOX_ROWINFO ptr
    if this.IsValidRow(row) = false then return null 
    return @this.rows(row)
end function


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
declare function CListBox_GetText( byval hListControl as HWND, byval row as integer ) as DWSTRING
declare function CListBox_SetText( byval hListControl as HWND, byval row as integer, byval Text as DWSTRING ) as boolean
declare function CListBox_GetItemData( byval hListControl as HWND, byval row as integer ) as integer
declare function CListBox_SetItemData( byval hListControl as HWND, byval row as integer, byval itemData as integer ) as boolean
declare function CListBox_GetCurSel( byval hListControl as HWND ) as integer
declare function CListBox_SetCurSel( byval hListControl as HWND, byval row as integer ) as integer
declare function CListBox_GetRowHeight( byval hListControl as HWND ) as integer
declare function CListBox_SetRowHeight( byval hListControl as HWND, byval height as integer ) as integer
declare function CListBox_GetFont( byval hListControl as HWND ) as HFONT
declare function CListBox_SetFont( byval hListControl as HWND, byval hFont as HFONT ) as boolean
declare function CListBox_GetCount( byval hListControl as HWND ) as integer
declare function CListBox_SetMessageCallback( byval hListControl as HWND, byval userfunc as MessageCallbackFunc ) as boolean
declare sub      CListBox_SetHoverTime( byval hListControl as HWND, byval milliseconds as integer ) 
declare sub      CListBox_SetTooltip( byval hListControl as HWND, byval wszTooltip as DWSTRING )
declare sub      CListBox_Refresh( byval hListControl as HWND )
declare sub      CListBox_SetPaintCallback( byval hListControl as HWND, byval usersub as PaintCallbackSub )
