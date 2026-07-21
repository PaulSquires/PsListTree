''
''  CColumnHeader.inc  -- owner-drawn column header band
''

#include once "CColumnHeader.bi"


function GetColumnHeaderPointer( byval hHeader as HWND ) as CCOLUMNHEADER ptr
    dim pWindow as CWindow ptr = AfxCWindowPtr(hHeader)
    if pWindow = 0 then return 0
    return cast( CCOLUMNHEADER ptr, pWindow->UserData(0) )
end function


' Repaint one column's header cell. Silently does nothing for an invalid index, which is
' what makes the hover paths safe when the cursor is over empty band area (idx = -1).
' FALSE = no background erase; the cell rect is fully painted by the paint callback.
private sub CColumnHeader_InvalidateColumn( byval pHdr as CCOLUMNHEADER ptr, byval idx as integer )
    if pHdr = 0 then exit sub
    if pHdr->IsValidColumn(idx) = false then exit sub
    InvalidateRect( pHdr->hWin, @pHdr->cols(idx).rc, FALSE )
end sub


' ========================================================================================
' Add a column to the end of the header. Returns the new column's index, or -1.
' ========================================================================================
function CColumnHeader_AddColumn( byval hHeader as HWND, byval Text as DWSTRING, byval nWidth as integer = 100, byval nMinWidth as integer = 0, byval itemData as integer = 0 ) as integer
    return CColumnHeader_InsertColumn( hHeader, &h7FFFFFFF, Text, nWidth, nMinWidth, itemData )
end function


' ========================================================================================
' Insert a column at the given index (existing columns shift up). Returns the actual
' index used (clamped to [0, count]), or -1 on failure.
' ========================================================================================
function CColumnHeader_InsertColumn( byval hHeader as HWND, byval idx as integer, byval Text as DWSTRING, byval nWidth as integer = 100, byval nMinWidth as integer = 0, byval itemData as integer = 0 ) as integer
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return -1
    dim as integer at = idx
    if at < 0 then at = 0
    if at > pHdr->GetCount() then at = pHdr->GetCount()
    dim as CCOLUMNHEADER_COLINFO ptr pCol = pHdr->InsertColumnAt( at )
    if pCol = 0 then return -1
    pCol->Text      = Text
    pCol->itemData  = itemData
    if nWidth > 0 then pCol->nWidth = nWidth
    if nMinWidth > 0 then pCol->nMinWidth = nMinWidth
    return at
end function


' ========================================================================================
' Delete the column at the given index. Returns TRUE on success.
' ========================================================================================
function CColumnHeader_DeleteColumn( byval hHeader as HWND, byval idx as integer ) as boolean
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return false
    return pHdr->DeleteColumnAt( idx )
end function


' ========================================================================================
' Remove all columns from the header.
' ========================================================================================
sub CColumnHeader_Clear( byval hHeader as HWND )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return
    pHdr->Clear()
end sub


' ========================================================================================
' Retrieve the current column count.
' ========================================================================================
function CColumnHeader_GetCount( byval hHeader as HWND ) as integer
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return 0
    return pHdr->GetCount()
end function


' ========================================================================================
' Refresh (re-layout + repaint) the header.
' ========================================================================================
sub CColumnHeader_Refresh( byval hHeader as HWND )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr then pHdr->Refresh()
end sub


' ========================================================================================
' Column text.
' ========================================================================================
function CColumnHeader_GetColumnText( byval hHeader as HWND, byval idx as integer ) as DWSTRING
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return ""
    if pHdr->IsValidColumn(idx) = false then return ""
    dim as DWSTRING wszText = pHdr->GetColumn(idx)->Text
    return wszText
end function

function CColumnHeader_SetColumnText( byval hHeader as HWND, byval idx as integer, byval Text as DWSTRING ) as boolean
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return false
    if pHdr->IsValidColumn(idx) = false then return false
    pHdr->GetColumn(idx)->Text = Text
    ' Text does not drive layout (widths are stored), so a repaint of the cell suffices.
    CColumnHeader_InvalidateColumn( pHdr, idx )
    return true
end function


' ========================================================================================
' Column itemData.
' ========================================================================================
function CColumnHeader_GetColumnItemData( byval hHeader as HWND, byval idx as integer ) as integer
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return 0
    if pHdr->IsValidColumn(idx) = false then return 0
    return pHdr->GetColumn(idx)->itemData
end function

function CColumnHeader_SetColumnItemData( byval hHeader as HWND, byval idx as integer, byval itemData as integer ) as boolean
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return false
    if pHdr->IsValidColumn(idx) = false then return false
    pHdr->GetColumn(idx)->itemData = itemData
    return true
end function


' ========================================================================================
' Column width (pixels). SetColumnWidth is the programmatic door: silent by the family
' rule (no WidthChanged callback -- that is reserved for user drags). The width is
' clamped to the column's effective minimum.
' ========================================================================================
function CColumnHeader_GetColumnWidth( byval hHeader as HWND, byval idx as integer ) as integer
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return 0
    if pHdr->IsValidColumn(idx) = false then return 0
    ' The fill column's rendered width is derived; report the laid-out truth.
    if idx = pHdr->EffectiveFillCol() then
        if pHdr->bLayoutDirty then pHdr->LayoutColumns()
        return pHdr->cols(idx).rc.right - pHdr->cols(idx).rc.left
    end if
    dim as integer nWidth = pHdr->cols(idx).nWidth
    dim as integer minW = pHdr->EffectiveMinWidth(idx)
    if nWidth < minW then nWidth = minW
    return nWidth
end function

function CColumnHeader_SetColumnWidth( byval hHeader as HWND, byval idx as integer, byval nWidth as integer ) as boolean
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return false
    if pHdr->IsValidColumn(idx) = false then return false
    dim as integer minW = pHdr->EffectiveMinWidth(idx)
    if nWidth < minW then nWidth = minW
    pHdr->cols(idx).nWidth = nWidth
    ' Programmatic width change during a live drag on the SAME column: the new value
    ' becomes the drag's base, so a later ESC restores to it and the next mousemove
    ' resumes from it (re-anchored to the current cursor x).
    if idx = pHdr->nDragDividerIdx then
        pHdr->nDragOrigWidth = nWidth
        dim as POINT pt
        GetCursorPos( @pt )
        ScreenToClient( hHeader, @pt )
        pHdr->nDragAnchorX = pt.x
    end if
    pHdr->Refresh()
    return true
end function


' ========================================================================================
' Column minimum width (pixels). 0 = the scaled CCOLHDR_DEFAULT_MINWIDTH floor.
' ========================================================================================
function CColumnHeader_GetColumnMinWidth( byval hHeader as HWND, byval idx as integer ) as integer
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return 0
    if pHdr->IsValidColumn(idx) = false then return 0
    return pHdr->GetColumn(idx)->nMinWidth
end function

function CColumnHeader_SetColumnMinWidth( byval hHeader as HWND, byval idx as integer, byval nMinWidth as integer ) as boolean
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return false
    if pHdr->IsValidColumn(idx) = false then return false
    if nMinWidth < 0 then nMinWidth = 0
    pHdr->GetColumn(idx)->nMinWidth = nMinWidth
    pHdr->Refresh()
    return true
end function


' ========================================================================================
' Fill column. Set takes an index, CCOLHDR_FILL_LAST, or CCOLHDR_FILL_NONE; Get returns
' the resolved effective index (-1 = none).
' ========================================================================================
function CColumnHeader_GetFillColumn( byval hHeader as HWND ) as integer
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return -1
    return pHdr->EffectiveFillCol()
end function

function CColumnHeader_SetFillColumn( byval hHeader as HWND, byval idx as integer ) as boolean
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return false
    dim as boolean isDesignation = (idx = CCOLHDR_FILL_LAST) orelse (idx = CCOLHDR_FILL_NONE)
    if isDesignation = false then
        if pHdr->IsValidColumn(idx) = false then return false
    end if
    pHdr->fillCol = idx
    pHdr->Refresh()
    return true
end function


' ========================================================================================
' Retrieve the computed client-coordinate rect of the specified column. Forces a pending
' layout so the result is always current -- an embedding host derives row-cell
' x-coordinates from this. Returns FALSE for a bad index.
' ========================================================================================
function CColumnHeader_GetColumnRect( byval hHeader as HWND, byval idx as integer, byref rc as RECT ) as boolean
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    SetRectEmpty( @rc )
    if pHdr = 0 then return false
    if pHdr->IsValidColumn(idx) = false then return false
    if pHdr->bLayoutDirty then pHdr->LayoutColumns()
    rc = pHdr->GetColumn(idx)->rc
    return true
end function


' ========================================================================================
' Public divider hit-test (client coords), mainly for self-tests. Returns the column
' whose right-edge divider gutter contains the point, -1 otherwise.
' ========================================================================================
function CColumnHeader_HitTestDivider( byval hHeader as HWND, byval x as integer, byval y as integer ) as integer
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return -1
    return pHdr->HitTestDivider( x, y )
end function


' ========================================================================================
' Caption padding (pixels). Paint-side only: inset your caption by this much.
' ========================================================================================
function CColumnHeader_GetPadding( byval hHeader as HWND ) as integer
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return 0
    return pHdr->nPadding
end function

sub CColumnHeader_SetPadding( byval hHeader as HWND, byval nPadding as integer )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then exit sub
    if nPadding < 0 then nPadding = 0
    pHdr->nPadding = nPadding
    InvalidateRect( hHeader, NULL, FALSE )
end sub


' ========================================================================================
' Horizontal scroll offset (future CHScrollBar integration; always 0 in v1). Shifts the
' whole column run left by xOffset pixels.
' ========================================================================================
sub CColumnHeader_SetXOffset( byval hHeader as HWND, byval xOffset as integer )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then exit sub
    if xOffset < 0 then xOffset = 0
    if xOffset = pHdr->xOffset then exit sub
    pHdr->xOffset = xOffset
    pHdr->Refresh()
end sub


' ========================================================================================
' Appearance.
' ========================================================================================
function CColumnHeader_GetBackColor( byval hHeader as HWND ) as COLORREF
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return 0
    return pHdr->BackColor
end function

function CColumnHeader_SetBackColor( byval hHeader as HWND, byval clr as COLORREF ) as COLORREF
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return 0
    dim as COLORREF oldClr = pHdr->BackColor
    pHdr->BackColor = clr
    InvalidateRect( hHeader, NULL, FALSE )
    return oldClr
end function

function CColumnHeader_GetFont( byval hHeader as HWND ) as HFONT
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return 0
    return pHdr->hHeaderFont
end function

function CColumnHeader_SetFont( byval hHeader as HWND, byval hFont as HFONT ) as boolean
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return false
    pHdr->hHeaderFont = hFont
    ' The font does not drive layout (widths are stored); a repaint suffices.
    InvalidateRect( hHeader, NULL, FALSE )
    return true
end function

function CColumnHeader_GetTooltipHandle( byval hHeader as HWND ) as HWND
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return 0
    return pHdr->hToolTip
end function

sub CColumnHeader_SetHoverTime( byval hHeader as HWND, byval milliseconds as integer )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return
    pHdr->HoverTime = milliseconds
end sub


' ========================================================================================
' Callback setters.
' ========================================================================================
sub CColumnHeader_SetPaintCallback( byval hHeader as HWND, byval usersub as HDR_PaintCallbackSub )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return
    pHdr->PaintCallback = usersub
    InvalidateRect( hHeader, NULL, FALSE )
end sub

sub CColumnHeader_SetMessageCallback( byval hHeader as HWND, byval userfunc as HDR_MessageCallbackFunc )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return
    pHdr->MessageCallback = userfunc
end sub

sub CColumnHeader_SetTooltipCallback( byval hHeader as HWND, byval userfunc as HDR_TooltipCallbackFunc )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return
    pHdr->TooltipCallback = userfunc
end sub

sub CColumnHeader_SetClickCallback( byval hHeader as HWND, byval usersub as HDR_ClickCallbackSub )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return
    pHdr->ClickCallback = usersub
end sub

sub CColumnHeader_SetWidthChangedCallback( byval hHeader as HWND, byval usersub as HDR_WidthChangedCallbackSub )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return
    pHdr->WidthChangedCallback = usersub
end sub

sub CColumnHeader_SetAutoSizeCallback( byval hHeader as HWND, byval userfunc as HDR_AutoSizeCallbackFunc )
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hHeader)
    if pHdr = 0 then return
    pHdr->AutoSizeCallback = userfunc
end sub


' Resolve the tooltip text for a column: host callback if set, else the column's Text.
function CColumnHeader_ResolveTooltipText( byval pHdr as CCOLUMNHEADER ptr, byval idx as integer ) as DWSTRING
    if pHdr = 0 then return ""
    if pHdr->IsValidColumn(idx) = false then return ""
    if pHdr->TooltipCallback then return pHdr->TooltipCallback( pHdr->hWin, idx )
    dim as DWSTRING wszText = pHdr->cols(idx).Text
    return wszText
end function


' ========================================================================================
' Process WM_SIZE. No child windows to position; the client width decides how much the
' fill column absorbs, so a resize invalidates the layout.
' ========================================================================================
function CColumnHeader_OnSize( _
            byval hwnd as HWND, _
            byval state as UINT, _
            byval cx as long, _
            byval cy as long _
            ) as LRESULT
    if state <> SIZE_MINIMIZED then
        dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hwnd)
        if pHdr then pHdr->Refresh()
    end if
    function = 0
end function


' ========================================================================================
' Process WM_PAINT. Consumes any pending layout first, fills the control's flat
' background, then hands each column touched by the update rect to the host callback.
' (GetUpdateRect is a conservative bounding-box skip -- see CStatusBar_OnPaint for why
' that is sufficient here.)
' ========================================================================================
function CColumnHeader_OnPaint( byval hwnd as HWND ) as LRESULT
    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer(hwnd)
    if pHdr = 0 then return 0

    if pHdr->bLayoutDirty then pHdr->LayoutColumns()

    ' Capture the update rect BEFORE BeginDoubleBuffer -- BeginPaint clears it.
    dim as RECT rcUpdate
    if GetUpdateRect( hwnd, @rcUpdate, FALSE ) = 0 then GetClientRect( hwnd, @rcUpdate )

    dim as clsDoubleBuffer b
    b.BeginDoubleBuffer(hwnd)
    b.SetBackColor( pHdr->BackColor )

    ' Fill the whole band, which also covers any strip no column owns (the control's own
    ' background is the only thing it paints; cells belong to the paint callback).
    b.PaintClientRect()

    if pHdr->PaintCallback then
        dim p as CCOLUMNHEADER_PAINTINFO
        p.hHeader = hwnd
        p.b       = @b
        dim as integer effFill = pHdr->EffectiveFillCol()
        for i as integer = 0 to pHdr->colCount - 1
            dim as RECT rcHit
            if IntersectRect( @rcHit, @pHdr->cols(i).rc, @rcUpdate ) = 0 then continue for
            p.itemID      = i
            p.rc          = pHdr->cols(i).rc
            p.isHot       = (i = pHdr->nLastHotIdx)
            p.isPressed   = (i = pHdr->nPressedIdx)
            p.isResizeHot = (i = pHdr->nHotDividerIdx)
            p.isResizing  = (i = pHdr->nDragDividerIdx)
            p.isFill      = (i = effFill)
            p.wszCaption  = pHdr->cols(i).Text
            pHdr->PaintCallback( @p )
        next
    end if

    b.EndDoubleBuffer()
    function = 0
end function


' ========================================================================================
' Clear any hot-tracked column/divider and stop the safety-net timer. Repaints only what
' actually changes, so this is cheap to call repeatedly.
' ========================================================================================
private sub CColumnHeader_ClearHotTracking( byval pHdr as CCOLUMNHEADER ptr, byval hHeader as HWND )
    if pHdr = 0 then exit sub
    if pHdr->hotTimerOn then
        KillTimer( hHeader, IDT_CCOLHDR_HOTTRACK )
        pHdr->hotTimerOn = false
    end if
    dim as boolean changed = false
    if pHdr->nLastHotIdx <> -1 then
        CColumnHeader_InvalidateColumn( pHdr, pHdr->nLastHotIdx )
        pHdr->nLastHotIdx = -1
        changed = true
    end if
    if pHdr->nHotDividerIdx <> -1 then
        CColumnHeader_InvalidateColumn( pHdr, pHdr->nHotDividerIdx )
        CColumnHeader_InvalidateColumn( pHdr, pHdr->nHotDividerIdx + 1 )
        pHdr->nHotDividerIdx = -1
        changed = true
    end if
    if changed then UpdateWindow( hHeader )
end sub


' ========================================================================================
' Update hover state (column body + divider gutter) from a client-coordinate point.
' Invalidates only the cells whose paint flags changed. The divider highlight may be
' drawn by the host at the boundary between column i and i+1, so both get repainted.
' ========================================================================================
private sub CColumnHeader_UpdateHotTracking( byval pHdr as CCOLUMNHEADER ptr, byval x as integer, byval y as integer )
    if pHdr = 0 then exit sub

    dim as integer newDiv = pHdr->HitTestDivider( x, y )
    dim as integer newHot = pHdr->HitTestColumn( x, y )

    if newHot <> pHdr->nLastHotIdx then
        CColumnHeader_InvalidateColumn( pHdr, pHdr->nLastHotIdx )
        CColumnHeader_InvalidateColumn( pHdr, newHot )
        pHdr->nLastHotIdx = newHot
        ' Column changed: pop the current tip so it re-queries TTN_GETDISPINFOW for the
        ' new column on the next hover.
        if pHdr->hToolTip then SendMessage( pHdr->hToolTip, TTM_POP, 0, 0 )
    end if

    if newDiv <> pHdr->nHotDividerIdx then
        CColumnHeader_InvalidateColumn( pHdr, pHdr->nHotDividerIdx )
        CColumnHeader_InvalidateColumn( pHdr, pHdr->nHotDividerIdx + 1 )
        CColumnHeader_InvalidateColumn( pHdr, newDiv )
        CColumnHeader_InvalidateColumn( pHdr, newDiv + 1 )
        pHdr->nHotDividerIdx = newDiv
    end if
end sub


' ========================================================================================
' CColumnHeader Window procedure
'
' This control has no child windows, so there is nothing to subclass: every message is
' handled here and anything unhandled goes to DefWindowProc.
' ========================================================================================
function CColumnHeader_WndProc( _
            byval hwnd   as HWND, _
            byval uMsg   as UINT, _
            byval wParam as WPARAM, _
            byval lParam as LPARAM _
            ) as LRESULT

    dim as CCOLUMNHEADER ptr pHdr = GetColumnHeaderPointer( hwnd )

    ' Per-message scratch info passed to the user MessageCallback. A plain local (NOT
    ' static) so that multiple control instances never share it.
    dim m as CCOLUMNHEADER_MESSAGEINFO

    select case uMsg
        ' Prepare the callback MessageInfo for only the messages we hand to the host.
        case WM_MOUSEMOVE, WM_MOUSEHOVER, WM_MOUSELEAVE, _
             WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK
            with m
                .hHeader = hwnd
                .uMsg    = uMsg
                .wParam  = wParam
                .lParam  = lParam
                .idx     = -1
            end with
    end select

    select case uMsg
        HANDLE_MSG (hwnd, WM_SIZE,  CColumnHeader_OnSize)
        HANDLE_MSG (hwnd, WM_PAINT, CColumnHeader_OnPaint)

    case WM_ERASEBKGND
        ' Never erase: WM_PAINT fills the whole client through the double buffer.
        return true

    case WM_TIMER
        if (pHdr <> 0) andalso (wParam = IDT_CCOLHDR_HOTTRACK) then
            ' The cursor may have left without a WM_MOUSELEAVE. If it is no longer over
            ' our client area, clear the hot column ourselves.
            dim as RECT rcClient : GetClientRect( hwnd, @rcClient )
            if isMouseOverRECT( hwnd, rcClient ) = false then
                CColumnHeader_ClearHotTracking( pHdr, hwnd )
            end if
            return 0
        end if

    case WM_MOUSEMOVE
        if pHdr = 0 then return 0

        ' Track that we are over the control to catch WM_MOUSEHOVER / WM_MOUSELEAVE.
        dim tme as TrackMouseEvent
        tme.cbSize      = sizeof(TrackMouseEvent)
        tme.dwFlags     = TME_HOVER or TME_LEAVE
        tme.hwndTrack   = hwnd
        tme.dwHoverTime = pHdr->HoverTime
        TrackMouseEvent(@tme)

        ' Safety net: WM_MOUSELEAVE is not guaranteed to arrive on fast exits.
        if pHdr->hotTimerOn = false then
            SetTimer( hwnd, IDT_CCOLHDR_HOTTRACK, CCOLHDR_HOTTRACK_MS, NULL )
            pHdr->hotTimerOn = true
        end if

        ' Signed coords: under capture (phase 2 dragging) they legitimately go negative.
        dim as integer ptX = cast( short, loword(lParam) )
        dim as integer ptY = cast( short, hiword(lParam) )

        CColumnHeader_UpdateHotTracking( pHdr, ptX, ptY )

        m.idx = pHdr->nLastHotIdx
        if pHdr->MessageCallback then
            if pHdr->MessageCallback( @m ) then return 0
        end if

    case WM_MOUSEHOVER
        if pHdr then
            m.idx = pHdr->nLastHotIdx
            if pHdr->MessageCallback then
                if pHdr->MessageCallback( @m ) then return 0
            end if
        end if

    case WM_MOUSELEAVE
        if pHdr then
            ' Fast path for clearing the hot column (the WM_TIMER poll is the fallback).
            ' m.idx stays -1: the mouse is gone.
            CColumnHeader_ClearHotTracking( pHdr, hwnd )
            if pHdr->MessageCallback then
                if pHdr->MessageCallback( @m ) then return 0
            end if
        end if

    case WM_NOTIFY
        ' The tooltip asks for its text on demand; supply the current hot column's.
        dim as NMHDR ptr pnmh = cast(NMHDR ptr, lParam)
        if (pnmh <> 0) andalso (pnmh->code = TTN_GETDISPINFOW) then
            if pHdr then
                dim as NMTTDISPINFOW ptr pdi = cast(NMTTDISPINFOW ptr, lParam)
                ' store in an instance-lifetime buffer and point lpszText at it
                pHdr->wszTooltip = CColumnHeader_ResolveTooltipText( pHdr, pHdr->nLastHotIdx )
                pdi->lpszText = cast( LPWSTR, pHdr->wszTooltip.vptr )
                pdi->hinst    = NULL
            end if
            return 0
        end if

    case WM_DESTROY
        if pHdr then
            if pHdr->hotTimerOn then
                KillTimer( hwnd, IDT_CCOLHDR_HOTTRACK )
                pHdr->hotTimerOn = false
            end if
            ' Never leave a capture stranded (phase 2 press/drag holds it).
            pHdr->CancelPress()
        end if

    case WM_NCDESTROY
        if pHdr then
            if IsWindow(pHdr->hToolTip) then DestroyWindow(pHdr->hToolTip)
            delete(pHdr)
        end if
        dim as CWindow ptr pWindow = AfxCWindowPtr(hwnd)
        if pWindow then delete(pWindow)
        return 0

    end select

    ' for messages that we don't deal with
    function = DefWindowProc( hwnd, uMsg, wParam, lParam )

end function


' ========================================================================================
' Create a tooltip that pulls its text on demand (LPSTR_TEXTCALLBACK) for the whole
' control -- one tool covering the entire band, because the columns are painted rects
' rather than windows. TTN_GETDISPINFOW goes to TOOLINFO.hwnd, which is the control
' itself (NOT the parent form), and CColumnHeader_WndProc answers it using the current
' hot column.
' ========================================================================================
private function CColumnHeader_AddTooltip( byval hCtrl as HWND ) as HWND
    if IsWindow(hCtrl) = 0 then return 0
    dim as HWND hToolTip = CreateWindowExW( 0, "tooltips_class32", "", _
                                            WS_POPUP or TTS_ALWAYSTIP, _
                                            0, 0, 0, 0, NULL, NULL, NULL, NULL )
    if hToolTip = 0 then return 0
    SetWindowPos( hToolTip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE or SWP_NOSIZE or SWP_NOACTIVATE )

    dim tti as TOOLINFOW
    ' cbSize must match the running comctl32 version (see AfxNova AfxAddTooltip).
    #ifdef __FB_64BIT__
        if AfxComCtlVersion < 600 then tti.cbSize = 64 else tti.cbSize = 72
    #else
        if AfxComCtlVersion < 600 then tti.cbSize = 44 else tti.cbSize = 48
    #endif
    tti.uFlags   = TTF_IDISHWND or TTF_SUBCLASS
    tti.hwnd     = hCtrl
    tti.uId      = cast(UINT_PTR, hCtrl)
    tti.hinst    = GetModuleHandleW(NULL)
    tti.lpszText = LPSTR_TEXTCALLBACK
    SendMessageW( hToolTip, TTM_ADDTOOLW, 0, cast(LPARAM, @tti) )
    return hToolTip
end function


' ========================================================================================
' CColumnHeader_Create. The control is created zero-sized; the host positions it.
' ========================================================================================
function CColumnHeader_Create( byval hWndParent as HWND, byval CtrlID as integer ) as HWND

    dim pWindow as CWindow ptr = New CWindow

    dim as HWND hCtrl = _
            pWindow->Create( hWndParent, "", @CColumnHeader_WndProc, _
            0, 0, 0, 0, WS_CHILD or WS_CLIPSIBLINGS or WS_CLIPCHILDREN _
            )
    ' Enable double-click messages (WM_LBUTTONDBLCLK) on the control's window class for
    ' divider double-click autosize, preserving the class's existing styles rather than
    ' replacing them. (CWindow's ClassStyle setter REPLACES, so this must be
    ' read-modify-write, and it needs m_hwnd -- i.e. it only works after Create.)
    pWindow->ClassStyle = pWindow->ClassStyle or CS_DBLCLKS

    dim as CCOLUMNHEADER ptr pHdr = new CCOLUMNHEADER
    pHdr->hWin = hCtrl
    pHdr->nPadding = pWindow->ScaleX( CCOLHDR_DEFAULT_PADDING )
    SetWindowLongPtr( hCtrl, GWLP_ID, CtrlID )

    ' Store the new pointer into the user data area of the header window
    pWindow->UserData(0) = cast(LONG_PTR, pHdr)

    pHdr->hToolTip = CColumnHeader_AddTooltip( hCtrl )

    function = hCtrl

end function
