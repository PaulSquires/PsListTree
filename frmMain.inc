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




' One-shot numeric trace of the first multi-column row's cell rects (phase 4 gate:
' compare against the "Header rects:" line -- they must be identical spans).
dim shared gCellTraceDone as boolean = false

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
        ' Header row: bold, with a +/- collapse indicator. Group headers always span
        ' the full row width -- the control never hands them a cell array.
        dim as DWSTRING wszPrefix
        if p->isCollapsed then wszPrefix = "+ " else wszPrefix = "- "
        p->b->SetFont( ghFont(GUIFONTBOLD_10) )
        p->b->PaintText( wszPrefix & p->wszCaption, @p->rc, DT_LEFT )
    elseif p->columnCount > 0 then
        ' Multi-column row: the background/selection above already spans the full row
        ' (listview-style); each cell now draws inside its own rect, ellipsized, inset
        ' by the header's padding so cells line up with their captions.
        if gCellTraceDone = false then
            gCellTraceDone = true
            dim as string s = "Row cells:   "
            for c as integer = 0 to p->columnCount - 1
                s &= " c" & str(c) & "=[" & str(p->cells[c].rc.left) & ".." & str(p->cells[c].rc.right) & "]"
            next
            print s
        end if
        dim as integer pad = CColumnHeader_GetPadding( HWND_COLHDR )
        p->b->SetFont( ghFont(GUIFONT_10) )
        for c as integer = 0 to p->columnCount - 1
            dim as RECT rcCell = p->cells[c].rc
            rcCell.left += pad
            rcCell.right -= pad
            p->b->PaintText( p->cells[c].wszText, @rcCell, DT_LEFT or DT_END_ELLIPSIS )
        next
    else
        ' Single-column item row: normal font, indented under its header
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

' Host-side sort state for instance B (the CONTROL never sorts; the header's click
' callback is just the hook). -1 = unsorted.
dim shared gSortCol as integer = -1
dim shared gSortAsc as boolean = true

' Counts every ColumnResize notification -- lets the self-test assert that programmatic
' SetColumnWidth stays SILENT (family rule) while user drags notify.
dim shared gResizeNotifyCount as integer = 0


' ========================================================================================
' Compare two snapshot rows by the current sort column: numeric for Size (col 1),
' case-insensitive text otherwise. Returns <0 / 0 / >0.
' ========================================================================================
function Demo_CompareRows( byval a0 as string, byval a1 as string, byval a2 as string, _
                           byval b0 as string, byval b1 as string, byval b2 as string ) as integer
    if gSortCol = 1 then
        dim as integer na = val(a1)
        dim as integer nb = val(b1)
        if na < nb then return -1
        if na > nb then return 1
        return 0
    end if
    dim as string ka, kb
    if gSortCol = 2 then
        ka = lcase(a2) : kb = lcase(b2)
    else
        ka = lcase(a0) : kb = lcase(b0)
    end if
    if ka < kb then return -1
    if ka > kb then return 1
    return 0
end function


' ========================================================================================
' Host-side sort of instance B by gSortCol/gSortAsc. The demo's backing data lives in
' the control's cells, so snapshot it, sort each group's item run (headers stay put),
' and repopulate under one BeginUpdate batch.
' ========================================================================================
sub Demo_SortInstanceB()
    dim as HWND h = HWND_FRMEXPLORER2
    dim as integer n = CListBox_GetCount( h )
    if n <= 0 then return

    redim as boolean isHdr( 0 to n - 1 )
    redim as boolean isCol( 0 to n - 1 )
    redim as string  c0( 0 to n - 1 ), c1( 0 to n - 1 ), c2( 0 to n - 1 )
    redim as integer dat( 0 to n - 1 )
    for i as integer = 0 to n - 1
        isHdr(i) = CListBox_IsHeader( h, i )
        isCol(i) = CListBox_IsCollapsed( h, i )
        c0(i) = CListBox_GetCellText( h, i, 0 )
        c1(i) = CListBox_GetCellText( h, i, 1 )
        c2(i) = CListBox_GetCellText( h, i, 2 )
        dat(i) = CListBox_GetItemData( h, i )
    next

    ' Insertion-sort each header's item run in place.
    dim as integer i = 0
    while i < n
        if isHdr(i) = false then
            i += 1
            continue while
        end if
        dim as integer first = i + 1
        dim as integer last = first
        while last < n
            if isHdr(last) then exit while
            last += 1
        wend
        for a as integer = first + 1 to last - 1
            dim as string k0 = c0(a), k1 = c1(a), k2 = c2(a)
            dim as integer kd = dat(a)
            dim as integer b2 = a - 1
            while b2 >= first
                dim as integer cmp = Demo_CompareRows( c0(b2), c1(b2), c2(b2), k0, k1, k2 )
                dim as boolean moveIt
                if gSortAsc then moveIt = (cmp > 0) else moveIt = (cmp < 0)
                if moveIt = false then exit while
                c0(b2 + 1) = c0(b2) : c1(b2 + 1) = c1(b2) : c2(b2 + 1) = c2(b2)
                dat(b2 + 1) = dat(b2)
                b2 -= 1
            wend
            c0(b2 + 1) = k0 : c1(b2 + 1) = k1 : c2(b2 + 1) = k2
            dat(b2 + 1) = kd
        next
        i = last
    wend

    CListBox_BeginUpdate( h )
    CListBox_Clear( h )
    for i as integer = 0 to n - 1
        dim as integer r
        if isHdr(i) then
            r = CListBox_AddHeader( h, c0(i), dat(i) )
            if isCol(i) then CListBox_CollapseRow( h, r )
        else
            r = CListBox_AddString( h, c0(i), dat(i) )
            if len(c1(i)) > 0 then CListBox_SetCellText( h, r, 1, c1(i) )
            if len(c2(i)) > 0 then CListBox_SetCellText( h, r, 2, c2(i) )
        end if
    next
    CListBox_EndUpdate( h )
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

    ' Sort glyph on the current sort column (host state -- the control knows nothing
    ' about sorting; a click just re-sorted the host's data).
    if p->itemID = gSortCol then
        dim as RECT rcGlyph = p->rc
        rcGlyph.left = rcGlyph.right - pad - 12
        dim as DWSTRING wszGlyph
        if gSortAsc then wszGlyph = wchr(&hE70E) else wszGlyph = wchr(&hE70D)
        p->b->SetFont( ghFont(SYMBOLFONT_9) )
        p->b->PaintChar( wszGlyph, @rcGlyph, foreclr )
    end if

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
    gResizeNotifyCount += 1
    ' plain string, not DWSTRING: print interleaves wide chars
    dim as string s = "header resize col=" & str(idx) & " w=" & str(nWidth) & " live=" & str(bLive)
    print s
end sub

sub Header_ClickCallback( byval hHeader as HWND, byval idx as integer )
    ' Click-to-sort: toggle direction on the same column, ascending on a new one.
    if idx = gSortCol then
        gSortAsc = not gSortAsc
    else
        gSortCol = idx
        gSortAsc = true
    end if
    dim as string s = "header click col=" & str(idx) & " -> sort "
    if gSortAsc then s &= "asc" else s &= "desc"
    print s
    Demo_SortInstanceB()
    CColumnHeader_Refresh( hHeader )   ' repaint the sort glyph
end sub

function Header_AutoSizeCallback( byval hHeader as HWND, byval idx as integer ) as integer
    ' Demo best-fit: measure the column CAPTION with the header font + 2*padding. (A
    ' real host measures its widest cell; the standalone header has no cell data.)
    dim as integer bestW = 0
    dim as DWSTRING wszText = CColumnHeader_GetColumnText( hHeader, idx )
    dim as HDC hDC = GetDC( hHeader )
    dim as HFONT hOldFont = SelectObject( hDC, ghFont(GUIFONTBOLD_10) )
    dim as SIZE sz
    if GetTextExtentPoint32W( hDC, wszText.vptr, len(wszText), @sz ) then bestW = sz.cx
    SelectObject( hDC, hOldFont )
    ReleaseDC( hHeader, hDC )
    if bestW > 0 then bestW += CColumnHeader_GetPadding( hHeader ) * 2
    dim as string s = "header autosize col=" & str(idx) & " -> " & str(bestW)
    print s
    return bestW
end function


' ========================================================================================
' Geometry/contract self-test (run with CLISTBOX_SELFTEST=1). Asserts the column rect
' math, divider hit zones, and the painted cell contract -- captured from the REAL
' paint path, never re-derived (CHScrollBar's self-test pattern).
' ========================================================================================
dim shared gFailCount as integer = 0
dim shared gSelfTestCells(any) as CLISTBOX_CELLINFO
dim shared gSelfTestCellCount as integer = -1     ' -1 = capture never ran
' The row's OWN rect, captured alongside its cells. Rows are painted in the surface's
' client coordinates now, so "where is this row" is no longer a constant the test can
' assume -- it has to be read from the same PAINTINFO the cells came from.
dim shared gSelfTestRowRc as RECT
dim shared gSelfTestRowIsHeader as boolean = false
dim shared gSelfTestWantRow as integer = -2       ' model row to capture; -2 = any row

' SelChange recorder: the callback fires only for USER-driven selection moves, so the
' self-test can assert both that it fires (keyboard) and that it does not (setters).
dim shared gSelChangeCount as integer = 0
dim shared gSelChangeRow as integer = -99

sub SelfTest_SelChange( byval hCtl as HWND, byval row as integer )
    gSelChangeCount += 1
    gSelChangeRow = row
end sub

sub SelfTest_Check( byval sName as string, byval bOK as boolean, byval sDetail as string )
    dim as string s
    if bOK then
        s = "PASS  "
    else
        s = "FAIL  "
        gFailCount += 1
    end if
    s &= sName & "   [" & sDetail & "]"
    print s
end sub

' Swapped-in row paint callback: copies the requested model row's column info out of
' the live PAINTINFO (the pointer is only valid during the call, so deep-copy).
sub SelfTest_RowCapturePaint( byval p as CLISTBOX_PAINTINFO ptr )
    if (gSelfTestWantRow <> -2) andalso (p->itemID <> gSelfTestWantRow) then exit sub
    gSelfTestCellCount   = p->columnCount
    gSelfTestRowIsHeader = p->isHeader
    gSelfTestRowRc       = p->rc
    if p->columnCount > 0 then
        redim gSelfTestCells( 0 to p->columnCount - 1 )
        for c as integer = 0 to p->columnCount - 1
            gSelfTestCells(c) = p->cells[c]
        next
    end if
end sub

' Force a synchronous repaint of an instance's listbox child; TRUE if the capture
' callback saw the requested row (it must be scrolled into view).
function SelfTest_GrabRow( byval hCtl as HWND, byval modelRow as integer ) as boolean
    gSelfTestWantRow = modelRow
    gSelfTestCellCount = -1
    AfxRedrawWindow( GetDlgItem( hCtl, IDC_FRMMAIN_LISTBOX ) )
    return (gSelfTestCellCount <> -1)
end function

sub frmMain_RunSelfTest()
    dim as HWND hB   = HWND_FRMEXPLORER2
    dim as HWND hHdr = HWND_COLHDR
    dim pWindow as CWindow ptr = AfxCWindowPtr( hHdr )

    print ""
    print "=== CListBox columns/header self-test ==="

    ' ---- 1. Column rect math (widths 160 / 70 / fill on the live client width) ----
    dim as RECT rcC : GetClientRect( hHdr, @rcC )
    dim as integer clientW = rcC.right - rcC.left
    dim as RECT rc0, rc1, rc2
    CColumnHeader_GetColumnRect( hHdr, 0, rc0 )
    CColumnHeader_GetColumnRect( hHdr, 1, rc1 )
    CColumnHeader_GetColumnRect( hHdr, 2, rc2 )
    SelfTest_Check( "col0 = [0..160]", (rc0.left = 0) andalso (rc0.right = 160), _
        "l=" & str(rc0.left) & " r=" & str(rc0.right) )
    SelfTest_Check( "col1 = [160..230]", (rc1.left = 160) andalso (rc1.right = 230), _
        "l=" & str(rc1.left) & " r=" & str(rc1.right) )
    SelfTest_Check( "columns contiguous", (rc1.left = rc0.right) andalso (rc2.left = rc1.right), _
        "c1.l=" & str(rc1.left) & " c2.l=" & str(rc2.left) )
    SelfTest_Check( "fill.right = clientW", (rc2.right = clientW), _
        "r=" & str(rc2.right) & " clientW=" & str(clientW) )

    ' ---- 2. Programmatic width setter: clamps to the scaled minimum, and is SILENT ----
    dim as integer minDefault = CCOLHDR_DEFAULT_MINWIDTH
    if pWindow then minDefault = pWindow->ScaleX( CCOLHDR_DEFAULT_MINWIDTH )
    dim as integer notifyBefore = gResizeNotifyCount
    CListBox_SetColumnWidth( hB, 1, 5 )
    SelfTest_Check( "SetColumnWidth clamps to scaled min", _
        (CListBox_GetColumnWidth( hB, 1 ) = minDefault), _
        "w=" & str(CListBox_GetColumnWidth( hB, 1 )) & " min=" & str(minDefault) )
    SelfTest_Check( "programmatic width change is silent", _
        (gResizeNotifyCount = notifyBefore), _
        "notifies=" & str(gResizeNotifyCount - notifyBefore) )
    CListBox_SetColumnWidth( hB, 1, 70 )    ' restore

    ' ---- 3. Divider hit zones: boundary +/- gutter hits, one past misses, and the
    '         fill column's own divider is never grabbable ----
    dim as integer gutter = CCOLHDR_DIVIDER_GUTTER
    if pWindow then gutter = pWindow->ScaleX( CCOLHDR_DIVIDER_GUTTER )
    CColumnHeader_GetColumnRect( hHdr, 0, rc0 )
    CColumnHeader_GetColumnRect( hHdr, 2, rc2 )
    dim as integer bx = rc0.right
    dim as integer midY = (rcC.bottom - rcC.top) \ 2
    SelfTest_Check( "divider hits at boundary", _
        (CColumnHeader_HitTestDivider( hHdr, bx, midY ) = 0), "x=" & str(bx) )
    SelfTest_Check( "divider hits at boundary-gutter", _
        (CColumnHeader_HitTestDivider( hHdr, bx - gutter, midY ) = 0), "x=" & str(bx - gutter) )
    SelfTest_Check( "divider hits at boundary+gutter", _
        (CColumnHeader_HitTestDivider( hHdr, bx + gutter, midY ) = 0), "x=" & str(bx + gutter) )
    SelfTest_Check( "no divider at boundary-(gutter+1)", _
        (CColumnHeader_HitTestDivider( hHdr, bx - gutter - 1, midY ) = -1), "x=" & str(bx - gutter - 1) )
    SelfTest_Check( "no divider at boundary+(gutter+1)", _
        (CColumnHeader_HitTestDivider( hHdr, bx + gutter + 1, midY ) = -1), "x=" & str(bx + gutter + 1) )
    SelfTest_Check( "fill column's divider not grabbable", _
        (CColumnHeader_HitTestDivider( hHdr, rc2.right, midY ) = -1), "x=" & str(rc2.right) )

    ' ---- 4. Painted cells: captured from the real paint path, equal to the
    '         header's rects, y spanning the row height; sparse cells read empty ----
    CListBox_SetPaintCallback( hB, @SelfTest_RowCapturePaint )
    if SelfTest_GrabRow( hB, 1 ) then
        SelfTest_Check( "item row captures columnCount=3", (gSelfTestCellCount = 3), _
            "n=" & str(gSelfTestCellCount) )
        if gSelfTestCellCount = 3 then
            dim as boolean okX = true
            dim as RECT rcH
            for c as integer = 0 to 2
                CColumnHeader_GetColumnRect( hHdr, c, rcH )
                if (gSelfTestCells(c).rc.left <> rcH.left) orelse (gSelfTestCells(c).rc.right <> rcH.right) then okX = false
            next
            SelfTest_Check( "cell x-spans == header rects", okX, _
                "c0=[" & str(gSelfTestCells(0).rc.left) & ".." & str(gSelfTestCells(0).rc.right) & _
                "] c1=[" & str(gSelfTestCells(1).rc.left) & ".." & str(gSelfTestCells(1).rc.right) & "]" )
            ' CONTRACT CHANGE, and this assertion is the one that caught it. Cell rects
            ' used to arrive in a row-sized buffer starting at y=0, so this checked
            ' top = 0 and bottom = LB_GETITEMHEIGHT. Both halves are now wrong: rows are
            ' painted in the surface's CLIENT coordinates, and there is no listbox left
            ' to ask for an item height (LB_GETITEMHEIGHT returned 0, which is why the
            ' failure read "rowH=0").
            '
            ' Checked against the ROW'S OWN rect rather than against any absolute origin,
            ' which is what it should always have said -- a cell spans exactly its row,
            ' wherever that row happens to be. A host row painter that derives everything
            ' from p->rc needs no change for this; one that hardcoded 0 for the top does.
            dim as integer rowH = gSelfTestRowRc.bottom - gSelfTestRowRc.top
            SelfTest_Check( "cell y-span = the row's own y-span", _
                (rowH > 0) andalso _
                (gSelfTestCells(0).rc.top = gSelfTestRowRc.top) andalso _
                (gSelfTestCells(0).rc.bottom = gSelfTestRowRc.bottom), _
                "cell=[" & str(gSelfTestCells(0).rc.top) & ".." & str(gSelfTestCells(0).rc.bottom) & _
                "] row=[" & str(gSelfTestRowRc.top) & ".." & str(gSelfTestRowRc.bottom) & _
                "] rowH=" & str(rowH) )
        end if
    else
        SelfTest_Check( "paint captured (item row)", false, "the row paint callback never ran" )
    end if
    SelfTest_Check( "sparse folder Size cell reads empty", _
        (len(CListBox_GetCellText( hB, 43, 1 )) = 0), _
        "len=" & str(len(CListBox_GetCellText( hB, 43, 1 ))) )

    ' ---- 5. A width change reflows the painted cells ----
    CListBox_SetColumnWidth( hB, 0, 150 )
    if SelfTest_GrabRow( hB, 1 ) then
        dim as boolean okW = (gSelfTestCellCount = 3)
        if okW then okW = (gSelfTestCells(1).rc.left = 150)
        SelfTest_Check( "SetColumnWidth(0,150) -> cell1.left = 150", okW, _
            "left=" & str(gSelfTestCells(1).rc.left) )
    else
        SelfTest_Check( "paint captured (width reflow)", false, "the row paint callback never ran" )
    end if
    CListBox_SetColumnWidth( hB, 0, 160 )   ' restore

    ' ---- 6. Group-header rows span the full width (columnCount = 0) ----
    if SelfTest_GrabRow( hB, 0 ) then
        dim as boolean okHdr = (gSelfTestCellCount = 0)
        if okHdr then okHdr = gSelfTestRowIsHeader
        SelfTest_Check( "group header captures columnCount=0", okHdr, _
            "n=" & str(gSelfTestCellCount) )
    else
        SelfTest_Check( "paint captured (group header)", false, "the row paint callback never ran" )
    end if

    ' ---- 7. Instance A (no columns): the pre-columns contract, proven not assumed ----
    CListBox_SetPaintCallback( HWND_FRMEXPLORER, @SelfTest_RowCapturePaint )
    if SelfTest_GrabRow( HWND_FRMEXPLORER, -2 ) then
        SelfTest_Check( "no-columns instance captures columnCount=0", _
            (gSelfTestCellCount = 0), "n=" & str(gSelfTestCellCount) )
    else
        SelfTest_Check( "paint captured (instance A)", false, "the row paint callback never ran" )
    end if

    ' ---- 7b. SelChange: fires for user-driven moves ONLY, and never twice for the same
    '          row. The keyboard case is the reason the callback exists at all -- the
    '          control consumes WM_KEYDOWN, so nothing else can observe arrow navigation.
    CListBox_SetSelChangeCallback( hB, @SelfTest_SelChange )
    dim as HWND hInner = GetDlgItem( hB, IDC_FRMMAIN_LISTBOX )

    gSelChangeCount = 0
    CListBox_SetCurSel( hB, 2 )
    SelfTest_Check( "SetCurSel is silent", (gSelChangeCount = 0), _
        "fires=" & str(gSelChangeCount) )

    gSelChangeCount = 0
    SendMessage( hInner, WM_KEYDOWN, VK_DOWN, 0 )
    dim as integer rowAfterDown = CListBox_GetCurSel( hB )
    SelfTest_Check( "VK_DOWN fires exactly once", (gSelChangeCount = 1), _
        "fires=" & str(gSelChangeCount) )
    SelfTest_Check( "SelChange row = GetCurSel", (gSelChangeRow = rowAfterDown), _
        "cb=" & str(gSelChangeRow) & " GetCurSel=" & str(rowAfterDown) )
    SelfTest_Check( "VK_DOWN actually moved off row 2", (rowAfterDown <> 2), _
        "row=" & str(rowAfterDown) )

    gSelChangeCount = 0
    SendMessage( hInner, WM_KEYDOWN, VK_DOWN, 0 )
    SelfTest_Check( "second VK_DOWN fires once more", (gSelChangeCount = 1), _
        "fires=" & str(gSelChangeCount) )

    ' Re-setting the row that is already current must stay silent.
    gSelChangeCount = 0
    CListBox_SetCurSel( hB, CListBox_GetCurSel( hB ) )
    SelfTest_Check( "SetCurSel to the current row is silent", (gSelChangeCount = 0), _
        "fires=" & str(gSelChangeCount) )

    ' Clamped navigation does not move the focus, so it must not notify either.
    CListBox_SetCurSel( hB, 0 )
    gSelChangeCount = 0
    SendMessage( hInner, WM_KEYDOWN, VK_UP, 0 )
    SelfTest_Check( "VK_UP clamped at the top is silent", (gSelChangeCount = 0), _
        "fires=" & str(gSelChangeCount) & " row=" & str(CListBox_GetCurSel(hB)) )

    ' A host that never sets the callback must be unaffected.
    CListBox_SetSelChangeCallback( hB, 0 )
    gSelChangeCount = 0
    SendMessage( hInner, WM_KEYDOWN, VK_DOWN, 0 )
    SelfTest_Check( "cleared callback stops firing", (gSelChangeCount = 0), _
        "fires=" & str(gSelChangeCount) )

    ' ---- 8. DESTRUCTIVE (last): column delete/insert shift the per-row cells across
    '         the Text<->cells(0) alias, and the fill designation self-heals ----
    CListBox_DeleteColumn( hB, 2 )
    SelfTest_Check( "delete fill col -> effective fill = last (1)", _
        (CListBox_GetFillColumn( hB ) = 1), "fill=" & str(CListBox_GetFillColumn( hB )) )
    SelfTest_Check( "deleted column's cells removed", _
        (len(CListBox_GetCellText( hB, 1, 2 )) = 0), _
        "len=" & str(len(CListBox_GetCellText( hB, 1, 2 ))) )
    dim as string sSize = CListBox_GetCellText( hB, 1, 1 )
    SelfTest_Check( "cells before the deleted column survive", (sSize = "137"), "c1=" & sSize )
    CListBox_InsertColumn( hB, 0, "Icon", 40 )
    dim as string sShift0 = CListBox_GetCellText( hB, 1, 0 )
    dim as string sShift1 = CListBox_GetCellText( hB, 1, 1 )
    dim as boolean okIns = (len(sShift0) = 0) andalso (sShift1 = "file0.bas")
    SelfTest_Check( "insert at 0 shifts Text into column 1", okIns, _
        "c0len=" & str(len(sShift0)) & " c1=" & sShift1 )

    print "=== self-test complete: " & str(gFailCount) & " failure(s) ==="
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
        dim as integer r = CListBox_AddString( HWND_FRMEXPLORER2, "file" & i & ".bas", i )
        ' Columns 1/2 (Size/Type); column 0 is the AddString text itself.
        CListBox_SetCellText( HWND_FRMEXPLORER2, r, 1, str( (i + 1) * 137 ) )
        CListBox_SetCellText( HWND_FRMEXPLORER2, r, 2, "FreeBASIC source" )
    next
    CListBox_AddHeader( HWND_FRMEXPLORER2, "Folders" )           ' model row 42
    for i as integer = 0 to 4
        dim as integer r = CListBox_AddString( HWND_FRMEXPLORER2, "folder" & i, i )
        ' Size stays "" (sparse cells) -- proves never-set cells paint as empty.
        CListBox_SetCellText( HWND_FRMEXPLORER2, r, 2, "Folder" )
    next
    CListBox_EndUpdate( HWND_FRMEXPLORER2 )
    ' Start the "Folders" group collapsed. (Was CollapseRow(6) -- comment rot from when
    ' the file loop added 5 rows; row 6 is a file item now, so that call was a silent
    ' no-op and the group never started collapsed.)
    CListBox_CollapseRow( HWND_FRMEXPLORER2, 42 )

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
    print "             live=false) and the row cells follow; ESC mid-drag snaps back;"
    print "             dbl-click a divider autosizes to the caption; click a column"
    print "             body SORTS instance B (arrow glyph, toggles asc/desc; press,"
    print "             slide off, release -> nothing); tooltip shows column text"
    print "  Self-test  CLISTBOX_SELFTEST=1 main.exe -> geometry asserts, then exits"
    print

    
    UpdateWindow( pWindow->hWindow )
    ShowWindow( pWindow->hWindow, SW_SHOW )

    ' Headless geometry/contract assertions, then exit (CHScrollBar's env-gate shape):
    ' a normal launch never runs this.
    if environ("CLISTBOX_SELFTEST") = "1" then
        frmMain_RunSelfTest()
        DestroyWindow( HWND_FRMMAIN )
    end if

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

