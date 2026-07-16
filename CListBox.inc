
''
''  CListBox.inc
''   

#include once "CListBox.bi"


function GetListBoxPointer( byval hListControl as HWND ) as CLISTBOX ptr
    ' hListBox is actually the overall parent form (ListBox is the child)
    dim pWindow as CWindow ptr = AfxCWindowPtr(hListControl)
    dim as CLISTBOX ptr pList = null
    if pWindow then pList = cast( CLISTBOX ptr, pWindow->UserData(0) )
    return pList
end function


' ========================================================================================
' Calcluate the client area at bottom of Listbox not covered by a row (needed to manually
' paint the unused area in WM_ERASEBKGRD messages to avoid flicker.
' ========================================================================================
function GetListBoxEmptyClientArea( byval hListBox as HWND ) as RECT
    dim as RECT rc: GetClientRect( hListBox, @rc )
    ' if the number of lines in the listbox is less than the number per page then 
    ' calculate from last item to bottom of listbox, otherwise calculate based on
    ' the mod of the lineheight to listbox height so we can color the partial line
    ' that won't be displayed at the bottom of the list.
    dim as RECT rcItem
    SendMessage( hListBox, LB_GETITEMRECT, 0, cast(LPARAM, @rcItem) )
    dim as long itemHeight = rcItem.bottom - rcItem.top
    dim as long NumItems = ListBox_GetCount(hListBox)
    ' NOTE: do NOT divide by itemHeight here -- on an empty list LB_GETITEMRECT(0)
    ' fails and itemHeight is 0. ItemsPerPage is (re)computed below only when
    ' NumItems > 0, so item 0 is guaranteed to exist there.
    dim as long ItemsPerPage
    dim as long nTopIndex = SendMessage( hListBox, LB_GETTOPINDEX, 0, 0 )
    dim as long visible_rows = 0

    if NumItems > 0 then
        ItemsPerPage = (rc.bottom - rc.top) / itemHeight
        dim as long bottom_index = (nTopIndex + ItemsPerPage)
        if bottom_index >= NumItems then bottom_index = NumItems - 1
        visible_rows = (bottom_index - nTopIndex) + 1
    end if

    rc.top = visible_rows * itemHeight 
    if rc.top > rc.bottom then rc.top = rc.bottom
    
    return rc
end function


' ========================================================================================
' Add a string (and optional itemData) to the end of the custom listbox control.
' ========================================================================================
function CListBox_AddString( byval hListControl as HWND, byval Text as DWSTRING, byval itemData as integer = 0 ) as integer
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return -1
    dim as CLISTBOX_ROWINFO ptr pRow = pList->AddRow()
    if pRow = null then return -1
    pRow->Text = Text
    pRow->itemData = itemData
    return pList->GetCount() - 1  ' ensure zero based
end function


' ========================================================================================
' Set specified row text in the custom listbox control. Returns TRUE on success,
' FALSE if the row index is invalid.
' ========================================================================================
function CListBox_SetText( byval hListControl as HWND, byval row as integer, byval Text as DWSTRING ) as boolean
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return false
    if pList->IsValidRow(row) then
        pList->GetRow(row)->Text = Text
        return true     ' success
    end if
    return false  ' invalid row
end function


' ========================================================================================
' Retrieve specified row text from the custom listbox control.
' ========================================================================================
function CListBox_GetText( byval hListControl as HWND, byval row as integer ) as DWSTRING
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return ""
    if pList->IsValidRow(row) = false then return ""
    dim as CLISTBOX_ROWINFO ptr pRow = pList->GetRow(row)
    dim as DWSTRING wszText = pRow->Text
    return wszText
end function


' ========================================================================================
' Set the user defined row painting callback for the custom listbox control.
' ========================================================================================
sub CListBox_SetPaintCallback( byval hListControl as HWND, byval usersub as PaintCallbackSub )
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return 
    pList->PaintCallback = usersub
end sub


' ========================================================================================
' Set the user defined message callback for the custom listbox control.
' ========================================================================================
function CListBox_SetMessageCallback( byval hListControl as HWND, byval userfunc as MessageCallbackFunc ) as boolean
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return false
    pList->MessageCallback = userfunc
    return true
end function


' ========================================================================================
' Set the tooltip for the custom listbox control.
' ========================================================================================
sub CListBox_SetTooltip( byval hListControl as HWND, byval wszTooltip as DWSTRING )
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return
    ' Only set the tooltip if it has changed
    if pList->wszTooltip <> wszTooltip then
        pList->wszTooltip = wszTooltip
        ' The tooltip tool was registered against the CHILD listbox (see
        ' AfxAddTooltip in CListBox_Create), so the text must be set against
        ' that same handle -- NOT the form (pList->hWin), or TTM_GETTOOLINFOW
        ' fails to find the tool and the text is silently dropped.
        dim as HWND hList = GetDlgItem( pList->hWin, pList->idc_ListBox )
        AfxSetTooltipText( pList->hTooltip, hList, wszTooltip )
    end if
end sub


' ========================================================================================
' Set the tooltip hovertime for the custom listbox control.
' ========================================================================================
sub CListBox_SetHoverTime( byval hListControl as HWND, byval milliseconds as integer )
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return 
    pList->HoverTime = milliseconds
end sub


' ========================================================================================
' Set specified row itemData for the custom listbox control.
' ========================================================================================
' Returns TRUE on success, FALSE if the row index is invalid.
function CListBox_SetItemData( byval hListControl as HWND, byval row as integer, byval itemData as integer ) as boolean
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return false
    if pList->IsValidRow(row) then
        pList->GetRow(row)->itemData = itemData
        return true     ' success
    end if
    return false  ' invalid row
end function


' ========================================================================================
' Retrieve specified row itemData from the custom listbox control.
' ========================================================================================
function CListBox_GetItemData( byval hListControl as HWND, byval row as integer ) as integer
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return 0
    if pList->IsValidRow(row) = false then return 0
    return pList->GetRow(row)->itemData
end function


' ========================================================================================
' Retrieve the current selected row from the custom listbox control.
' ========================================================================================
function CListBox_GetCurSel( byval hListControl as HWND ) as integer
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return -1
    return ListBox_GetCurSel( GetDlgItem( pList->hWin, pList->idc_ListBox ) )
end function


' ========================================================================================
' Set the current selected row for the custom listbox control.
' ========================================================================================
function CListBox_SetCurSel( byval hListControl as HWND, byval row as integer ) as integer
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return -1
    return ListBox_SetCurSel( GetDlgItem( pList->hWin, pList->idc_ListBox ), row )
end function


' ========================================================================================
' Retrieve the row height from the custom listbox control.
' ========================================================================================
function CListBox_GetRowHeight( byval hListControl as HWND ) as integer
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return 0
    return pList->RowHeight    
end function


' ========================================================================================
' Set the row height for the custom listbox control.
' ========================================================================================
function CListBox_SetRowHeight( byval hListControl as HWND, byval height as integer ) as integer
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return -1
    pList->RowHeight = height
    ' Apply immediately -- WM_MEASUREITEM only fires at creation, so a later change
    ' would otherwise have no visible effect until the listbox is recreated.
    dim as HWND hList = GetDlgItem( pList->hWin, pList->idc_ListBox )
    if hList then
        dim as long h = height
        dim pWindow as CWindow ptr = AfxCWindowPtr( pList->hWin )
        if pWindow then h = pWindow->ScaleY( height )
        SendMessage( hList, LB_SETITEMHEIGHT, 0, cast( LPARAM, h ) )
        AfxRedrawWindow( hList )
    end if
    return height
end function


' ========================================================================================
' Retrieve the font used to paint row text.
' ========================================================================================
function CListBox_GetFont( byval hListControl as HWND ) as HFONT
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return 0
    return pList->hFont
end function


' ========================================================================================
' Set the font used to paint row text. The caller owns the HFONT and is responsible for
' destroying it; the control only stores the handle.
' ========================================================================================
function CListBox_SetFont( byval hListControl as HWND, byval hFont as HFONT ) as boolean
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return false
    pList->hFont = hFont
    dim as HWND hList = GetDlgItem( pList->hWin, pList->idc_ListBox )
    if hList then AfxRedrawWindow( hList )
    return true
end function


' ========================================================================================
' Retrieve the background color from the custom listbox control.
' ========================================================================================
function CListBox_GetBackColor( byval hListControl as HWND ) as COLORREF
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return 0
    return pList->BackColor
end function


' ========================================================================================
' Set the background color for the custom listbox control.
' ========================================================================================
function CListBox_SetBackColor( byval hListControl as HWND, byval clr as COLORREF ) as COLORREF 
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return 0
    dim as COLORREF oldClr = pList->BackColor
    pList->BackColor = clr
    return oldClr
end function


' ========================================================================================
' Retrieve the current row count in the custom listbox control.
' ========================================================================================
function CListBox_GetCount( byval hListControl as HWND ) as integer
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList = null then return 0
    return pList->GetCount()
end function


' ========================================================================================
' Refresh the custom listbox control.
' ========================================================================================
sub CListBox_Refresh( byval hListControl as HWND ) 
    dim as CLISTBOX ptr pList = GetListBoxPointer(hListControl)
    if pList then pList->Refresh()
end sub



' ========================================================================================
' Position all child windows. Called manually and/or by WM_SIZE
' ========================================================================================
function CListBox_PositionWindows( byval hwnd as HWND ) as LRESULT
    dim as CLISTBOX ptr pList = GetListBoxPointer(hwnd)
    if pList = null then return 0
 
    ' Get the entire client area
    dim as Rect rc
    GetClientRect( hwnd, @rc )

    dim as HWND hListbox = GetDlgItem( hwnd, pList->idc_ListBox )
    SetWindowPos( hListbox, 0, _
                    rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, _
                    SWP_NOZORDER )

    function = 0
end function


' ========================================================================================
' Process WM_SIZE message for window/dialog: CListBox
' ========================================================================================
function CListBox_OnSize( _
            byval hwnd as HWND, _
            byval state as UINT, _
            byval cx as long, _
            byval cy as long _
            ) as LRESULT
    if state <> SIZE_MINIMIZED then
        ' Position all of the child windows
        CListBox_PositionWindows( hwnd )
    end if
    function = 0
end function
 
        
' ========================================================================================
' Process WM_PAINT message for window/dialog: CListBox
' ========================================================================================
function CListBox_OnPaint( byval hwnd as HWND ) as LRESULT
    dim as CLISTBOX ptr pList = GetListBoxPointer(hwnd)
    if pList = null then return 0

    dim as clsDoubleBuffer b

    b.BeginDoubleBuffer(hwnd)
    b.SetBackColors( pList->BackColor, pList->BackColor )
    b.PaintClientRect()
    b.EndDoubleBuffer()

    function = 0
end function


' ========================================================================================
' Process WM_MEASUREITEM message for window/dialog: CListBox
' ========================================================================================
function CListBox_OnMeasureItem( _
            byval hwnd as HWND, _
            byval lpmis as MEASUREITEMSTRUCT ptr _
            ) as long

    dim pWindow as CWindow ptr = AfxCWindowPtr(hwnd)
    if pWindow = 0 then exit function

    dim as CLISTBOX ptr pList = GetListBoxPointer(hwnd)
    if pList then 
        ' Set the height of the list box items. 
        lpmis->itemHeight = pWindow->ScaleY( pList->RowHeight )
    end if
        
    function = 0
end function


' ========================================================================================
' Process WM_DRAWITEM message for window/dialog: CListBox
' ========================================================================================
function CListBox_OnDrawItem( _
            byval hwnd as HWND, _
            byval lpdis as const DRAWITEMSTRUCT ptr _
            ) as long

    dim pWindow as CWindow ptr = AfxCWindowPtr(hwnd)
    if pWindow = null then exit function

    if lpdis = null then exit function
    
    dim as CLISTBOX ptr pList = GetListBoxPointer(hwnd)
    if pList = null then exit function

    if ( lpdis->itemAction = ODA_DRAWENTIRE ) orelse _
        ( lpdis->itemAction = ODA_SELECT ) orelse _
        ( lpdis->itemAction = ODA_FOCUS ) then

        dim as clsDoubleBuffer b

        b.BeginDoubleBuffer(lpdis->hwndItem, lpdis->hDC, lpdis->rcItem)
        b.SetFont( pList->hFont )    ' default row font; the paint callback may override

        dim as boolean isHot = false
        if isMouseOverRECT(lpdis->hwndItem, b.rcClient) then isHot = true
        if ListBox_GetSel(lpdis->hwndItem, lpdis->itemID) then isHot = true

        ' Create our rect that works with the entire line
        dim as RECT rc
        SetRect( @rc, 0, 0, b.rcClientWidth, b.rcClientHeight )

        dim as CLISTBOX_PAINTINFO p
        with p
            .itemID     = lpdis->itemID
            .b          = @b
            .rc         = rc
            .isHot      = isHot
            .wszCaption = CListBox_GetText( hwnd, lpdis->ItemID )
        end with

        if pList->PaintCallback then pList->PaintCallback( @p )

        b.EndDoubleBuffer()

    end if   

    function = true
    
end function


' ========================================================================================
' CListBox Window procedure
' ========================================================================================
function CListBox_WndProc( _
            byval hwnd   as HWND, _
            byval uMsg   as UINT, _
            byval wParam as WPARAM, _
            byval lParam as LPARAM _
            ) as LRESULT

    select case uMsg
        HANDLE_MSG (hwnd, WM_SIZE,        CListBox_OnSize)
        HANDLE_MSG (hwnd, WM_PAINT,       CListBox_OnPaint)
        HANDLE_MSG (hwnd, WM_MEASUREITEM, CListBox_OnMeasureItem)
        HANDLE_MSG (hwnd, WM_DRAWITEM,    CListBox_OnDrawItem)
    
    case WM_NCDESTROY
        ' Delete the stored listbox data pointer
        dim as CLISTBOX ptr pList = GetListBoxPointer(hwnd)
        if pList then 
            if IsWindow(pList->hToolTip) then DestroyWindow(pList->hToolTip)
            delete(pList)
        end if

        ' delete the allocated memory for window
        dim as CWindow ptr pWindow = AfxCWindowPtr(hwnd)
        if pWindow then delete(pWindow)
        return 0

    case WM_ERASEBKGND
        return true

    end select
    
    ' for messages that we don't deal with
    function = DefWindowProc( hwnd, uMsg, wParam, lParam )

end function

' ========================================================================================
' CListBoxListBox_SubclassProc 
' ========================================================================================
function CListBoxListBox_SubclassProc ( _
            byval hWin   as HWND, _                 ' // Control window handle
            byval uMsg   as UINT, _                 ' // Type of message
            byval _wParam as WPARAM, _               ' // First message parameter
            byval _lParam as LPARAM, _               ' // Second message parameter
            byval uIdSubclass as UINT_PTR, _        ' // The subclass ID
            byval dwRefData as DWORD_PTR _          ' // Pointer to reference data
            ) as LRESULT

    dim pWindow as CWindow ptr = AfxCWindowPtr(hWin)
    dim as CLISTBOX ptr pList = GetListBoxPointer( GetParent(hWin) )

    ' Per-message scratch info passed to the user MessageCallback. A plain local
    ' (NOT static) so that multiple control instances never share it. The hover
    ' tracking index likewise lives per-instance in pList->nLastHotIdx.
    dim m as CLISTBOX_MESSAGEINFO

    select case uMsg
        ' Prepare the Callback MessageInfo structure for only the messages that we handle
        case WM_MOUSEWHEEL, WM_MOUSEMOVE, WM_MOUSEHOVER, WM_MOUSELEAVE, _
             WM_RBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK
            with m
                .hList   = hWin
                .uMsg    = uMsg
                .wParam  = _wParam
                .lParam  = _lParam
                .idx     = -1
                .isCtrl  = (GetAsyncKeyState(VK_CONTROL) and &H8000)
                .isShift = (GetAsyncKeyState(VK_SHIFT) and &H8000) 
            end with
    end select
   
    select case uMsg
        Case WM_MOUSEWHEEL
            if pList then
                ' accumulate delta until scroll one line (up +120, down -120). 
                ' 120 is the Microsoft default delta
                dim as long zDelta = GET_WHEEL_DELTA_WPARAM( _wParam )
                dim as long nTopIndex = SendMessage( hWin, LB_GETTOPINDEX, 0, 0 ) 
                pList->accumDelta = pList->accumDelta + zDelta
                if pList->accumDelta >= 120 then       ' scroll up 3 lines
                    nTopIndex = nTopIndex - 3
                    nTopIndex = max( 0, nTopIndex )
                    SendMessage( hWin, LB_SETTOPINDEX, nTopIndex, 0 )
                    pList->accumDelta = 0
                    if (pList->MessageCallback <> 0) andalso pList->MessageCallback( @m ) then return 0
                elseif pList->accumDelta <= -120 then  ' scroll down 3 lines
                    nTopIndex = nTopIndex + 3
                    SendMessage( hWin, LB_SETTOPINDEX, nTopIndex, 0 )
                    pList->accumDelta = 0
                    if (pList->MessageCallback <> 0) andalso pList->MessageCallback( @m ) then return 0
                end if
            end if


        Case WM_MOUSEMOVE
            ' Track that we are over the control in order to catch the 
            ' eventual WM_MOUSEHOVER and WM_MOUSELEAVE events
            dim tme as TrackMouseEvent
            tme.cbSize = sizeof(TrackMouseEvent)
            tme.dwFlags = TME_HOVER or TME_LEAVE
            tme.hwndTrack = hWin
            tme.dwHoverTime = 250    ' HOVER_DEFAULT is 500
            if pList then tme.dwHoverTime = pList->HoverTime
            TrackMouseEvent(@tme) 

            ' get the item rect that the mouse is over and only invalidate
            ' that instead of the entire listbox
            dim as RECT rc
            dim as long idx = Listbox_ItemFromPoint( hWin, GET_X_LPARAM(_lParam), GET_Y_LPARAM(_lParam)) 
            ' The return value contains the index of the nearest item in the LOWORD. The HIWORD is zero 
            ' if the specified point is in the client area of the list box, or one if it is outside the 
            ' client area.
            if pList andalso hiword(idx) <> 1 then
                if idx <> pList->nLastHotIdx then
                    ListBox_GetItemRect( hWin, idx, @rc )
                    InvalidateRect( hWin, @rc, true )
                    ListBox_GetItemRect( hWin, pList->nLastHotIdx, @rc )
                    InvalidateRect( hWin, @rc, true )
                    pList->nLastHotIdx = idx
                end if
            end if
            if pList andalso pList->MessageCallback then
                if pList->MessageCallback( @m ) then return 0
            end if
                        

        case WM_MOUSEHOVER
            dim as long idx = Listbox_ItemFromPoint( hWin, GET_X_LPARAM(_lParam), GET_Y_LPARAM(_lParam)) 
            ' The return value contains the index of the nearest item in the LOWORD. The HIWORD is zero 
            ' if the specified point is in the client area of the list box, or one if it is outside the 
            ' client area.
            if hiword(idx) <> 1 then
                m.idx = idx
                if pList andalso pList->MessageCallback then
                    if pList->MessageCallback( @m ) then return 0
                end if
            end if


        case WM_MOUSELEAVE
            if pList then
                pList->nLastHotIdx = -1
                pList->wszTooltip = ""
                ' hWin is the child listbox (the subclass target) -- the correct
                ' handle for the tooltip tool, matching how it was registered.
                AfxSetTooltipText( pList->hTooltip, hWin, "" )
                AfxRedrawWindow(hWin)
                if (pList->MessageCallback <> 0) andalso pList->MessageCallback( @m ) then return 0
            end if
            

        case WM_RBUTTONDOWN, _
             WM_LBUTTONUP, _
             WM_LBUTTONDBLCLK 
            dim as long idx = Listbox_ItemFromPoint( hWin, GET_X_LPARAM(_lParam), GET_Y_LPARAM(_lParam)) 
            ' The return value contains the index of the nearest item in the LOWORD. The HIWORD is zero 
            ' if the specified point is in the client area of the list box, or one if it is outside the 
            ' client area.
            if hiword(idx) <> 1 then
                m.idx = idx
                if pList andalso pList->MessageCallback then
                    if pList->MessageCallback( @m ) then return 0
                end if
            end if


        case WM_ERASEBKGND
            ' if the number of lines in the listbox maybe less than the number per page then 
            ' calculate from last item to bottom of listbox, otherwise calculate based on
            ' the mod of the lineheight to listbox height so we can color the partial line
            ' that won't be displayed at the bottom of the list.
            if pList then
                dim as RECT rc = GetListBoxEmptyClientArea( hWin )
                if rc.top < rc.bottom then
                    dim as HDC _hDC = cast(HDC, _wParam)
                    PaintRect( _hDC, @rc, pList->BackColor )
                end if
                return true
            end if    
            
        Case WM_DESTROY
            ' REQUIRED: Remove control subclassing
            RemoveWindowSubclass( hWin, @CListBoxListBox_SubclassProc, uIdSubclass )
    end select
    
    ' For messages that we don't deal with
    function = DefSubclassProc( hWin, uMsg, _wParam, _lParam )

end function


' ========================================================================================
' CListBox_Create
' ========================================================================================
function CListBox_Create( byval hWndParent as HWND, byval CtrlID as integer ) as HWND

    dim pWindow as CWindow ptr = New CWindow

    dim as HWND hForm = _
            pWindow->Create( hWndParent, "", @CListBox_WndProc, _
            0, 0, 0, 0, WS_CHILD or WS_CLIPSIBLINGS or WS_CLIPCHILDREN _
            )
    ' Disable background erasing by only assigning the one style
    pWindow->ClassStyle = CS_DBLCLKS

    ' Define the TYPE that holds all the information about this custom listbox.
    dim as CLISTBOX ptr pList = new CLISTBOX
    pList->hWin = hForm
    pList->idc_ListBox = CtrlID
    
    ' Store the new pointer into the user data area of the listbox window
    pWindow->UserData(0) = cast(LONG_PTR, pList)
    

    dim as long ExtendSel = iif( pList->ExtendSel, LBS_EXTENDEDSEL, 0 )
    dim as long MultipleSel = iif( pList->MultipleSel, LBS_MULTIPLESEL, 0 )
    
    dim as HWND hList = _ 
        pWindow->AddControl( "LISTBOX", , pList->idc_ListBox, "", 0, 0, 0, 0, _
        WS_CHILD or WS_CLIPSIBLINGS or WS_CLIPCHILDREN or WS_TABSTOP or _
        LBS_NOINTEGRALHEIGHT or ExtendSel or MultipleSel or _
        LBS_OWNERDRAWFIXED or LBS_NODATA or LBS_NOTIFY, _
        WS_EX_LEFT or WS_EX_RIGHTSCROLLBAR)
        pWindow->SetControlSubclass( hList, @CListBoxListBox_SubclassProc, pList->idc_ListBox, @pWindow )
    pList->hTooltip = AfxAddTooltip( hList, "", false, false )

    function = hForm
    
end function

