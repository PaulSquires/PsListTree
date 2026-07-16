#include once "frmMain.bi"
#include once "clsDoubleBuffer.bi"


' ========================================================================================
' Process WM_PAINT message for window/dialog: frmMain
' ========================================================================================
function frmMain_OnPaint( byval hwnd as HWND ) as LRESULT
    dim as clsDoubleBuffer b

    b.BeginDoubleBuffer(hwnd)
    b.SetBackColors( theme.BackColorPanel, theme.BackColorPanel )
    b.PaintClientRect()

    b.EndDoubleBuffer()
    
    function = 0
end function


' ========================================================================================
' Process WM_CREATE message for window/dialog: frmMain
' ========================================================================================
function frmMain_OnCreate( _
            byval hwnd as HWND, _
            byval lpCreateStructPtr as LPCREATESTRUCT _
            ) as boolean

    ' Enable drag and drop files
    DragAcceptFiles hwnd, CTRUE
    
    '  Message cracker macro expects a true to be returned for a successful
    '  OnCreate handler even though returning -1 from a standard WM_CREATE
    '  call would stop creating the window. This is just one of those Windows
    '  inconsistencies.
    return true
end Function


' ========================================================================================
' Process WM_CLOSE message for window/dialog: frmMain
' ========================================================================================
function frmMain_OnClose( byval hwnd as HWND ) as LRESULT

    DestroyWindow(hwnd)
    
    function = 0
end Function


' ========================================================================================
' Process WM_DESTROY message for window/dialog: frmMain
' ========================================================================================
function frmMain_OnDestroy( byval hwnd as HWND ) as LRESULT

    ' Disable drag and drop files
    DragAcceptFiles hwnd, false
    
    PostQuitMessage(0)
    
    function = 0
end Function


' ========================================================================================
' frmMain Window procedure
' ========================================================================================
function frmMain_WndProc( _
            byval hwnd   as HWND, _
            byval uMsg   as UINT, _
            byval wParam as WPARAM, _
            byval lParam as LPARAM _
            ) as LRESULT

    select case uMsg
        HANDLE_MSG (hwnd, WM_CREATE,        frmMain_OnCreate)
        HANDLE_MSG (hwnd, WM_PAINT,         frmMain_OnPaint)
        HANDLE_MSG (hwnd, WM_CLOSE,         frmMain_OnClose)
        HANDLE_MSG (hwnd, WM_DESTROY,       frmMain_OnDestroy)

        case WM_NCDESTROY
            ' delete the allocated memory for window
            dim as CWindow ptr pWindow = AfxCWindowPtr(hwnd)
            if pWindow then delete(pWindow)
            HWND_FRMMAIN = 0
            return 0

        case WM_ERASEBKGND
            return true      ' prevents painting the background
            
        case WM_SYSCOMMAND
            if (wParam and &HFFF0) = SC_CLOSE then
                SendMessage( hwnd, WM_CLOSE, wParam, lParam )
                exit function
            end if
            
    end select 

    ' for messages that we don't deal with
    function = DefWindowProc(HWnd, uMsg, wParam, lParam)

end Function




' ========================================================================================
' CListBox_PaintCallback
' ========================================================================================
sub CListBox_PaintCallback( byval p as CLISTBOX_PAINTINFO ptr )

    ' State priority for the row background/foreground: selected > hot > normal
    dim as COLORREF backclr, foreclr
    if p->isSelected then
        backclr = theme.BackColorSelect
        foreclr = theme.ForeColorSelect
    elseif p->isHot then
        backclr = theme.BackColorHot
        foreclr = theme.ForeColorHot
    else
        backclr = theme.BackColor
        foreclr = theme.ForeColor
    end if
    p->b->SetForeColors(foreclr, foreclr)
    p->b->SetBackColors(backclr, backclr)

    ' Paint the entire row background
    p->b->PaintRect( @p->rc )

    if p->isHeader then
        ' Header row: bold, with a +/- collapse indicator
        dim as DWSTRING wszPrefix
        if p->isCollapsed then wszPrefix = "+ " else wszPrefix = "- "
        p->b->SetFont( ghFont(GUIFONTBOLD_10) )
        p->b->PaintText( wszPrefix & p->wszCaption, @p->rc, DT_LEFT )
    else
        ' Item row: normal font, indented under its header
        dim as RECT rcText = p->rc
        rcText.left += 16
        p->b->SetFont( ghFont(GUIFONT_10) )
        p->b->PaintText( p->wszCaption, @rcText, DT_LEFT )
    end if

    ' Focus (caret) row: a thin accent bar on the left edge
    if p->isFocused then
        dim as RECT rcBar = p->rc
        rcBar.right = rcBar.left + 3
        p->b->SetBackColors( theme.FocusAccent, theme.FocusAccent )
        p->b->PaintRect( @rcBar )
    end if

end sub

' ========================================================================================
' CListBox_MessageCallback
' ========================================================================================
function CListBox_MessageCallback( byval m as CLISTBOX_MESSAGEINFO ptr ) as boolean

    select case m->uMsg
             
        case WM_MOUSEWHEEL
            print "WM_MOUSEWHEEL"
            return true

        case WM_MOUSEHOVER
            print "WM_MOUSEHOVER on idx: "; m->idx
            ' Tooltips are now on-demand via the tooltip callback; nothing to push.
            return true
            
        case WM_LBUTTONUP
            print "WM_LBUTTONUP on idx: "; m->idx
            ' Header collapse/expand is now handled inside the control on the
            ' mouse-down, and it preserves the current selection.
            return true
    
    end select

    ' Return false to allow to fall to the default windows handler
    return false
end function    


' ========================================================================================
' CListBox_TooltipCallback  -- per-row tooltip text, resolved on demand
' ========================================================================================
function CListBox_TooltipCallback( byval hCtl as HWND, byval row as integer ) as DWSTRING
    if CListBox_IsHeader( hCtl, row ) then
        return "Group header: " & CListBox_GetText( hCtl, row )
    end if
    return "Row " & row & " -> " & CListBox_GetText( hCtl, row )
end function


' ========================================================================================
' frmMain_Show
' ========================================================================================
function frmMain_Show( byval hWndParent as HWND ) as LRESULT

    '  Create the main window and child controls
    dim pWindow as CWindow ptr = new CWindow(APPCLASSNAME)
    
    ghFont(GUIFONT_9)      = pWindow->CreateFont(GUIFONT, 9, FW_NORMAL) 
    ghFont(GUIFONT_10)     = pWindow->CreateFont(GUIFONT, 10, FW_NORMAL)
    ghFont(GUIFONTBOLD_10) = pWindow->CreateFont(GUIFONT, 10, FW_HEAVY)
    ghFont(ITALICFONT_10)  = pWindow->CreateFont(GUIFIXEDFONT, 10, FW_NORMAL, CTRUE)
    ghFont(INFOFONT_11)    = pWindow->CreateFont(GUIFIXEDFONT, 11, FW_NORMAL)
    ghFont(SYMBOLFONT_9)   = pWindow->CreateFont(SYMBOLFONT, 9, FW_NORMAL)
    ghFont(SYMBOLFONT_10)  = pWindow->CreateFont(SYMBOLFONT, 10, FW_NORMAL)
    ghFont(SYMBOLFONT_12)  = pWindow->CreateFont(SYMBOLFONT, 12, FW_NORMAL)

    HWND_FRMMAIN = pWindow->Create( _
            null, APPNAME, @frmMain_WndProc, _
            100, 100, 800, 600 _
            )

    dim as RECT rc: GetClientRect( HWND_FRMMAIN, @rc )
    dim as long halfW = rc.right \ 2

    ' ---- Instance A (left half): many rows -- exercises scrolling ----
    HWND_FRMEXPLORER = CListBox_Create( HWND_FRMMAIN, IDC_FRMMAIN_LISTBOX )
    SetWindowPos( HWND_FRMEXPLORER, 0, _
        rc.left, rc.top, halfW, rc.bottom, _
        SWP_NOZORDER or SWP_SHOWWINDOW )
    CListBox_SetPaintCallback( HWND_FRMEXPLORER, @CListBox_PaintCallback )
    CListBox_SetMessageCallback( HWND_FRMEXPLORER, @CListBox_MessageCallback )
    CListBox_SetBackColor( HWND_FRMEXPLORER, theme.BackColor )
    CListBox_SetFont( HWND_FRMEXPLORER, ghFont(GUIFONT_10) )
    CListBox_SetTooltipCallback( HWND_FRMEXPLORER, @CListBox_TooltipCallback )
    CListBox_SetExtendedSelect( HWND_FRMEXPLORER, true )   ' Shift/Ctrl range selection
    ' The scrollbar is created/positioned/auto-hidden by the control; we only theme it.
    ' (CListBox_SetScrollBarPaintCallback is available for full custom drawing.)
    CListBox_SetScrollBarColors( HWND_FRMEXPLORER, theme.BackColorScrollBar, _
                                 theme.ForeColorScrollBar, theme.ForeColorScrollBarHot )
    ' Two groups; bulk add is batched so the map rebuilds once, not per row.
    CListBox_BeginUpdate( HWND_FRMEXPLORER )
    CListBox_AddHeader( HWND_FRMEXPLORER, "Group Alpha" )
    for i as integer = 0 to 20
        CListBox_AddString( HWND_FRMEXPLORER, "Alpha item " & i, 1000 + i )
    next
    CListBox_AddHeader( HWND_FRMEXPLORER, "Group Beta" )
    for i as integer = 0 to 20
        CListBox_AddString( HWND_FRMEXPLORER, "Beta item " & i, 2000 + i )
    next
    CListBox_EndUpdate( HWND_FRMEXPLORER )
    CListBox_SetCurSel( HWND_FRMEXPLORER, 1 )

    ' ---- Instance B (right half): a couple of small groups, one starting
    ' collapsed. Tests: per-instance hover (must not affect A), empty client-area
    ' painting below the last row, and collapse hiding a group's items. ----
    HWND_FRMEXPLORER2 = CListBox_Create( HWND_FRMMAIN, IDC_FRMMAIN_LISTBOX )
    SetWindowPos( HWND_FRMEXPLORER2, 0, _
        halfW, rc.top, rc.right - halfW, rc.bottom, _
        SWP_NOZORDER or SWP_SHOWWINDOW )
    CListBox_SetPaintCallback( HWND_FRMEXPLORER2, @CListBox_PaintCallback )
    CListBox_SetMessageCallback( HWND_FRMEXPLORER2, @CListBox_MessageCallback )
    CListBox_SetBackColor( HWND_FRMEXPLORER2, theme.BackColor )
    CListBox_SetFont( HWND_FRMEXPLORER2, ghFont(GUIFONT_10) )
    CListBox_SetTooltipCallback( HWND_FRMEXPLORER2, @CListBox_TooltipCallback )
    CListBox_SetScrollBarColors( HWND_FRMEXPLORER2, theme.BackColorScrollBar, _
                                 theme.ForeColorScrollBar, theme.ForeColorScrollBarHot )
    CListBox_BeginUpdate( HWND_FRMEXPLORER2 )
    CListBox_AddHeader( HWND_FRMEXPLORER2, "Files" )              ' model row 0
    for i as integer = 0 to 4
        CListBox_AddString( HWND_FRMEXPLORER2, "file" & i & ".bas", i )   ' rows 1..5
    next
    CListBox_AddHeader( HWND_FRMEXPLORER2, "Folders" )           ' model row 6
    for i as integer = 0 to 4
        CListBox_AddString( HWND_FRMEXPLORER2, "folder" & i, i )          ' rows 7..11
    next
    CListBox_EndUpdate( HWND_FRMEXPLORER2 )
    CListBox_CollapseRow( HWND_FRMEXPLORER2, 6 )   ' start the "Folders" group collapsed

    print "Instance A: "; CListBox_GetCount( HWND_FRMEXPLORER ); " model rows, "; _
          CListBox_GetVisibleCount( HWND_FRMEXPLORER ); " visible"
    print "Instance B: "; CListBox_GetCount( HWND_FRMEXPLORER2 ); " model rows, "; _
          CListBox_GetVisibleCount( HWND_FRMEXPLORER2 ); " visible"

    
    UpdateWindow( pWindow->hWindow )
    ShowWindow( pWindow->hWindow, SW_SHOW )

    ' Process windows events
    dim uMsg as MSG

    ' Message loop
    do while GetMessage(@uMsg, null, 0, 0)
        if uMsg.message = WM_QUIT then exit do

        ' Processes accelerator keys for menu commands
        if (pWindow->AccelHandle = 0) orelse (TranslateAccelerator(pWindow->hWindow, pWindow->AccelHandle, @uMsg) = 0) then

            if IsDialogMessage(pWindow->hWindow, @uMsg) = 0 then
                TranslateMessage @uMsg    ' Translates virtual-key messages into character messages.
                DispatchMessage @uMsg     ' Dispatches a message to a window procedure.
            end if

        end if  ' accelerators
    loop  ' message loop
    
    function = uMsg.wParam


    for i as long = 0 to MAXFONTS
        if ghFont(i) then DeleteObject(ghFont(i))
    next

end function

