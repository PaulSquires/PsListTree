#include once "frmMain.bi"
#include once "clsDoubleBuffer.bi"


' ========================================================================================
' Process WM_PAINT message for window/dialog: frmMain
' ========================================================================================
function frmMain_OnPaint( byval hwnd as HWND ) as LRESULT
    dim as clsDoubleBuffer b

    b.BeginDoubleBuffer(hwnd)
    b.SetBackColor( theme.BackColorPanel )
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
    p->b->SetForeColor( foreclr )
    p->b->SetBackColor( backclr )

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
        p->b->SetBackColor( theme.FocusAccent )
        p->b->PaintRect( @rcBar )
    end if

end sub

' ========================================================================================
' Header_PaintCallback -- demo painter for the CColumnHeader band (per-column)
' ========================================================================================
sub Header_PaintCallback( byval p as CCOLUMNHEADER_PAINTINFO ptr )

    ' State priority for the cell background/foreground: pressed > hot > normal
    dim as COLORREF backclr, foreclr
    if p->isPressed then
        backclr = theme.BackColorSelect
        foreclr = theme.ForeColorSelect
    elseif p->isHot then
        backclr = theme.BackColorHot
        foreclr = theme.ForeColorHot
    else
        backclr = theme.BackColor
        foreclr = theme.ForeColor
    end if
    p->b->SetColors( foreclr, backclr )
    p->b->PaintRect( @p->rc )

    ' Caption, inset by the control's padding (the space the layout accounts for)
    dim as RECT rcText = p->rc
    dim as integer pad = CColumnHeader_GetPadding( p->hHeader )
    rcText.left += pad
    rcText.right -= pad
    p->b->SetFont( ghFont(GUIFONTBOLD_10) )
    p->b->PaintText( p->wszCaption, @rcText, DT_LEFT )

    ' Divider tick at the right edge, accented (and widened) while its gutter is
    ' hovered or the divider is being dragged. The fill column has no divider.
    if p->isFill = false then
        dim as RECT rcDiv = p->rc
        rcDiv.left = rcDiv.right - 1
        dim as COLORREF divclr = theme.ForeColorScrollBar
        if p->isResizeHot orelse p->isResizing then
            divclr = theme.FocusAccent
            rcDiv.left -= 1
        end if
        p->b->SetBackColor( divclr )
        p->b->PaintRect( @rcDiv )
    end if

end sub


' ========================================================================================
' Header interaction callbacks (demo) -- numeric traces so the gestures are observable
' ========================================================================================
sub Header_WidthChangedCallback( byval hHeader as HWND, byval idx as integer, byval nWidth as integer, byval bLive as boolean )
    ' plain string, not DWSTRING: print interleaves wide chars
    dim as string s = "header resize col=" & str(idx) & " w=" & str(nWidth) & " live=" & str(bLive)
    print s
end sub

sub Header_ClickCallback( byval hHeader as HWND, byval idx as integer )
    dim as string s = "header click col=" & str(idx)
    print s
end sub

function Header_AutoSizeCallback( byval hHeader as HWND, byval idx as integer ) as integer
    ' Demo best-fit: measure the column CAPTION with the header font + 2*padding. (A
    ' real host measures its widest cell; the standalone header has no cell data.)
    dim as integer bestW = 0
    dim as DWSTRING wszText = CColumnHeader_GetColumnText( hHeader, idx )
    dim as HDC hDC = GetDC( hHeader )
    if hDC then
        dim as HFONT hOldFont = SelectObject( hDC, ghFont(GUIFONTBOLD_10) )
        dim as SIZE sz
        if GetTextExtentPoint32W( hDC, wszText.vptr, len(wszText), @sz ) then bestW = sz.cx
        SelectObject( hDC, hOldFont )
        ReleaseDC( hHeader, hDC )
    end if
    if bestW > 0 then bestW += CColumnHeader_GetPadding( hHeader ) * 2
    dim as string s = "header autosize col=" & str(idx) & " -> " & str(bestW)
    print s
    return bestW
end function


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
            ' Report the resulting selection so multi-select is observable while testing.
            dim as HWND hCtl = GetParent(m->hList)
            ' plain string, not DWSTRING: print interleaves wide chars
            dim as string sKind = "item"
            if CListBox_IsHeader( hCtl, m->idx ) then sKind = "HEADER"
            print "click row" & str(m->idx) & " (" & sKind & ")  focus=" & _
                  str(CListBox_GetCurSel(hCtl)) & "  selCount=" & str(CListBox_GetSelCount(hCtl))
            ' Header collapse/expand is handled inside the control on the mouse-down,
            ' and it preserves the current selection. Return FALSE: the listbox needs
            ' its default WM_LBUTTONUP handling to release the mouse capture it took
            ' on the button-down.
            return false
    
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
            100, 100, 800, 400 _
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
    ' collapsed, PLUS the embedded column header band. Tests: per-instance hover
    ' (must not affect A), empty client-area painting below the last row, collapse
    ' hiding a group's items, and the header/columns integration. ----
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
    ' B uses the OTHER selection mode, so the harness covers both.
    CListBox_SetMultiSelect( HWND_FRMEXPLORER2, true )     ' checklist style: click toggles
    CListBox_BeginUpdate( HWND_FRMEXPLORER2 )
    CListBox_AddHeader( HWND_FRMEXPLORER2, "Files" )              ' model row 0
    for i as integer = 0 to 40
        CListBox_AddString( HWND_FRMEXPLORER2, "file" & i & ".bas", i )   ' rows 1..5
    next
    CListBox_AddHeader( HWND_FRMEXPLORER2, "Folders" )           ' model row 6
    for i as integer = 0 to 4
        CListBox_AddString( HWND_FRMEXPLORER2, "folder" & i, i )          ' rows 7..11
    next
    CListBox_EndUpdate( HWND_FRMEXPLORER2 )
    CListBox_CollapseRow( HWND_FRMEXPLORER2, 6 )   ' start the "Folders" group collapsed

    ' ---- Embedded column header on instance B (phase 3). Columns are defined through
    ' the CListBox wrappers; the header child is the store. Host callbacks go through
    ' the wrappers too -- the resize slot on the child itself belongs to the control. ----
    CListBox_AddColumn( HWND_FRMEXPLORER2, "Name", 160 )
    CListBox_AddColumn( HWND_FRMEXPLORER2, "Size", 70 )
    CListBox_AddColumn( HWND_FRMEXPLORER2, "Type" )    ' fill column (default: last)
    CListBox_SetHeaderPaintCallback( HWND_FRMEXPLORER2, @Header_PaintCallback )
    CListBox_SetHeaderBackColor( HWND_FRMEXPLORER2, theme.BackColorScrollBar )
    CListBox_SetHeaderFont( HWND_FRMEXPLORER2, ghFont(GUIFONTBOLD_10) )
    CListBox_SetColumnResizeCallback( HWND_FRMEXPLORER2, @Header_WidthChangedCallback )
    CListBox_SetColumnClickCallback( HWND_FRMEXPLORER2, @Header_ClickCallback )
    CListBox_SetColumnAutoSizeCallback( HWND_FRMEXPLORER2, @Header_AutoSizeCallback )
    CListBox_ShowHeader( HWND_FRMEXPLORER2, true )
    HWND_COLHDR = CListBox_GetHeader( HWND_FRMEXPLORER2 )

    ' ---- Geometry gates (numeric, asserted, never eyeballed) ----
    ' 1) Column rects: contiguous from 0, fill column's right edge = header client width.
    scope
        dim as RECT rcH, rcC
        GetClientRect( HWND_COLHDR, @rcC )
        dim as string sTrace = "Header rects:"
        dim as integer prevRight = 0
        dim as boolean okContig = true
        for i as integer = 0 to CColumnHeader_GetCount( HWND_COLHDR ) - 1
            CColumnHeader_GetColumnRect( HWND_COLHDR, i, rcH )
            sTrace &= " c" & str(i) & "=[" & str(rcH.left) & ".." & str(rcH.right) & "]"
            if i = 0 then
                if rcH.left <> 0 then okContig = false
            else
                if rcH.left <> prevRight then okContig = false
            end if
            prevRight = rcH.right
        next
        print sTrace
        dim as boolean okFill = (prevRight = rcC.right)
        dim as string sVerdict = "Header geometry: "
        if okContig andalso okFill then sVerdict &= "PASS" else sVerdict &= "FAIL"
        sVerdict &= "  (contiguous=" & str(okContig) & " lastRight=" & str(prevRight) & _
                    " clientW=" & str(rcC.right) & ")"
        print sVerdict
    end scope
    ' 2) Embedding layout: header spans the full container width at the top; the listbox
    '    starts below it; hiding the header gives the strip back.
    scope
        dim as HWND hList2 = GetDlgItem( HWND_FRMEXPLORER2, IDC_FRMMAIN_LISTBOX )
        dim as RECT rcHdr, rcList, rcCont
        GetWindowRect( HWND_COLHDR, @rcHdr )
        GetWindowRect( hList2, @rcList )
        GetWindowRect( HWND_FRMEXPLORER2, @rcCont )
        dim as integer hdrTop  = rcHdr.top - rcCont.top
        dim as integer hdrWide = rcHdr.right - rcHdr.left
        dim as integer contW   = rcCont.right - rcCont.left
        dim as integer hdrHgt  = rcHdr.bottom - rcHdr.top
        dim as integer listTop = rcList.top - rcCont.top
        dim as string sTrace = "Embed: hdrTop=" & str(hdrTop) & " hdrW=" & str(hdrWide) & _
                               "/" & str(contW) & " hdrH=" & str(hdrHgt) & " listTop=" & str(listTop)
        print sTrace
        dim as boolean okShown = (hdrTop = 0) andalso (hdrWide = contW) andalso (listTop = hdrHgt)
        CListBox_ShowHeader( HWND_FRMEXPLORER2, false )
        GetWindowRect( hList2, @rcList )
        dim as integer listTopHidden = rcList.top - rcCont.top
        CListBox_ShowHeader( HWND_FRMEXPLORER2, true )
        dim as boolean okHidden = (listTopHidden = 0)
        dim as string sVerdict = "Embed layout: "
        if okShown andalso okHidden then sVerdict &= "PASS" else sVerdict &= "FAIL"
        sVerdict &= "  (listTop hidden=" & str(listTopHidden) & ")"
        print sVerdict
    end scope

    print "CListBox / CVScrollBar test harness"
    print "-----------------------------------"
    print "Instance A (left) : "; CListBox_GetCount( HWND_FRMEXPLORER ); " model rows, "; _
          CListBox_GetVisibleCount( HWND_FRMEXPLORER ); " visible   [extended select: Shift/Ctrl]"
    print "Instance B (right): "; CListBox_GetCount( HWND_FRMEXPLORER2 ); " model rows, "; _
          CListBox_GetVisibleCount( HWND_FRMEXPLORER2 ); " visible   [multi select: click toggles]"
    print
    print "Manual checks (things a script cannot drive):"
    print "  Selection  A: click / Ctrl+click toggles / Shift+click ranges"
    print "             B: every click toggles one row"
    print "  Headers    click a header -> folds, and your item selection SURVIVES"
    print "  Keyboard   arrows, PageUp/Dn, Home/End; Left/Right collapse/expand;"
    print "             Space toggles (the only way to select a header)"
    print "  Hover      row highlights; whip the mouse out -> must clear every time"
    print "  Tooltips   pause on a row; text follows the row as you move"
    print "  Scrollbar  drag thumb (works on the FIRST press); hold the track to"
    print "             auto-repeat; thumb highlights on hover"
    print "  Auto-hide  collapse a group in A until the rows fit -> bar disappears"
    print "             and the list reclaims the width; expand -> it returns"
    print "  2 instances nothing you do in one may disturb the other"
    print "  Header     cursor flips to <-> over a divider (not over the fill column's"
    print "             edge); drag resizes LIVE (traces w=... live=true, final"
    print "             live=false); ESC mid-drag snaps back; dbl-click a divider"
    print "             traces autosize; click a column body traces click (press,"
    print "             slide off, release -> nothing); tooltip shows column text"
    print

    
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

