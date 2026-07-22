'    tiko editor - Programmer's Code Editor for the FreeBASIC Compiler
'    Copyright (C) 2016-2026 Paul Squires, PlanetSquires Software
'
'    This program is free software: you can redistribute it and/or modify
'    it under the terms of the GNU General Public License as published by
'    the Free Software Foundation, either version 3 of the License, or
'    (at your option) any later version.
'
'    This program is distributed in the hope that it will be useful,
'    but WITHOUT any WARRANTY; without even the implied warranty of
'    MERCHANTABILITY or FITNESS for A PARTICULAR PURPOSE.  See the
'    GNU General Public License for more details.


#include once "clsDoubleBuffer.bi"

' ========================================================================================
' Determine if the mouse cursor is currently over client RECT area
' ========================================================================================
function isMouseOverRECT( byval hWin as HWND, byval rc as RECT ) as boolean
    dim as point pt
    GetCursorPos( @pt )
    MapWindowpoints( hWin, HWND_DESKTOP, cast( point ptr, @rc), 2 )
    if PtInRect( @rc, pt ) then return true
end function

function isMouseOverWindow( byval hChild as HWND ) as boolean
    dim as point pt
    GetCursorPos( @pt )
    dim as HWND hWindow = WindowFromPoint(pt)
    if hWindow = hChild then return true
end function

function PaintRect( byval hDC as HDC, byval rc as RECT ptr, byval clr as COLORREF ) as long
    dim as COLORREF oldclr = SetBkColor(hDC, clr)
    ExtTextOut( hDC, 0, 0, ETO_OPAQUE, rc, "", 0, 0)
    SetBkColor( hDC, oldclr )
    function = 0
end function


#ifdef DBUF_IMPL_GDIPLUS
' ========================================================================================
' COLORREF is 0x00BBGGRR; GDI+ ARGB is 0xAARRGGBB. The byte order is REVERSED, not just
' shifted -- swapping R and B is the whole job, and getting it wrong yields a picture that
' still renders (so it looks like it works) with every colour subtly wrong.
' ========================================================================================
private function DBufToARGB( byval clr as COLORREF, byval nAlpha as ubyte ) as ARGB
    dim as ulong r = (clr        ) and &hFF
    dim as ulong g = (clr shr  8 ) and &hFF
    dim as ulong b = (clr shr 16 ) and &hFF
    return cast( ARGB, (culng(nAlpha) shl 24) or (r shl 16) or (g shl 8) or b )
end function
#endif


function clsDoubleBuffer.SetupBitmap() as long
    _memDC = CreateCompatibleDC( _hDC )
    SaveDC( _memDC )

    dim as long w = _rc.right - _rc.left
    dim as long h = _rc.bottom - _rc.top

    #ifdef DBUF_IMPL_GDIPLUS
        ' A 32bpp top-down DIB section rather than CreateCompatibleBitmap: GDI+ wants a
        ' KNOWN pixel format for antialiasing and alpha blending, where a compatible
        ' bitmap is whatever the device happens to be. On a 32bpp display this is still
        ' the device format, so the final BitBlt stays a straight copy.
        ' CreateDIBSection rejects a zero or negative extent, which BeginPaint can hand
        ' us for a collapsed window; clamp rather than leak a DC on the early return.
        if w < 1 then w = 1
        if h < 1 then h = 1
        dim as BITMAPINFO bmi
        with bmi.bmiHeader
            .biSize        = sizeof(BITMAPINFOHEADER)
            .biWidth       = w
            .biHeight      = -h          ' negative = top-down, matching GDI's y-axis
            .biPlanes      = 1
            .biBitCount    = 32
            .biCompression = BI_RGB
        end with
        dim as any ptr pBits
        _hbit = CreateDIBSection( _hDC, @bmi, DIB_RGB_COLORS, @pBits, null, 0 )
    #else
        _hbit = CreateCompatibleBitmap( _hDC, w, h )
    #endif

    if _hbit then SelectObject( _memDC, _hbit )
    function = 0
end function

' ========================================================================================
' Whole-client paint, from WM_PAINT.
'
' Under D2D THERE IS NO BACK BUFFER HERE and no blit: the render target presents itself.
' BeginPaint is still called, and not for its HDC -- it is what clears the update region.
' Skipping it would leave the region dirty and Windows would send WM_PAINT forever.
'
' THE UPDATE-REGION CLIP, and why it is not optional.
'
' Under GDI and GDI+ the HDC that BeginPaint returns is CLIPPED to the update region. That
' silently makes a very common idiom correct: a control fills its WHOLE client with the
' background and then redraws only the items intersecting the update rect, because the
' fill lands only inside the update region and everything outside keeps the pixels already
' on screen. Five controls in this family are written that way (CColumnHeader, CStatusBar,
' CTabBar, CIconPanel, CSelectBar).
'
' A Direct2D render target is the window's own surface and NOTHING clips it. The same code
' therefore wipes the entire client and redraws one item -- every other item vanishes.
' That is not theoretical: hovering off a CColumnHeader left two of its three captions
' blank, which is how this was found.
'
' Pushing the update rect as a clip reproduces GDI's semantics exactly, for every control,
' with no control changes at all. It works because a D2D HWND render target PRESERVES the
' previous frame -- measured, not assumed, since the documentation does not promise it:
' a frame that fills nothing and redraws one item leaves the rest of the window intact on
' this machine. If that ever stops being true the symptom would be exactly the bug above,
' which is at least a loud failure rather than a quiet one.
'
' ALIASED, not per-primitive: a clip on pixel boundaries must not feather its edges, or
' every partial repaint leaves a faint seam where it met the previous frame.
' ========================================================================================
function clsDoubleBuffer.BeginDoubleBuffer( byval hwnd as HWND ) as long
    _UsePaint = true
#ifdef DBUF_IMPL_D2D
    ' BEFORE BeginPaint, which is what clears the update region.
    dim as RECT rcUpd
    dim as boolean bHaveUpd = (GetUpdateRect( hwnd, @rcUpd, FALSE ) <> 0)
#endif
    _hwnd = hwnd
    _hDC = BeginPaint( _hwnd, @_ps )
    GetClientRect( _hwnd, @_rc )
#ifdef DBUF_IMPL_D2D
    _bBlitOnEnd = false
    _pRT = cast( ID2D1RenderTarget ptr, DBufD2D_TargetFor( _hwnd ) )
    if _pRT then
        ' ClearType, which is available only because the target is opaque
        ' (ALPHA_MODE_IGNORE). On a premultiplied target D2D silently downgrades to
        ' greyscale antialiasing, and the text just looks softer for no stated reason.
        _pRT->SetTextAntialiasMode( D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE )
        ' Which ClearType. Null params (the mode has none yet, or creation failed) leaves
        ' the target on the system default, which is what it would have used anyway.
        _pRT->SetTextRenderingParams( DBufD2D_RenderParamsFor( DBufD2D_GetTextMode() ) )
        _pRT->BeginDraw()
        _bDrawing = true

        ' Only when the update region is a STRICT subset of the client. A full-client
        ' update needs no clip, and pushing one would cost a state change for nothing.
        dim as boolean bPartial = bHaveUpd andalso _
                                  ((rcUpd.left > _rc.left) orelse (rcUpd.top > _rc.top) orelse _
                                   (rcUpd.right < _rc.right) orelse (rcUpd.bottom < _rc.bottom))
        if bPartial then
            _pRT->PushAxisAlignedClip( _
                type<D2D1_RECT_F>( csng(rcUpd.left), csng(rcUpd.top), _
                                   csng(rcUpd.right), csng(rcUpd.bottom) ), _
                D2D1_ANTIALIAS_MODE_ALIASED )
            _bClipPushed = true
        end if
    end if
#else
    this.SetupBitmap
#endif
    function = 0
end function

' Overload that provides an hDC rather than use BeginPaint/EndPaint
function clsDoubleBuffer.BeginDoubleBuffer( byval hwnd as HWND, byval hdc as HDC, byval rcItem as RECT ) as long
    _UsePaint = false
    _owns = true
    _hwnd = hwnd
    _hDC = hdc
    _rc = rcItem
#ifdef DBUF_IMPL_D2D
    ' A DC render target bound to the caller's DC. NOTE THE COORDINATE SPACE: BindDC maps
    ' the target's origin onto rcItem's top-left, which is exactly what the GDI backend
    ' achieves with a 0-based memDC plus an offset BitBlt. So callers draw 0-based under
    ' both, and no blit is needed here -- EndDraw does the copy.
    _bBlitOnEnd = false
    _pRT = cast( ID2D1RenderTarget ptr, DBufD2D_DCTargetFor( hdc, rcItem ) )
    if _pRT then
        _pRT->SetTextAntialiasMode( D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE )
        ' Which ClearType. Null params (the mode has none yet, or creation failed) leaves
        ' the target on the system default, which is what it would have used anyway.
        _pRT->SetTextRenderingParams( DBufD2D_RenderParamsFor( DBufD2D_GetTextMode() ) )
        _pRT->BeginDraw()
        _bDrawing = true
    end if
#else
    this.SetupBitmap
#endif
    function = 0
end function

' Cached overload: draw into a caller-owned memDC (bitmap already selected and
' large enough). Saves DC state so per-row object selections don't accumulate;
' EndDoubleBuffer blits but does not delete the DC/bitmap (the caller owns them).
function clsDoubleBuffer.BeginDoubleBuffer( byval hwnd as HWND, byval hdc as HDC, byval rcItem as RECT, byval cachedMemDC as HDC ) as long
    _UsePaint = false
    _owns = false
    _hwnd = hwnd
    _hDC = hdc
    _rc = rcItem
#ifdef DBUF_IMPL_D2D
    ' cachedMemDC is DELIBERATELY IGNORED under D2D. It exists to spare the GDI backends
    ' the cost of creating a compatible DC and bitmap per row; Direct2D has no equivalent
    ' churn to avoid, so binding straight to the destination DC is both simpler and one
    ' whole blit cheaper. The parameter stays in the signature so the same host code
    ' compiles on every backend.
    _memDC = 0
    _hbit  = 0
    _bBlitOnEnd = false
    _pRT = cast( ID2D1RenderTarget ptr, DBufD2D_DCTargetFor( hdc, rcItem ) )
    if _pRT then
        _pRT->SetTextAntialiasMode( D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE )
        ' Which ClearType. Null params (the mode has none yet, or creation failed) leaves
        ' the target on the system default, which is what it would have used anyway.
        _pRT->SetTextRenderingParams( DBufD2D_RenderParamsFor( DBufD2D_GetTextMode() ) )
        _pRT->BeginDraw()
        _bDrawing = true
    end if
#else
    _memDC = cachedMemDC
    _hbit = 0
    SaveDC( _memDC )
#endif
    function = 0
end function

function clsDoubleBuffer.EndDoubleBuffer() as long
#ifdef DBUF_IMPL_D2D
    ' Hoisted to a boolean local rather than written as "_pRT andalso _bDrawing":
    ' fbc warning 38 fires on mixing a pointer with a boolean in andalso/orelse.
    dim as boolean bActive = (_pRT <> 0) andalso _bDrawing
    if bActive then
        ' Before EndDraw, always. An unbalanced clip makes EndDraw fail outright, so the
        ' window would freeze on its last good frame -- the same silent failure mode as an
        ' unhandled device loss.
        this.PopClipIfPushed()
        dim as HRESULT hr = _pRT->EndDraw()
        _bDrawing = false
        ' Device loss: the GPU behind the target went away (driver reset, remote-desktop
        ' transition, hot-swap). Nothing crashes -- the window would simply freeze on its
        ' last good frame -- so it has to be handled explicitly. This is the one obligation
        ' neither GDI nor GDI+ ever had.
        if hr = D2DERR_RECREATE_TARGET then DBufD2D_HandleDeviceLoss( _hwnd )
    end if
    ' The brush belongs to the render target, so it must go before the target is reused.
    this.ReleaseD2DObjects()
    _pRT = 0
    if _UsePaint then EndPaint( _hwnd, @_ps ) : _UsePaint = false
    return 0
#else
    #ifdef DBUF_IMPL_GDIPLUS
        ' Tear the GDI+ objects down BEFORE the blit. Deleting the Graphics flushes any
        ' batched drawing; blitting first would copy a surface that is still missing it.
        this.ReleaseGpObjects()
    #endif
    BitBlt( _hDC, _rc.left, _rc.top, _rc.right - _rc.left, _rc.bottom - _rc.top, _memDC, 0, 0, SRCCOPY )
    RestoreDC( _memDC, -1 )
    if _owns then
        if _hbit  then DeleteObject( _hbit ) : _hbit = 0
        if _memDC then DeleteDC( _memDC )    : _memDC = 0
    else
        _memDC = 0    ' cached DC is owned by the caller; do not delete
    end if
    if _UsePaint then EndPaint( _hwnd, @_ps ) : _UsePaint = false
    function = 0
#endif
end function

' RAII safety net: release any GDI objects still held if EndDoubleBuffer was
' not reached (e.g. an early return between Begin and End). Only frees objects
' this instance owns; handles are nulled by End, so this never double-frees.
destructor clsDoubleBuffer()
    #ifdef DBUF_IMPL_D2D
        ' An unbalanced BeginDraw is worse than a leaked GDI handle: the target stays mid
        ' draw and EVERY later paint through it fails, so the window stops updating
        ' entirely. End clears the flag, so reaching this means Begin/End was not balanced.
        dim as boolean bStillDrawing = (_pRT <> 0) andalso _bDrawing
        if bStillDrawing then
            this.PopClipIfPushed()
            _pRT->EndDraw()
            _bDrawing = false
        end if
        this.ReleaseD2DObjects()
        _pRT = 0
    #endif
    #ifdef DBUF_IMPL_GDIPLUS
        ' Same reasoning as the GDI handles below: End nulls these, so reaching them
        ' here means Begin/End was not balanced. Must still happen before the process
        ' calls GdiplusShutdown.
        this.ReleaseGpObjects()
    #endif
    if _owns then
        if _hbit  then DeleteObject( _hbit ) : _hbit = 0
        if _memDC then DeleteDC( _memDC )    : _memDC = 0
    end if
    ' Only ever set by the descriptor SetFont overload, and only on the GDI backends.
    ' _hFont itself is host-owned and must NOT be touched.
    if _hFontOwned then DeleteObject( _hFontOwned ) : _hFontOwned = 0
    if _UsePaint andalso (_hwnd <> 0) then EndPaint( _hwnd, @_ps ) : _UsePaint = false
end destructor


#ifdef DBUF_IMPL_D2D
' ========================================================================================
' One solid brush per buffer, recoloured rather than reallocated. Created against the
' render target, so it must not outlive the paint -- hence ReleaseD2DObjects in End.
' ========================================================================================
private function clsDoubleBuffer.BrushFor( byval clr as COLORREF, byval nAlpha as ubyte ) as ID2D1SolidColorBrush ptr
    if _pRT = 0 then return 0
    dim as D2D1_COLOR_F c = DBufToColorF( clr, nAlpha )
    if _pBrush = 0 then
        if _pRT->CreateSolidColorBrush( c, *cast(D2D1_BRUSH_PROPERTIES ptr, NULL), _pBrush ) <> S_OK then return 0
        if _pBrush = 0 then return 0
    elseif (_brushColorSet = false) orelse (c.r <> _brushR) orelse (c.g <> _brushG) _
                                   orelse (c.b <> _brushB) orelse (c.a <> _brushA) then
        _pBrush->SetColor( c )
    end if
    _brushColorSet = true
    _brushR = c.r : _brushG = c.g : _brushB = c.b : _brushA = c.a
    return _pBrush
end function

private sub clsDoubleBuffer.ReleaseD2DObjects()
    if _pBrush then _pBrush->Release() : _pBrush = 0
    _brushColorSet = false
end sub

' Pop the update-region clip if one was pushed, exactly once. Guarded by its own flag
' rather than by "did we take the partial path", so the two can never disagree.
private sub clsDoubleBuffer.PopClipIfPushed()
    if _bClipPushed andalso (_pRT <> 0) then
        _pRT->PopAxisAlignedClip()
    end if
    _bClipPushed = false
end sub
#endif


#ifdef DBUF_IMPL_GDIPLUS
' ========================================================================================
' GDI+ object lifetime. All three are lazy: a buffer that only ever paints text (several
' do) never constructs a Graphics at all.
' ========================================================================================
private function clsDoubleBuffer.EnsureGraphics() as CGpGraphics ptr
    if _pGraphics = 0 then
        if _memDC = 0 then return 0
        _pGraphics = new CGpGraphics( _memDC )
        if _pGraphics = 0 then return 0
        ' Integral coordinates land on whole pixels without this, but any half-pixel
        ' geometry (the inset on an even pen width) would straddle two.
        _pGraphics->SetPixelOffsetMode( PixelOffsetModeHalf )
    end if
    return _pGraphics
end function

private function clsDoubleBuffer.BrushFor( byval clr as ARGB ) as CGpBrush ptr
    if _pBrush = 0 then
        _pBrush = new CGpSolidBrush( clr )
        _brushARGB = clr
    elseif _brushARGB <> clr then
        _pBrush->SetColor( clr )
        _brushARGB = clr
    end if
    return cast( CGpBrush ptr, _pBrush )
end function

private function clsDoubleBuffer.PenFor( byval clr as ARGB, byval nWidth as single ) as CGpPen ptr
    if _pPen = 0 then
        _pPen = new CGpPen( clr, nWidth )
        _penARGB = clr
        _penWidthCached = nWidth
    else
        if _penARGB <> clr then
            _pPen->SetColor( clr )
            _penARGB = clr
        end if
        if _penWidthCached <> nWidth then
            _pPen->SetWidth( nWidth )
            _penWidthCached = nWidth
        end if
    end if
    return _pPen
end function

private sub clsDoubleBuffer.EnsureGdiReady()
    if _gpDirty andalso (_pGraphics <> 0) then
        _pGraphics->Flush( FlushIntentionSync )
        _gpDirty = false
    end if
end sub

private sub clsDoubleBuffer.ReleaseGpObjects()
    if _pPen      then delete _pPen      : _pPen = 0
    if _pBrush    then delete _pBrush    : _pBrush = 0
    if _pGraphics then delete _pGraphics : _pGraphics = 0
    _gpDirty = false
end sub

' ========================================================================================
' Build a rounded-rectangle path from four 90-degree arcs.
'
' radius is a TRUE RADIUS. Callers converting from GDI must halve first: RoundRect's
' curvature arguments are the ellipse WIDTH and HEIGHT, i.e. a diameter. Passing GDI's
' number straight through here makes every corner twice as round, which still looks
' deliberate and is why PaintRectFactory does the halving in one clearly-marked place.
' ========================================================================================
private sub clsDoubleBuffer.BuildRoundPath( _
            byval pPath as CGpGraphicsPath ptr, _
            byval x as single, _
            byval y as single, _
            byval w as single, _
            byval h as single, _
            byval radius as single _
            )
    pPath->Reset()

    ' GDI clamps the corner to half the box; match it, or a large curvature on a small
    ' icon button produces a degenerate path instead of a stadium.
    dim as single r = radius
    if r > (w / 2.0) then r = w / 2.0
    if r > (h / 2.0) then r = h / 2.0

    if r <= 0.0 then
        pPath->AddRectangle( x, y, w, h )
        exit sub
    end if

    dim as single d = r * 2.0
    pPath->AddArc( x,         y,         d, d, 180.0, 90.0 )   ' top-left
    pPath->AddArc( x + w - d, y,         d, d, 270.0, 90.0 )   ' top-right
    pPath->AddArc( x + w - d, y + h - d, d, d,   0.0, 90.0 )   ' bottom-right
    pPath->AddArc( x,         y + h - d, d, d,  90.0, 90.0 )   ' bottom-left
    pPath->CloseFigure()
end sub
#endif


function clsDoubleBuffer.rcClient() as RECT
    return _rc
end function

function clsDoubleBuffer.rcClientWidth() as long
    return _rc.right - _rc.left
end function

function clsDoubleBuffer.rcClientHeight() as long
    return _rc.bottom - _rc.top
end function

' The escape hatch to the raw surface. Asking for the HDC means the caller is about to do
' something with GDI -- read pixels back, blit, draw directly -- so the GDI+ batch is
' flushed first. Without this, anything drawn through the buffer's own methods may simply
' not be on the surface yet, and a caller that reads it sees a half-painted picture. That
' is not hypothetical: CToggle's self-test scans the knob's rim through this exact path,
' BEFORE EndDoubleBuffer, and would otherwise be measuring blank background.
function clsDoubleBuffer.getMemDC() as HDC
    #ifdef DBUF_IMPL_D2D
        ' THERE IS NO memDC UNDER D2D. The HWND render target owns its own surface and
        ' presents it; nothing GDI can draw on is ever created. Returning 0 rather than
        ' inventing one is deliberate -- a caller that reaches through this is about to do
        ' GDI work on a surface D2D owns, and GDI drawing between BeginDraw and EndDraw is
        ' not merely discouraged, it is undefined.
        '
        ' PORTABILITY NOTE for the rest of the family: CToggle's self-test reads pixels
        ' back through exactly this path. It would need the offscreen route instead -- the
        ' BeginDoubleBuffer( hwnd, hdc, rc ) overload, which under D2D is a DC render
        ' target and CAN be read back.
        return 0
    #else
        #ifdef DBUF_IMPL_GDIPLUS
            this.EnsureGdiReady()
        #endif
        return _memDC
    #endif
end function

' Store a caller-supplied font handle. The control/host owns the HFONT and is
' responsible for destroying it; the double buffer only selects it while painting.
function clsDoubleBuffer.SetFont( byval hFont as HFONT ) as long
    if hFont then _hFont = hFont
    function = 0
end function

' ----------------------------------------------------------------------------------------
' Descriptor overload. On D2D this is the natural form and no GDI font object exists at
' all; on the GDI backends there is no way to name a typeface except through an HFONT, so
' one is synthesised and owned by this buffer.
'
' nSizeDIP is a DIP em size, matching DirectWrite. At the 96 DPI the render target is
' pinned to, that is also a pixel size -- and the negative lfHeight below is what makes
' the GDI backends agree, since a negative height is GDI's em size in pixels.
' ----------------------------------------------------------------------------------------
function clsDoubleBuffer.SetFont( _
            byval wszFamily as DWSTRING, _
            byval nSizeDIP as single, _
            byval nWeight as long = FW_NORMAL, _
            byval bItalic as boolean = false _
            ) as long
    if len( wszFamily ) = 0 then return 0
    if nSizeDIP <= 0.0 then return 0

    ' Replace any font this buffer previously synthesised; never touch a host-owned one.
    if _hFontOwned then DeleteObject( _hFontOwned ) : _hFontOwned = 0

    dim as LOGFONTW lf
    lf.lfHeight  = -clng( nSizeDIP + 0.5 )
    lf.lfWeight  = nWeight
    lf.lfItalic  = iif( bItalic, 1, 0 )
    lf.lfCharSet = DEFAULT_CHARSET
    lf.lfQuality = CLEARTYPE_QUALITY
    lf.lfFaceName = left( wszFamily, 31 )

    _hFontOwned = CreateFontIndirectW( @lf )
    if _hFontOwned then _hFont = _hFontOwned
    function = 0
end function


' ========================================================================================
' Measure a string. See the contract note in clsDoubleBuffer.bi for why this exists.
' ========================================================================================
function clsDoubleBuffer_MeasureText( byval wszText as DWSTRING, byval hFont as HFONT ) as SIZE
    dim as SIZE sz
    if len( wszText ) = 0 then return sz

#ifdef DBUF_IMPL_D2D
    if gDWriteFactory = 0 then return sz
    dim as IDWriteTextFormat ptr pFmt = DBufD2D_TextFormatFor( hFont )
    if pFmt = 0 then return sz

    ' The cached format may still be carrying trimming and wrapping from whatever drew
    ' last. Both change the answer -- trimming caps the reported width at the layout box,
    ' and wrapping folds a long string onto several lines -- so state them here rather
    ' than inherit them. Same shared-mutable-format trap as DBufD2D_SetEllipsis.
    DBufD2D_SetEllipsis( pFmt, false )
    pFmt->SetWordWrapping( DWRITE_WORD_WRAPPING_NO_WRAP )

    ' A deliberately huge layout box: measuring must report the text's natural extent, not
    ' the box it was asked to fit.
    '
    ' The layout has to be built the same way the text will be DRAWN. Under either GDI mode
    ' that means CreateGdiCompatibleTextLayout, which quantises advances to whole pixels
    ' exactly as GDI does; measuring with the natural layout and then drawing GDI-hinted
    ' would put the measurement and the rendering a pixel or two apart on every string.
    dim as IDWriteTextLayout ptr pLayout
    dim as long tm = DBufD2D_GetTextMode()
    dim as HRESULT hrL
    if tm = DBUF_TEXTMODE_NATURAL then
        hrL = gDWriteFactory->CreateTextLayout( *wszText.vptr, culng(len(wszText)), pFmt, _
                                                100000.0, 100000.0, pLayout )
    else
        ' pixelsPerDip 1.0: the render targets are pinned to 96 DPI, so one DIP is one
        ' pixel and no conversion is wanted here either.
        hrL = gDWriteFactory->CreateGdiCompatibleTextLayout( _
                    *wszText.vptr, culng(len(wszText)), pFmt, _
                    100000.0, 100000.0, 1.0, *cast(DWRITE_MATRIX ptr, NULL), _
                    (tm = DBUF_TEXTMODE_GDI_NATURAL), pLayout )
    end if
    if (hrL <> S_OK) orelse (pLayout = 0) then return sz

    dim as DWRITE_TEXT_METRICS m
    if pLayout->GetMetrics( m ) = S_OK then
        ' widthIncludingTrailingWhitespace, not width: GetTextExtentPoint32W counts
        ' trailing spaces, and this is meant to be a drop-in for it.
        ' Rounded UP -- a fractional advance that gets floored clips the last glyph.
        sz.cx = clng( m.widthIncludingTrailingWhitespace + 0.9999 )
        sz.cy = clng( m.height + 0.9999 )
    end if
    pLayout->Release()
    return sz
#else
    ' A screen DC rather than the buffer's: this must work outside a paint, which is
    ' exactly when the layout passes that need it run.
    dim as HDC hdc = GetDC( NULL )
    if hdc = 0 then return sz
    dim as HFONT hOld
    if hFont then hOld = SelectObject( hdc, hFont )
    GetTextExtentPoint32W( hdc, wszText.vptr, len(wszText), @sz )
    if hFont then SelectObject( hdc, hOld )
    ReleaseDC( NULL, hdc )
    return sz
#endif
end function

function clsDoubleBuffer.MeasureText( byval wszText as DWSTRING ) as SIZE
    return clsDoubleBuffer_MeasureText( wszText, _hFont )
end function

function clsDoubleBuffer.SetForeColor( byval forecolor as COLORREF ) as long
    _forecolor = forecolor
    _forealpha = 255
    function = 0
end function

function clsDoubleBuffer.SetBackColor( byval backcolor as COLORREF ) as long
    _backcolor = backcolor
    _backalpha = 255
    function = 0
end function

function clsDoubleBuffer.SetColors( byval forecolor as COLORREF, byval backcolor as COLORREF ) as long
    _forecolor = forecolor
    _backcolor = backcolor
    _forealpha = 255
    _backalpha = 255
    function = 0
end function

function clsDoubleBuffer.SetPenColor( byval pencolor as COLORREF ) as long
    _pencolor = pencolor
    _penalpha = 255
    function = 0
end function

' --- Alpha-bearing variants. Under the GDI backend the alpha is accepted and ignored,
'     so a host written against these still compiles and renders (opaque) either way. ---
function clsDoubleBuffer.SetForeColorA( byval forecolor as COLORREF, byval nAlpha as ubyte ) as long
    _forecolor = forecolor
    _forealpha = nAlpha
    function = 0
end function

function clsDoubleBuffer.SetBackColorA( byval backcolor as COLORREF, byval nAlpha as ubyte ) as long
    _backcolor = backcolor
    _backalpha = nAlpha
    function = 0
end function

function clsDoubleBuffer.SetPenColorA( byval pencolor as COLORREF, byval nAlpha as ubyte ) as long
    _pencolor = pencolor
    _penalpha = nAlpha
    function = 0
end function

' ========================================================================================
' The rounded/bordered rectangle workhorse.
'
' iStyle carries the legacy pen-style meaning: PS_SOLID strokes a border, PS_NULL means
' fill only (that is how PaintRoundRect asks for a borderless shape). nPenWidth = 0 has
' always meant the same thing by a different route -- the GDI path forced the pen colour
' to the fill colour -- so both are folded into one "no stroke" decision here.
'
' nCurvature keeps GDI's meaning for API compatibility: it is the ellipse DIAMETER.
' ========================================================================================
function clsDoubleBuffer.PaintRectFactory( _
            byval rc as RECT ptr, _
            byval iStyle as long, _
            byval nPenWidth as long = 1, _
            byval nCurvature as long = 0 _
            ) as long

#ifdef DBUF_IMPL_D2D

    if _pRT = 0 then return 0

    dim as boolean bNullPen  = (iStyle = PS_NULL)
    dim as boolean bZeroWide = (nPenWidth <= 0)
    dim as boolean bStroke   = (bNullPen = false) andalso (bZeroWide = false)

    ' Same extent contract as the GDI+ path, and it was re-measured against the GDI
    ' backend rather than inherited: GDI's RoundRect EXCLUDES right/bottom, so its outline
    ' sits on right-1 / bottom-1, while D2D (like GDI+) strokes through x+w.
    dim as single w = csng( rc->right - rc->left - 1 )
    dim as single h = csng( rc->bottom - rc->top - 1 )
    if (w < 0.0) orelse (h < 0.0) then return 0

    ' GDI's curvature is an ellipse DIAMETER; D2D1_ROUNDED_RECT takes RADII. Halving in
    ' one marked place, exactly as the GDI+ path does -- passing GDI's number straight
    ' through makes every corner twice as round, which still looks deliberate.
    dim as single radius = csng( nCurvature ) / 2.0
    ' D2D does not clamp the radius to half the box the way GDI does; an over-large
    ' curvature on a small rect produces a distorted shape rather than a stadium.
    if radius > (w / 2.0) then radius = w / 2.0
    if radius > (h / 2.0) then radius = h / 2.0
    if radius < 0.0 then radius = 0.0

    ' A stroke is CENTRED on its path, so half the pen hangs outside the shape. Inset by
    ' that half to keep it inside rc.
    dim as single fpw   = 0.0
    dim as single inset = 0.0
    if bStroke then
        fpw   = csng( nPenWidth )
        inset = (fpw - 1.0) / 2.0
    end if

    ' Antialias curves; never antialias a plain rectangle. A square-cornered fill wants to
    ' land on exact pixel boundaries, and AA would fringe all four edges.
    if nCurvature > 0 then
        _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE )
    else
        _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_ALIASED )
    end if

    dim as ID2D1SolidColorBrush ptr pFill = this.BrushFor( _backcolor, _backalpha )
    if pFill then
        dim as D2D1_ROUNDED_RECT rr
        rr.rect = type<D2D1_RECT_F>( csng(rc->left), csng(rc->top), _
                                     csng(rc->left) + w, csng(rc->top) + h )
        rr.radiusX = radius
        rr.radiusY = radius
        _pRT->FillRoundedRectangle( rr, cast( ID2D1Brush ptr, pFill ) )
    end if

    if bStroke then
        ' The half-pixel offset is the same correction the GDI+ path needed and for the
        ' same reason: a fill covers the pixels its path encloses, but a pen straddles its
        ' path, so a stroke laid on the fill boundary resolves one column to the left of
        ' where GDI put it. Offsetting to the pixel CENTRE puts it back.
        dim as ID2D1SolidColorBrush ptr pPen = this.BrushFor( _pencolor, _penalpha )
        if pPen then
            dim as D2D1_ROUNDED_RECT rs
            rs.rect = type<D2D1_RECT_F>( _
                        csng(rc->left) + inset + 0.5, csng(rc->top) + inset + 0.5, _
                        csng(rc->left) + w - inset + 0.5, csng(rc->top) + h - inset + 0.5 )
            rs.radiusX = radius
            rs.radiusY = radius
            _pRT->DrawRoundedRectangle( rs, cast( ID2D1Brush ptr, pPen ), fpw, NULL )
        end if
    end if

    _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE )
    function = 0

#elseif defined(DBUF_IMPL_GDIPLUS)

    dim as CGpGraphics ptr g = this.EnsureGraphics()
    if g = 0 then return 0

    dim as boolean bNullPen  = (iStyle = PS_NULL)
    dim as boolean bZeroWide = (nPenWidth <= 0)
    dim as boolean bStroke   = (bNullPen = false) andalso (bZeroWide = false)

    ' GDI's RoundRect EXCLUDES right/bottom, so its outline sits on right-1 / bottom-1.
    ' GDI+ strokes THROUGH x+w. Hence the -1: without it every shape is a pixel wider
    ' and taller than the GDI build drew it.
    dim as single w = csng( rc->right - rc->left - 1 )
    dim as single h = csng( rc->bottom - rc->top - 1 )
    if (w < 0.0) orelse (h < 0.0) then return 0

    ' See BuildRoundPath: GDI's curvature is a diameter, a GDI+ arc takes a radius.
    dim as single radius = csng( nCurvature ) / 2.0

    ' Both APIs centre the stroke on the path, so half the pen hangs outside the shape.
    ' Inset by that half to keep it within rc -- in floats now, where the half-pixel the
    ' GDI path silently rounded away is actually visible.
    dim as single inset = 0.0
    dim as single fpw   = 0.0
    if bStroke then
        fpw   = csng( nPenWidth )
        inset = (fpw - 1.0) / 2.0
    end if

    ' Antialias curves; never antialias a plain rectangle. A square-cornered fill wants
    ' to land on exact pixel boundaries, and AA would fringe all four edges.
    dim as long oldSmooth = g->GetSmoothingMode()
    if nCurvature > 0 then
        g->SetSmoothingMode( SmoothingModeAntiAlias )
    else
        g->SetSmoothingMode( SmoothingModeNone )
    end if

    ' Fill and stroke need DIFFERENT paths, and the half pixel between them is not a
    ' rounding detail -- it was measured. A fill covers the pixels its path encloses,
    ' but a pen is CENTRED on its path, so a stroke path laid on the fill boundary
    ' straddles two pixel columns and resolves one to the left of where GDI put it
    ' (verified: GDI drew a 1px border at 10..39, GDI+ at 9..38). Offsetting the stroke
    ' path to the pixel CENTRE puts the pen back over the intended column.
    dim as CGpGraphicsPath fillPath
    this.BuildRoundPath( @fillPath, csng(rc->left), csng(rc->top), w, h, radius )
    g->FillPath( this.BrushFor( DBufToARGB( _backcolor, _backalpha ) ), @fillPath )

    if bStroke then
        dim as CGpGraphicsPath strokePath
        this.BuildRoundPath( @strokePath, _
                             csng(rc->left) + inset + 0.5, csng(rc->top) + inset + 0.5, _
                             w - (inset * 2.0), h - (inset * 2.0), _
                             radius )
        g->DrawPath( this.PenFor( DBufToARGB( _pencolor, _penalpha ), fpw ), @strokePath )
    end if

    g->SetSmoothingMode( oldSmooth )
    _gpDirty = true
    function = 0

#else

    dim as COLORREF backcolor = _backcolor

    if nPenWidth = 0 then _pencolor = backcolor
    dim as HPEN hPenSolid = CreatePen( iStyle, nPenWidth, _pencolor )
    dim as HPEN hOldPen = SelectObject( _memDC, hPenSolid )

    dim as HBRUSH hBrushSolid = CreateSolidBrush( backcolor )
    dim as HBRUSH hOldBrush = SelectObject( _memDC, hBrushSolid )

    RoundRect( _memDC, rc->left, rc->top, rc->right, rc->bottom, nCurvature, nCurvature )

    DeleteObject( SelectObject( _memDC, hOldPen ) )
    DeleteObject( SelectObject( _memDC, hOldBrush ) )

    function = 0

#endif
end function

function clsDoubleBuffer.PaintRect( byval rc as RECT ptr ) as long
#ifdef DBUF_IMPL_D2D
    if _pRT = 0 then return 0
    ' Full extent, NOT the -1 the stroked paths take: ExtTextOut/ETO_OPAQUE fills
    ' [left,right) x [top,bottom), and an aliased FillRectangle on integral coordinates
    ' covers exactly the same pixels.
    dim as single w = csng( rc->right - rc->left )
    dim as single h = csng( rc->bottom - rc->top )
    if (w <= 0.0) orelse (h <= 0.0) then return 0

    dim as ID2D1SolidColorBrush ptr pFill = this.BrushFor( _backcolor, _backalpha )
    if pFill = 0 then return 0

    _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_ALIASED )
    dim as D2D1_RECT_F r = type<D2D1_RECT_F>( csng(rc->left), csng(rc->top), _
                                              csng(rc->left) + w, csng(rc->top) + h )
    _pRT->FillRectangle( r, cast( ID2D1Brush ptr, pFill ) )
    _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE )
    return 0
#elseif defined(DBUF_IMPL_GDIPLUS)
    dim as CGpGraphics ptr g = this.EnsureGraphics()
    if g = 0 then return 0

    ' ExtTextOut/ETO_OPAQUE fills [left,right) x [top,bottom). FillRectangle with
    ' smoothing off and integral coordinates covers exactly the same pixels, so the
    ' width is the plain difference here -- NOT the -1 that the stroked paths need.
    dim as single w = csng( rc->right - rc->left )
    dim as single h = csng( rc->bottom - rc->top )
    if (w <= 0.0) orelse (h <= 0.0) then return 0

    dim as long oldSmooth = g->GetSmoothingMode()
    g->SetSmoothingMode( SmoothingModeNone )
    g->FillRectangle( this.BrushFor( DBufToARGB( _backcolor, _backalpha ) ), _
                      csng(rc->left), csng(rc->top), w, h )
    g->SetSmoothingMode( oldSmooth )
    _gpDirty = true
    return 0
#else
    ' Non-Brush painting
    dim as COLORREF oldclr = SetBkColor(_memDC, _backcolor)
    ExtTextOut( _memDC, 0, 0, ETO_OPAQUE, rc, "", 0, 0)
    SetBkColor( _memDC, oldclr )
    return 0
#endif
end function

function clsDoubleBuffer.PaintClientRect() as long
    return this.PaintRect( @_rc )
end function

function clsDoubleBuffer.PaintBorderRect( _
            byval rc as RECT ptr, _
            byval nPenWidth as long = 1 _
            ) as long
    return this.PaintRectFactory( rc, PS_SOLID, nPenWidth, 0 )
end function

function clsDoubleBuffer.PaintRoundRect( _
            byval rc as RECT ptr, _
            byval nCurvature as long = 20 _
            ) as long
    return this.PaintRectFactory( rc, PS_NULL, 1, nCurvature )
end function

function clsDoubleBuffer.PaintRoundBorderRect( _
            byval rc as RECT ptr, _
            byval nCurvature as long = 20, _
            byval nPenWidth as long = 1 _
            ) as long
    return this.PaintRectFactory( rc, PS_SOLID, nPenWidth, nCurvature )
end function

function clsDoubleBuffer.PaintText( _
            byval wszText as DWSTRING, _
            byval rc as RECT ptr, _
            byval wsStyle as DWORD _
            ) as long
#ifdef DBUF_IMPL_D2D
    ' ------------------------------------------------------------------------------------
    ' DirectWrite. SCOPE NOTE: this is the working-but-plain version. Phase 4 owns the rest
    ' -- a MeasureText primitive, DT_END_ELLIPSIS via a trimming sign, and the font
    ' descriptor overload. What is here covers alignment, vertical centring and clipping,
    ' which is everything the geometry self-test and the demo actually exercise.
    '
    ' There is no EnsureGdiReady equivalent and there cannot be: GDI+ only needed a flush
    ' to interleave with GDI on a shared HDC, whereas D2D owns the surface between
    ' BeginDraw and EndDraw and GDI drawing into it is undefined. That is precisely why
    ' text had to move to DirectWrite rather than stay on DrawText.
    ' ------------------------------------------------------------------------------------
    if _pRT = 0 then return 0
    if len( wszText ) = 0 then return 0

    dim as IDWriteTextFormat ptr pFmt = DBufD2D_TextFormatFor( _hFont )
    if pFmt = 0 then return 0

    ' DT_* -> DirectWrite. The family only ever passes the horizontal flags plus
    ' DT_END_ELLIPSIS; DT_VCENTER and DT_SINGLELINE are forced on by this method's own
    ' contract, so they are applied unconditionally rather than read from wsStyle.
    if (wsStyle and DT_CENTER) then
        pFmt->SetTextAlignment( DWRITE_TEXT_ALIGNMENT_CENTER )
    elseif (wsStyle and DT_RIGHT) then
        pFmt->SetTextAlignment( DWRITE_TEXT_ALIGNMENT_TRAILING )
    else
        pFmt->SetTextAlignment( DWRITE_TEXT_ALIGNMENT_LEADING )
    end if
    pFmt->SetParagraphAlignment( DWRITE_PARAGRAPH_ALIGNMENT_CENTER )   ' = DT_VCENTER
    pFmt->SetWordWrapping( DWRITE_WORD_WRAPPING_NO_WRAP )              ' = DT_SINGLELINE

    ' DT_END_ELLIPSIS. Stated on EVERY call, in both directions -- text formats are cached
    ' and shared, so leaving trimming set would make later captions in the same font
    ' silently sprout "..." (see DBufD2D_SetEllipsis).
    dim as boolean bEllipsis = ((wsStyle and DT_END_ELLIPSIS) <> 0)
    DBufD2D_SetEllipsis( pFmt, bEllipsis )

    dim as ID2D1SolidColorBrush ptr pB = this.BrushFor( _forecolor, _forealpha )
    if pB = 0 then return 0

    dim as D2D1_RECT_F layout = type<D2D1_RECT_F>( csng(rc->left), csng(rc->top), _
                                                   csng(rc->right), csng(rc->bottom) )
    ' CLIP is not the default in D2D the way it is in DrawText: without it a long string
    ' simply overflows its rect and paints over the next cell. It stays on even when
    ' trimming is active -- trimming decides where the "..." goes, clipping is what stops
    ' a glyph that still overhangs by a fraction from bleeding into the neighbour.
    ' Measuring mode must match the rendering mode set on the target -- see the note in
    ' dbufD2D.bi. GDI-hinted glyphs at DirectWrite positions look worse than either.
    _pRT->DrawText( wszText, culng( len(wszText) ), pFmt, layout, _
                    cast( ID2D1Brush ptr, pB ), _
                    D2D1_DRAW_TEXT_OPTIONS_CLIP, DBufD2D_MeasuringMode() )
    function = 0
#else
    #ifdef DBUF_IMPL_GDIPLUS
        this.EnsureGdiReady()
    #endif

    dim as COLORREF oldforecolor = SetTextColor( _memDC, _forecolor )
    SetBkMode(_memDC, TRANSPARENT)

    dim as HFONT hOldFont
    if _hFont then hOldFont = SelectObject( _memDC, _hFont )

    wsStyle = wsStyle or DT_NOPREFIX or DT_VCENTER or DT_SINGLELINE
    DrawText( _memDC, wszText, -1, cast(lpRect, rc), wsStyle )

    SetTextColor( _memDC, oldforecolor )
    if _hFont then SelectObject( _memDC, hOldFont )

    function = 0
#endif
end function

function clsDoubleBuffer.PaintChar( _
            byval wszChar as DWSTRING, _
            byval rc as RECT ptr, _
            byval forecolor as COLORREF _
            ) as long

#ifdef DBUF_IMPL_D2D
    ' A glyph from the icon font. This is the call that would silently render the WRONG
    ' character if the private font collection had not been registered with DirectWrite --
    ' see the FR_PRIVATE note in dbufD2D.bi.
    if _pRT = 0 then return 0
    if len( wszChar ) = 0 then return 0

    dim as IDWriteTextFormat ptr pFmt = DBufD2D_TextFormatFor( _hFont )
    if pFmt = 0 then return 0

    pFmt->SetTextAlignment( DWRITE_TEXT_ALIGNMENT_LEADING )
    pFmt->SetParagraphAlignment( DWRITE_PARAGRAPH_ALIGNMENT_CENTER )
    pFmt->SetWordWrapping( DWRITE_WORD_WRAPPING_NO_WRAP )
    ' A glyph must never trim. Stated explicitly because the format is shared and some
    ' earlier PaintText may have left trimming on -- an icon replaced by "..." would be a
    ' baffling bug to chase.
    DBufD2D_SetEllipsis( pFmt, false )

    dim as ID2D1SolidColorBrush ptr pB = this.BrushFor( forecolor, 255 )
    if pB = 0 then return 0

    dim as D2D1_RECT_F layout = type<D2D1_RECT_F>( csng(rc->left), csng(rc->top), _
                                                   csng(rc->right), csng(rc->bottom) )
    ' NONE, not CLIP: the GDI path passes DT_NOCLIP here, because a glyph is routinely
    ' given a rect narrower than its own advance and is meant to overhang.
    _pRT->DrawText( wszChar, culng( len(wszChar) ), pFmt, layout, _
                    cast( ID2D1Brush ptr, pB ), _
                    D2D1_DRAW_TEXT_OPTIONS_NONE, DBufD2D_MeasuringMode() )
    function = 0
#else
    #ifdef DBUF_IMPL_GDIPLUS
        this.EnsureGdiReady()
    #endif

    dim as HFONT hOldFont
    if _hFont then hOldFont = SelectObject( _memDC, _hFont )

    SetBkMode(_memDC, TRANSPARENT)
    SetTextColor(_memDC, forecolor)

    dim as DWORD wsStyle = DT_SINGLELINE or DT_VCENTER or DT_LEFT or DT_NOCLIP
    DrawText( _memDC, wszChar, -1, cast(lpRect, rc), wsStyle )

    if _hFont then SelectObject( _memDC, hOldFont )

    function = 0
#endif
end function

' ========================================================================================
' Stroke a rounded rect, leaving the interior untouched. Same geometry contract as
' PaintRectFactory -- see the fill/stroke path split there for why the stroke sits half a
' pixel inside the fill boundary.
' ========================================================================================
function clsDoubleBuffer.PaintRoundOutline( _
            byval rc as RECT ptr, _
            byval nCurvature as long = 20, _
            byval nPenWidth as long = 1 _
            ) as long
    if nPenWidth <= 0 then return 0

#ifdef DBUF_IMPL_D2D
    if _pRT = 0 then return 0

    dim as single w = csng( rc->right - rc->left - 1 )
    dim as single h = csng( rc->bottom - rc->top - 1 )
    if (w < 0.0) orelse (h < 0.0) then return 0

    dim as single fpw    = csng( nPenWidth )
    dim as single inset  = (fpw - 1.0) / 2.0
    dim as single radius = csng( nCurvature ) / 2.0
    if radius > (w / 2.0) then radius = w / 2.0
    if radius > (h / 2.0) then radius = h / 2.0
    if radius < 0.0 then radius = 0.0

    if nCurvature > 0 then
        _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE )
    else
        _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_ALIASED )
    end if

    dim as ID2D1SolidColorBrush ptr pPen = this.BrushFor( _pencolor, _penalpha )
    if pPen then
        dim as D2D1_ROUNDED_RECT rs
        rs.rect = type<D2D1_RECT_F>( _
                    csng(rc->left) + inset + 0.5, csng(rc->top) + inset + 0.5, _
                    csng(rc->left) + w - inset + 0.5, csng(rc->top) + h - inset + 0.5 )
        rs.radiusX = radius
        rs.radiusY = radius
        _pRT->DrawRoundedRectangle( rs, cast( ID2D1Brush ptr, pPen ), fpw, NULL )
    end if

    _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE )
    function = 0
#elseif defined(DBUF_IMPL_GDIPLUS)
    dim as CGpGraphics ptr g = this.EnsureGraphics()
    if g = 0 then return 0

    dim as single w = csng( rc->right - rc->left - 1 )
    dim as single h = csng( rc->bottom - rc->top - 1 )
    if (w < 0.0) orelse (h < 0.0) then return 0

    dim as single fpw   = csng( nPenWidth )
    dim as single inset = (fpw - 1.0) / 2.0

    dim as long oldSmooth = g->GetSmoothingMode()
    if nCurvature > 0 then
        g->SetSmoothingMode( SmoothingModeAntiAlias )
    else
        g->SetSmoothingMode( SmoothingModeNone )
    end if

    dim as CGpGraphicsPath strokePath
    this.BuildRoundPath( @strokePath, _
                         csng(rc->left) + inset + 0.5, csng(rc->top) + inset + 0.5, _
                         w - (inset * 2.0), h - (inset * 2.0), _
                         csng(nCurvature) / 2.0 )
    g->DrawPath( this.PenFor( DBufToARGB( _pencolor, _penalpha ), fpw ), @strokePath )

    g->SetSmoothingMode( oldSmooth )
    _gpDirty = true
    function = 0
#else
    dim as HPEN hPen = CreatePen( PS_SOLID, nPenWidth, _pencolor )
    dim as HGDIOBJ hOldPen   = SelectObject( _memDC, hPen )
    dim as HGDIOBJ hOldBrush = SelectObject( _memDC, GetStockObject( NULL_BRUSH ) )
    RoundRect( _memDC, rc->left, rc->top, rc->right, rc->bottom, nCurvature, nCurvature )
    SelectObject( _memDC, hOldBrush )
    SelectObject( _memDC, hOldPen )
    DeleteObject( hPen )
    function = 0
#endif
end function

' ========================================================================================
' Filled ellipse; strokes the rim too when nPenWidth > 0. Under GDI+ the rim is
' antialiased, which is the whole reason this exists as a primitive rather than being
' left to each control to hand-roll.
' ========================================================================================
function clsDoubleBuffer.PaintEllipse( _
            byval rc as RECT ptr, _
            byval nPenWidth as long = 0 _
            ) as long
#ifdef DBUF_IMPL_D2D
    if _pRT = 0 then return 0

    ' The same -1 the rounded rect takes, and for the same measured reason: GDI's
    ' Ellipse(10,5,30,25) does not fill through x=29, it stops at 28. Asserting the
    ' "obvious" full extent here made GDI+ diverge from GDI by a pixel during that
    ' refactor, and only running the same assertions on the GDI backend caught it.
    dim as single w = csng( rc->right - rc->left - 1 )
    dim as single h = csng( rc->bottom - rc->top - 1 )
    if (w < 0.0) orelse (h < 0.0) then return 0

    ' D2D takes a CENTRE and two radii where GDI takes a bounding box.
    dim as D2D1_ELLIPSE el
    el.point.x = csng(rc->left) + w / 2.0
    el.point.y = csng(rc->top)  + h / 2.0
    el.radiusX = w / 2.0
    el.radiusY = h / 2.0

    _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE )

    dim as ID2D1SolidColorBrush ptr pFill = this.BrushFor( _backcolor, _backalpha )
    if pFill then _pRT->FillEllipse( el, cast( ID2D1Brush ptr, pFill ) )

    if nPenWidth > 0 then
        dim as ID2D1SolidColorBrush ptr pPen = this.BrushFor( _pencolor, _penalpha )
        if pPen then
            dim as single fpw   = csng( nPenWidth )
            dim as single inset = (fpw - 1.0) / 2.0
            dim as D2D1_ELLIPSE es
            es.point.x = el.point.x + 0.5
            es.point.y = el.point.y + 0.5
            es.radiusX = el.radiusX - inset
            es.radiusY = el.radiusY - inset
            _pRT->DrawEllipse( es, cast( ID2D1Brush ptr, pPen ), fpw, NULL )
        end if
    end if

    function = 0
#elseif defined(DBUF_IMPL_GDIPLUS)
    dim as CGpGraphics ptr g = this.EnsureGraphics()
    if g = 0 then return 0

    ' MEASURED, and it contradicts the obvious reading: GDI's Ellipse(10,5,30,25) does not
    ' fill through x=29. It stops at 28, exactly as RoundRect stops a row short of its own
    ' rect -- so the ellipse takes the same -1 the rounded rect does, NOT the full extent
    ' that PaintRect uses. Asserting the "obvious" full extent here made GDI+ diverge from
    ' GDI by a pixel, and only the GDI backend running the same assertions caught it.
    dim as single w = csng( rc->right - rc->left - 1 )
    dim as single h = csng( rc->bottom - rc->top - 1 )
    if (w < 0.0) orelse (h < 0.0) then return 0

    dim as long oldSmooth = g->GetSmoothingMode()
    g->SetSmoothingMode( SmoothingModeAntiAlias )

    g->FillEllipse( this.BrushFor( DBufToARGB( _backcolor, _backalpha ) ), _
                    csng(rc->left), csng(rc->top), w, h )

    if nPenWidth > 0 then
        dim as single fpw   = csng( nPenWidth )
        dim as single inset = (fpw - 1.0) / 2.0
        g->DrawEllipse( this.PenFor( DBufToARGB( _pencolor, _penalpha ), fpw ), _
                        csng(rc->left) + inset + 0.5, csng(rc->top) + inset + 0.5, _
                        w - (inset * 2.0), h - (inset * 2.0) )
    end if

    g->SetSmoothingMode( oldSmooth )
    _gpDirty = true
    function = 0
#else
    ' Held as HGDIOBJ, not HPEN: the stock NULL_PEN comes back from GetStockObject as
    ' HGDIOBJ and casting it to HPEN is a syntax error here.
    dim as HGDIOBJ hPenObj
    dim as boolean bOwnPen = false
    if nPenWidth > 0 then
        hPenObj = CreatePen( PS_SOLID, nPenWidth, _pencolor )
        bOwnPen = true
    else
        hPenObj = GetStockObject( NULL_PEN )    ' stock: must NOT be deleted
    end if
    dim as HBRUSH hBrush = CreateSolidBrush( _backcolor )
    dim as HGDIOBJ hOldPen   = SelectObject( _memDC, hPenObj )
    dim as HGDIOBJ hOldBrush = SelectObject( _memDC, hBrush )
    Ellipse( _memDC, rc->left, rc->top, rc->right, rc->bottom )
    SelectObject( _memDC, hOldBrush )
    SelectObject( _memDC, hOldPen )
    DeleteObject( hBrush )
    if bOwnPen then DeleteObject( hPenObj )
    function = 0
#endif
end function

function clsDoubleBuffer.PaintIconButton( _
            byval wszText as DWSTRING, _
            byval rc as RECT ptr, _
            byval nCurvature as long = 20 _
            ) as long
    this.PaintRoundRect( rc, nCurvature )
    this.PaintText( wszText, rc, DT_CENTER )

    function = 0
end function

' ========================================================================================
' A straight line, nWidth units thick (DPI-scaled here, as it always has been).
'
' ONE DELIBERATE DIFFERENCE FROM THE GDI BACKEND, measured not guessed: with a pen
' THICKER than 1px, GDI's LineTo over-runs its endpoint by a pixel (a 2px rule from
' x=10 to x=40 painted 10..40 inclusive, where the 1px rule painted 10..39). That is a
' thick-pen end-cap artifact, not intent, and it is not reproduced here -- a 2px rule
' now ends where a 1px rule does. Two callers pass width 2, both full-width dividers
' (frmMain's menu underline and frmTopTabsMenu's), so the visible change is one pixel at
' the right end of each. Expect it in the A/B screenshot diff.
'
' Every caller in the family draws an axis-aligned rule -- a divider or a separator --
' and those are painted as a FILLED RECTANGLE rather than a GDI+ line. Two reasons, both
' learned the expensive way in this refactor:
'   * A 1px horizontal rule drawn with DrawLine and antialiasing on comes out grey and
'     soft, because the pen straddles two pixel rows.
'   * GDI's LineTo EXCLUDES its endpoint while GDI+ DrawLine INCLUDES it, so a direct
'     translation lengthens every divider by one pixel.
' A rectangle has neither problem: the extent is stated, not inferred.
'
' The diagonal branch is currently unreached (no caller draws one) but is the case that
' actually wants antialiasing, so it is implemented rather than left to fail silently.
' ========================================================================================
function clsDoubleBuffer.PaintLine( _
            byval nWidth as long, _
            byval nLeft as long, _
            byval nTop as long, _
            byval nRight as long, _
            byval nBottom as long _
            ) as long
    dim pWindow as CWindow ptr = AfxCWindowPtr(_hwnd)
    if pWindow = 0 then exit function
    dim as long penWidth = pWindow->ScaleY(nWidth)
    if penWidth < 1 then penWidth = 1

#ifdef DBUF_IMPL_D2D

    if _pRT = 0 then return 0

    ' Axis-aligned rules are painted as a FILLED RECTANGLE, not a line, for the two
    ' reasons the GDI+ path learned the expensive way and which apply identically here:
    ' an antialiased 1px horizontal rule straddles two pixel rows and comes out grey, and
    ' GDI's LineTo excludes its endpoint while D2D's DrawLine includes it. A rectangle has
    ' neither problem -- the extent is stated rather than inferred.
    if (nTop = nBottom) orelse (nLeft = nRight) then
        dim as ID2D1SolidColorBrush ptr pB = this.BrushFor( _pencolor, _penalpha )
        if pB = 0 then return 0
        _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_ALIASED )
        dim as D2D1_RECT_F r
        if nTop = nBottom then
            dim as long y0 = nTop - (penWidth \ 2)      ' GDI centres the pen on the path
            r = type<D2D1_RECT_F>( csng(nLeft), csng(y0), csng(nRight), csng(y0 + penWidth) )
        else
            dim as long x0 = nLeft - (penWidth \ 2)
            r = type<D2D1_RECT_F>( csng(x0), csng(nTop), csng(x0 + penWidth), csng(nBottom) )
        end if
        _pRT->FillRectangle( r, cast( ID2D1Brush ptr, pB ) )
        _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE )
    else
        ' Diagonal: the one case that genuinely wants antialiasing. Currently unreached --
        ' no caller in the family draws one -- but implemented rather than left to fail.
        dim as ID2D1SolidColorBrush ptr pB = this.BrushFor( _pencolor, _penalpha )
        if pB = 0 then return 0
        _pRT->SetAntialiasMode( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE )
        _pRT->DrawLine( type<D2D1_POINT_2F>( csng(nLeft), csng(nTop) ), _
                        type<D2D1_POINT_2F>( csng(nRight), csng(nBottom) ), _
                        cast( ID2D1Brush ptr, pB ), csng(penWidth), NULL )
    end if

    function = 0

#elseif defined(DBUF_IMPL_GDIPLUS)

    dim as CGpGraphics ptr g = this.EnsureGraphics()
    if g = 0 then return 0

    dim as long oldSmooth = g->GetSmoothingMode()

    if nTop = nBottom then
        ' Horizontal. GDI centres the pen on the path, so replicate that offset exactly.
        dim as long y0 = nTop - (penWidth \ 2)
        g->SetSmoothingMode( SmoothingModeNone )
        g->FillRectangle( this.BrushFor( DBufToARGB( _pencolor, _penalpha ) ), _
                          csng(nLeft), csng(y0), csng(nRight - nLeft), csng(penWidth) )
    elseif nLeft = nRight then
        ' Vertical.
        dim as long x0 = nLeft - (penWidth \ 2)
        g->SetSmoothingMode( SmoothingModeNone )
        g->FillRectangle( this.BrushFor( DBufToARGB( _pencolor, _penalpha ) ), _
                          csng(x0), csng(nTop), csng(penWidth), csng(nBottom - nTop) )
    else
        ' Diagonal: this is the one that genuinely benefits from antialiasing.
        g->SetSmoothingMode( SmoothingModeAntiAlias )
        g->DrawLine( this.PenFor( DBufToARGB( _pencolor, _penalpha ), csng(penWidth) ), _
                     csng(nLeft), csng(nTop), csng(nRight), csng(nBottom) )
    end if

    g->SetSmoothingMode( oldSmooth )
    _gpDirty = true
    function = 0

#else

    dim as HPEN hPenSolid = CreatePen( PS_SOLID, penWidth, _pencolor )
    dim as HPEN hOldPen = SelectObject( _memDC, hPenSolid)
    MoveToEx( _memDC, nLeft, nTop, null )
    LineTo( _memDC, nRight, nBottom )
    DeleteObject( SelectObject(_memDC, hOldPen) )
    function = 0

#endif
end function


' ========================================================================================
' Geometry self-test, run with DBUF_SELFTEST=1.
'
' Every expected number below was MEASURED off the GDI backend, not derived from what the
' documentation implies GDI does -- the two disagreed twice while this was being written
' (a stroke landing one column left, and RoundRect's fill stopping a row short of its
' own rect). Renders through the real public methods into an offscreen surface and reads
' the pixels back, so it asserts the rendering rather than the arithmetic behind it.
'
' Reports assertions RUN as well as failed: a harness that only counts failures cannot
' tell "all passed" from "barely executed" (Learnings.md).
' ========================================================================================
namespace DBufSelfTest
    dim shared as long gRun, gFail
    dim shared as HDC     gDC
    dim shared as HBITMAP gBmp

    const SW = 64
    const SH = 40
    const CBACK = BGR( 0, 0, 0 )
    const CFILL = BGR( 200, 100, 50 )
    const CPEN  = BGR( 10, 220, 240 )

    private sub Chk( byval sName as string, byval bOk as boolean, byval sDetail as string )
        gRun += 1
        dim as string s
        if bOk then s = "PASS  " else s = "FAIL  " : gFail += 1
        s &= sName & "   [" & sDetail & "]"
        print s
    end sub

    private sub ClearSurf()
        dim as RECT rc = type( 0, 0, SW, SH )
        dim as HBRUSH hbr = CreateSolidBrush( CBACK )
        FillRect( gDC, @rc, hbr )
        DeleteObject( hbr )
    end sub

    ' First and last non-background pixel across one row.
    private sub RowSpan( byval y as long, byref firstx as long, byref lastx as long )
        firstx = -1 : lastx = -1
        for x as long = 0 to SW - 1
            if GetPixel( gDC, x, y ) <> CBACK then
                if firstx = -1 then firstx = x
                lastx = x
            end if
        next
    end sub

    ' First and last non-background pixel down one column.
    private sub ColSpan( byval x as long, byref firsty as long, byref lasty as long )
        firsty = -1 : lasty = -1
        for y as long = 0 to SH - 1
            if GetPixel( gDC, x, y ) <> CBACK then
                if firsty = -1 then firsty = y
                lasty = y
            end if
        next
    end sub

    ' Distinct colours in a region. Exactly 2 means hard edges; more means antialiasing
    ' actually blended, rather than merely being switched on.
    private function ToneCount( byval x0 as long, byval y0 as long, byval x1 as long, byval y1 as long ) as long
        dim as ulong seen(0 to 255)
        dim as long n = 0
        for y as long = y0 to y1
            for x as long = x0 to x1
                dim as ulong p = GetPixel( gDC, x, y )
                dim as boolean found = false
                for i as long = 0 to n - 1
                    if seen(i) = p then found = true : exit for
                next
                if (found = false) andalso (n < 256) then seen(n) = p : n += 1
            next
        next
        return n
    end function
end namespace

sub clsDoubleBuffer_RunSelfTest( byval hwnd as HWND )
    using DBufSelfTest

    print ""
    ' Label taken from DBUF_BACKEND_NAME, which is derived from the same switch the code
    ' is. It used to be a two-way #ifdef/#else, which meant the D2D build fell into the
    ' #else and reported itself as "GDI" -- a self-test lying about what it just tested,
    ' which is the one thing a self-test must never do.
    print "=== clsDoubleBuffer geometry self-test  (backend: " & DBUF_BACKEND_NAME & ") ==="

    gRun = 0 : gFail = 0

    ' A CWindow must sit behind the HWND: PaintLine asks it for the DPI scale.
    if AfxCWindowPtr( hwnd ) = 0 then
        print "FAIL  precondition: hwnd has no CWindow   [PaintLine cannot run]"
        print "=== 0 of 1 assertions passed ==="
        exit sub
    end if

    dim as HDC hScreen = GetDC( null )
    gDC  = CreateCompatibleDC( hScreen )
    gBmp = CreateCompatibleBitmap( hScreen, SW, SH )
    SelectObject( gDC, gBmp )
    ReleaseDC( null, hScreen )

    dim as RECT rcAll = type( 0, 0, SW, SH )
    dim as long a, b2

    ' --- PaintRect: fills [left,right) x [top,bottom), same as ExtTextOut/ETO_OPAQUE ----
    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        bb.SetBackColor( CBACK ) : bb.PaintClientRect()
        dim as RECT r = type( 10, 5, 40, 20 )
        bb.SetBackColor( CFILL ) : bb.PaintRect( @r )
        bb.EndDoubleBuffer()
    end scope
    RowSpan( 10, a, b2 )
    Chk( "PaintRect: right edge exclusive", (a = 10) andalso (b2 = 39), "x " & a & ".." & b2 )
    ColSpan( 20, a, b2 )
    Chk( "PaintRect: bottom edge exclusive", (a = 5) andalso (b2 = 19), "y " & a & ".." & b2 )
    Chk( "PaintRect: colour survives BGR->ARGB", (GetPixel( gDC, 20, 10 ) = CFILL), _
         "want " & hex(CFILL,6) & " got " & hex(GetPixel(gDC,20,10),6) )

    ' --- PaintBorderRect: a 1px stroke sits ON the rect edge, not half a pixel off. -----
    '     This is the one that was wrong first time round.
    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        bb.SetBackColor( CBACK ) : bb.PaintClientRect()
        dim as RECT r = type( 10, 5, 40, 20 )
        bb.SetBackColor( CFILL ) : bb.SetPenColor( CPEN )
        bb.PaintBorderRect( @r, 1 )
        bb.EndDoubleBuffer()
    end scope
    RowSpan( 5, a, b2 )
    Chk( "Border: top edge on the rect, x span", (a = 10) andalso (b2 = 39), "x " & a & ".." & b2 )
    ColSpan( 20, a, b2 )
    Chk( "Border: y span on the rect", (a = 5) andalso (b2 = 19), "y " & a & ".." & b2 )
    Chk( "Border: stroke uses the pen colour", (GetPixel( gDC, 20, 5 ) = CPEN), _
         "want " & hex(CPEN,6) & " got " & hex(GetPixel(gDC,20,5),6) )

    ' --- PaintRoundRect, curvature 20. THE trap: GDI's curvature is an ellipse DIAMETER
    '     and a GDI+ arc takes a RADIUS. Halving wrongly doubles every corner, which
    '     still looks deliberate -- so the corner is measured, not eyeballed. -----------
    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        bb.SetBackColor( CBACK ) : bb.PaintClientRect()
        dim as RECT r = type( 10, 5, 50, 35 )      ' 40 x 30
        bb.SetBackColor( CFILL ) : bb.PaintRoundRect( @r, 20 )
        bb.EndDoubleBuffer()
    end scope
    ' Vertical mid-line: RoundRect (unlike PaintRect) stops a row short of its own
    ' bottom -- GDI does this too, and the GDI+ path matches it deliberately.
    ColSpan( 30, a, b2 )
    Chk( "RoundRect: vertical extent matches GDI", (a = 5) andalso (b2 = 33), "y " & a & ".." & b2 )
    ' Mid-height row must reach the full left/right edges (the straight part).
    RowSpan( 20, a, b2 )
    Chk( "RoundRect: mid-row spans full width", (a = 10) andalso (b2 = 48), "x " & a & ".." & b2 )
    ' With radius 10 the corner has bitten well into the top row; with radius 20
    ' (the un-halved bug) the shape degenerates and this row starts much further right.
    RowSpan( 6, a, b2 )
    Chk( "RoundRect: corner radius ~10 not ~20", (a >= 11) andalso (a <= 17), "row6 starts x=" & a )

    #ifdef DBUF_HAS_ANTIALIAS
    Chk( "RoundRect: corner is antialiased", (ToneCount( 8, 3, 24, 19 ) > 2), _
         "distinct tones = " & ToneCount( 8, 3, 24, 19 ) )
    #endif

    ' --- PaintLine: axis-aligned rules must stay hard-edged and keep GDI's exclusive
    '     endpoint. An antialiased 1px divider goes grey, which reads as a theme bug.
    '
    ' THESE ASSERTIONS ARE DPI-DERIVED, and they did not used to be. PaintLine is the only
    ' primitive that scales its input -- its nWidth is in unscaled units and it applies
    ' CWindow.ScaleY -- so "1 unit" is 1 pixel at 96 DPI and 2 at 168. The expectations
    ' were written as literal pixel counts, which silently encoded "the process is DPI
    ' unaware". Adding the manifest made the process see its real 168 DPI and all five of
    ' these failed at once: correct rendering, stale test. They now compute the expected
    ' thickness through the SAME ScaleY the code uses, so they hold at any DPI.
    ' The same CWindow PaintLine itself asks -- the precondition check at the top of this
    ' routine already established it is non-null.
    dim as CWindow ptr pWin = AfxCWindowPtr( hwnd )
    dim as long pw1 = pWin->ScaleY( 1 ) : if pw1 < 1 then pw1 = 1
    dim as long pw2 = pWin->ScaleY( 2 ) : if pw2 < 1 then pw2 = 1

    ' GDI centres the pen on the path: the run starts (width \ 2) before the coordinate.
    dim as long h1top = 8 - (pw1 \ 2)
    dim as long h2top = 8 - (pw2 \ 2)

    ' One measured GDI quirk the other backends deliberately do not reproduce: with a pen
    ' THICKER than 1px, LineTo over-runs its endpoint by a pixel. GDI+ and D2D paint the
    ' rule as a stated-extent rectangle and stop where asked.
    dim as long hRight = 39
    #ifdef DBUF_IMPL_GDI
        if pw1 > 1 then hRight = 40
    #endif

    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        bb.SetBackColor( CBACK ) : bb.PaintClientRect()
        bb.SetPenColor( CPEN ) : bb.PaintLine( 1, 10, 8, 40, 8 )
        bb.EndDoubleBuffer()
    end scope
    RowSpan( 8, a, b2 )
    Chk( "HLine w1: endpoint exclusive", (a = 10) andalso (b2 = hRight), _
         "x " & a & ".." & b2 & "  (expected ..ns" & hRight & " at scaled pen " & pw1 & ")" )
    ColSpan( 20, a, b2 )
    Chk( "HLine w1: exactly " & pw1 & " row(s) thick", _
         (a = h1top) andalso (b2 = h1top + pw1 - 1), _
         "y " & a & ".." & b2 & "  (scaled pen " & pw1 & ")" )
    Chk( "HLine w1: NOT antialiased", (ToneCount( 8, 6 - pw1, 42, 10 + pw1 ) = 2), _
         "distinct tones = " & ToneCount( 8, 6 - pw1, 42, 10 + pw1 ) )

    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        bb.SetBackColor( CBACK ) : bb.PaintClientRect()
        bb.SetPenColor( CPEN ) : bb.PaintLine( 2, 10, 8, 40, 8 )
        bb.EndDoubleBuffer()
    end scope
    ColSpan( 20, a, b2 )
    Chk( "HLine w2: pen centred like GDI", _
         (a = h2top) andalso (b2 = h2top + pw2 - 1), _
         "y " & a & ".." & b2 & "  (scaled pen " & pw2 & ")" )

    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        bb.SetBackColor( CBACK ) : bb.PaintClientRect()
        bb.SetPenColor( CPEN ) : bb.PaintLine( 1, 20, 5, 20, 30 )
        bb.EndDoubleBuffer()
    end scope
    dim as long vBot = 29
    #ifdef DBUF_IMPL_GDI
        if pw1 > 1 then vBot = 30
    #endif
    dim as long vLeft = 20 - (pw1 \ 2)
    ColSpan( 20, a, b2 )
    Chk( "VLine w1: endpoint exclusive", (a = 5) andalso (b2 = vBot), _
         "y " & a & ".." & b2 & "  (expected ..ns" & vBot & ")" )
    RowSpan( 10, a, b2 )
    Chk( "VLine w1: exactly " & pw1 & " column(s) thick", _
         (a = vLeft) andalso (b2 = vLeft + pw1 - 1), _
         "x " & a & ".." & b2 & "  (scaled pen " & pw1 & ")" )

    ' --- Interleave: GDI+ batches, GDI does not. A shape drawn through GDI+ must still
    '     be on the surface after a GDI text call follows it. Without the flush in
    '     PaintText this is exactly what goes missing, intermittently. ----------------
    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        bb.SetBackColor( CBACK ) : bb.PaintClientRect()
        dim as RECT r = type( 10, 5, 40, 20 )
        bb.SetBackColor( CFILL ) : bb.PaintRect( @r )
        dim as RECT rt = type( 12, 6, 38, 18 )
        bb.SetForeColor( CPEN ) : bb.PaintText( "x", @rt, DT_LEFT )
        bb.EndDoubleBuffer()
    end scope
    RowSpan( 10, a, b2 )
    Chk( "interleave: fill survives a text call", (a = 10) andalso (b2 = 39), _
         "x " & a & ".." & b2 )

    ' --- PaintEllipse: the knob primitive. Must be round, centred, and antialiased. -----
    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        bb.SetBackColor( CBACK ) : bb.PaintClientRect()
        dim as RECT r = type( 10, 5, 30, 25 )        ' 20 x 20 circle
        bb.SetBackColor( CFILL ) : bb.PaintEllipse( @r, 0 )
        bb.EndDoubleBuffer()
    end scope
    ' 10..28 and 5..23, NOT 10..29 / 5..24: GDI's Ellipse stops one short of its own rect,
    ' the same way RoundRect does. These numbers came off the GDI backend.
    RowSpan( 15, a, b2 )         ' widest row, through the centre
    Chk( "Ellipse: widest row matches GDI", (a = 10) andalso (b2 = 28), "x " & a & ".." & b2 )
    ColSpan( 20, a, b2 )
    Chk( "Ellipse: tallest column matches GDI", (a = 5) andalso (b2 = 23), "y " & a & ".." & b2 )
    ' A circle must be narrower near its top than at its middle -- that it is an ellipse
    ' at all, rather than the bounding box getting filled.
    dim as long topFirst, topLast
    RowSpan( 7, topFirst, topLast )
    Chk( "Ellipse: is round, not a filled box", (topLast - topFirst) < 18, _
         "row7 width = " & (topLast - topFirst + 1) & " vs mid 20" )
    #ifdef DBUF_HAS_ANTIALIAS
    Chk( "Ellipse: rim is antialiased", (ToneCount( 8, 3, 32, 27 ) > 2), _
         "distinct tones = " & ToneCount( 8, 3, 32, 27 ) )
    #endif

    ' --- PaintRoundOutline: strokes WITHOUT filling. The interior must survive. ---------
    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        bb.SetBackColor( CBACK ) : bb.PaintClientRect()
        dim as RECT r = type( 10, 5, 50, 30 )
        bb.SetBackColor( CFILL ) : bb.PaintRect( @r )      ' something underneath
        bb.SetPenColor( CPEN )   : bb.PaintRoundOutline( @r, 10, 1 )
        bb.EndDoubleBuffer()
    end scope
    Chk( "RoundOutline: interior NOT overpainted", (GetPixel( gDC, 30, 17 ) = CFILL), _
         "centre want " & hex(CFILL,6) & " got " & hex(GetPixel(gDC,30,17),6) )
    Chk( "RoundOutline: edge carries the pen colour", (GetPixel( gDC, 30, 5 ) = CPEN), _
         "top edge want " & hex(CPEN,6) & " got " & hex(GetPixel(gDC,30,5),6) )

    ' --- The CACHED overload: a caller-owned memDC, reused per row by CListBox. It is
    '     the one Begin path that skips SetupBitmap, so it never gets the 32bpp DIB the
    '     others do -- it draws onto whatever surface the host already made. Every list
    '     in the app goes through here once per visible row, so it is asserted rather
    '     than assumed to behave like its siblings. --------------------------------------
    ClearSurf()
    scope
        dim as HDC hScr2 = GetDC( null )
        dim as HDC cacheDC = CreateCompatibleDC( hScr2 )
        dim as HBITMAP cacheBmp = CreateCompatibleBitmap( hScr2, SW, SH )
        dim as HBITMAP oldBmp = SelectObject( cacheDC, cacheBmp )
        ReleaseDC( null, hScr2 )

        dim as RECT rRow = type( 0, 0, SW, 12 )
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rRow, cacheDC )
        bb.SetBackColor( CFILL )
        bb.PaintRect( @rRow )
        dim as RECT rIn = type( 4, 2, 30, 10 )
        bb.SetBackColor( CPEN )
        bb.PaintRoundRect( @rIn, 6 )
        bb.EndDoubleBuffer()

        SelectObject( cacheDC, oldBmp )
        DeleteObject( cacheBmp )
        DeleteDC( cacheDC )
    end scope
    RowSpan( 6, a, b2 )
    Chk( "cached DC: row blits across full width", (a = 0) andalso (b2 = SW - 1), _
         "x " & a & ".." & b2 )
    Chk( "cached DC: inner shape reached the surface", (GetPixel( gDC, 15, 6 ) = CPEN), _
         "want " & hex(CPEN,6) & " got " & hex(GetPixel(gDC,15,6),6) )
    ' Nothing below the row rect may be touched: the blit is bounded by rcItem, not by
    ' the (larger) cached bitmap behind it.
    ColSpan( 15, a, b2 )
    Chk( "cached DC: blit bounded by rcItem", (b2 <= 11), "last row touched = " & b2 )

    ' --- Alpha compositing --------------------------------------------------------------
    ' Asserts the claim DBUF_HAS_ALPHA makes, on whichever backend is running, by MEASURING
    ' the pixel rather than trusting the capability symbol. Fill with one colour, wash a
    ' second over it at 50%, and look at what came out:
    '
    '   composited (GDI+, D2D)  every channel lands strictly BETWEEN the two colours
    '   opaque      (GDI)       the result is exactly the wash colour, alpha ignored
    '
    ' Both are correct behaviour for their backend, so both are asserted -- the failure
    ' this catches is a backend that claims to composite and does not, or one that starts
    ' silently blending when a host expected opaque.
    ClearSurf()
    scope
        dim bb as clsDoubleBuffer
        bb.BeginDoubleBuffer( hwnd, gDC, rcAll )
        dim as RECT r = type( 10, 5, 40, 20 )
        bb.SetBackColor( CFILL ) : bb.PaintRect( @r )
        bb.SetBackColorA( CPEN, 128 ) : bb.PaintRect( @r )
        bb.EndDoubleBuffer()
    end scope
    scope
        ' NOT "fb": FB is FreeBASIC's own namespace, and a local of that name is a syntax
        ' error at its first use rather than at its declaration. Fourth reserved-word
        ' collision in this project after pSet/PSET, trim/TRIM and the family's ok/Ok.
        dim as ulong got = GetPixel( gDC, 20, 10 )
        dim as long gotR =  got         and &hFF
        dim as long gotG = (got  shr  8) and &hFF
        dim as long gotB = (got  shr 16) and &hFF
        dim as long filR =  CFILL       and &hFF
        dim as long filG = (CFILL shr  8) and &hFF
        dim as long filB = (CFILL shr 16) and &hFF
        dim as long wshR =  CPEN        and &hFF
        dim as long wshG = (CPEN  shr  8) and &hFF
        dim as long wshB = (CPEN  shr 16) and &hFF
        dim as string sDetail = "fill " & hex(CFILL,6) & " + wash " & hex(CPEN,6) & _
                                " @50% -> " & hex(got,6)
        #ifdef DBUF_HAS_ALPHA
            ' Between the two, per channel, with a pixel of slack for rounding.
            dim as boolean bMid = true
            if (gotR < min(filR,wshR) - 1) orelse (gotR > max(filR,wshR) + 1) then bMid = false
            if (gotG < min(filG,wshG) - 1) orelse (gotG > max(filG,wshG) + 1) then bMid = false
            if (gotB < min(filB,wshB) - 1) orelse (gotB > max(filB,wshB) + 1) then bMid = false
            dim as boolean bBlended = bMid andalso (got <> CFILL) andalso (got <> CPEN)
            Chk( "alpha: 50% wash composites with what is underneath", bBlended, sDetail )
        #else
            Chk( "alpha: ignored on this backend, wash is opaque", (got = CPEN), sDetail )
        #endif
    end scope

    ' --- MeasureText -------------------------------------------------------------------
    ' Runs on ALL THREE backends and prints the numbers, because the interesting result is
    ' not pass/fail but how far DirectWrite's measurement sits from GDI's. Those two
    ' disagreeing is the whole reason this primitive had to exist; the log is where that
    ' disagreement becomes visible.
    scope
        dim as HFONT hf = CreateFontW( -12, 0, 0, 0, FW_NORMAL, 0, 0, 0, _
                                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI" )
        if hf = 0 then
            Chk( "MeasureText: test font created", false, "CreateFontW failed" )
        else
            dim as SIZE szEmpty = clsDoubleBuffer_MeasureText( "", hf )
            Chk( "MeasureText: empty string measures zero", (szEmpty.cx = 0), _
                 "cx = " & szEmpty.cx )

            dim as SIZE sz1 = clsDoubleBuffer_MeasureText( "M", hf )
            dim as SIZE sz2 = clsDoubleBuffer_MeasureText( "MM", hf )
            dim as SIZE szL = clsDoubleBuffer_MeasureText( "The quick brown fox", hf )

            Chk( "MeasureText: single glyph has width and height", _
                 (sz1.cx > 0) andalso (sz1.cy > 0), "M = " & sz1.cx & "x" & sz1.cy )
            ' Not an exact doubling -- kerning and subpixel advances make it approximate --
            ' but two glyphs must be wider than one and no wider than twice.
            Chk( "MeasureText: width grows with the string", _
                 (sz2.cx > sz1.cx) andalso (sz2.cx <= sz1.cx * 2 + 2), _
                 "M=" & sz1.cx & "  MM=" & sz2.cx )
            Chk( "MeasureText: a sentence is wider than a glyph", (szL.cx > sz1.cx * 3), _
                 "sentence = " & szL.cx & "x" & szL.cy )
            ' Printed, not asserted: this is the cross-backend comparison. A GDI run and a
            ' D2D run put different numbers here, and the difference is the finding.
            print "      [measure] 'The quick brown fox' @ Segoe UI 12px = " & _
                  szL.cx & " x " & szL.cy & "   (backend: " & DBUF_BACKEND_NAME & ")"
            DeleteObject( hf )
        end if
    end scope

    ' --- TEXT CRISPNESS, measured ------------------------------------------------------
    ' "Which looks crisper" is a judgement; the thing behind it is not. A rasterizer that
    ' hints stems onto the pixel grid lands a stem's core on ONE column, leaving fully
    ' saturated pixels with a thin blended edge. One that positions at subpixel offsets
    ' spreads that stem across two columns, so almost none of its ink reaches full
    ' saturation. The count of SOLID pixels is therefore a direct proxy for crispness, and
    ' it is the number that separates the modes -- "partial coverage" barely does, because
    ' ClearType fringes make nearly every edge pixel coloured whatever the mode.
    '
    ' Runs on EVERY backend so GDI's own DrawText provides the reference the D2D modes are
    ' trying to match. Reported, never asserted: what counts as crisp enough is the
    ' reader's call, and this harness only supplies the arithmetic.
    scope
        const TW = 220, TH = 28
        dim as HDC hScr = GetDC( null )
        dim as HDC tDC  = CreateCompatibleDC( hScr )
        dim as HBITMAP tBmp = CreateCompatibleBitmap( hScr, TW, TH )
        dim as HBITMAP tOld = SelectObject( tDC, tBmp )
        ReleaseDC( null, hScr )

        ' TWO SIZES, and the second is the one that matters. 12 px is the historical
        ' sample, but a DPI-aware process scales its UI font: at this machine's 168 DPI a
        ' nominal 12 px em is rendered at 21. Measuring only the unscaled size would keep
        ' reporting a regime the user never actually looks at -- which is precisely the
        ' mistake the whole DPI-unaware phase of this project was making.
        dim as long emSizes(0 to 1)
        emSizes(0) = 12
        emSizes(1) = pWin->ScaleY( 12 )
        if emSizes(1) <= emSizes(0) then emSizes(1) = emSizes(0)   ' 96 DPI: same size twice

        print ""
        print "      --- text crispness: solid vs blended pixels ---"

        for si as long = 0 to 1
        if (si = 1) andalso (emSizes(1) = emSizes(0)) then exit for
        dim as long emPx = emSizes(si)
        dim as HFONT hf = CreateFontW( -emPx, 0, 0, 0, FW_NORMAL, 0, 0, 0, _
                                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI" )
        print "      Segoe UI " & emPx & "px em" & _
              iif( si = 1, "   <- the DPI-scaled size actually rendered here", "" )

        dim as long nModes = 1
        #ifdef DBUF_IMPL_D2D
            nModes = DBUF_TEXTMODE_COUNT
            dim as long savedMode = DBufD2D_GetTextMode()
        #endif

        for m as long = 0 to nModes - 1
            dim as string sLabel = DBUF_BACKEND_NAME & " (GDI DrawText)"
            #ifdef DBUF_IMPL_D2D
                DBufD2D_SetTextMode( m )
                sLabel = DBufD2D_TextModeName( m )
            #endif

            dim as RECT rcAllT = type( 0, 0, TW, TH )
            dim as HBRUSH hbrW = CreateSolidBrush( BGR(255,255,255) )
            FillRect( tDC, @rcAllT, hbrW )
            DeleteObject( hbrW )

            scope
                dim bb as clsDoubleBuffer
                bb.BeginDoubleBuffer( hwnd, tDC, rcAllT )
                bb.SetBackColor( BGR(255,255,255) ) : bb.PaintRect( @rcAllT )
                bb.SetFont( hf )
                bb.SetForeColor( BGR(0,0,0) )
                dim as RECT rcT = type( 4, 2, TW - 4, TH - 2 )
                bb.PaintText( "Handgloves Illimitable 0123", @rcT, DT_LEFT )
                bb.EndDoubleBuffer()
            end scope

            dim as long ink = 0, solid = 0
            for y as long = 0 to TH - 1
                for x as long = 0 to TW - 1
                    dim as ulong px = GetPixel( tDC, x, y )
                    if px <> BGR(255,255,255) then
                        ink += 1
                        if px = BGR(0,0,0) then solid += 1
                    end if
                next
            next
            dim as long pct = 0
            if ink > 0 then pct = (solid * 100) \ ink
            dim as string s = "      " & left( sLabel & space(56), 56 )
            s &= "ink=" & right(space(5) & str(ink), 5)
            s &= "  SOLID=" & right(space(5) & str(solid), 5)
            s &= "  solid/ink=" & right(space(3) & str(pct), 3) & "%"
            print s
        next

        #ifdef DBUF_IMPL_D2D
            ' --- Is GDI_CLASSIC actually GDI, or merely close? --------------------------
            ' The counts above match GDI's exactly, which is suggestive but not proof --
            ' two different rasterizations can coincidentally produce the same totals. So
            ' compare PIXEL BY PIXEL against GDI's own DrawText, rendered into the same
            ' bitmap by the same code path. GDI is available inside the D2D build, which
            ' is what makes this comparison possible at all.
            dim as ulong gdiPix( 0 to TW * TH - 1 )
            scope
                dim as RECT rcAllT = type( 0, 0, TW, TH )
                dim as HBRUSH hbrW = CreateSolidBrush( BGR(255,255,255) )
                FillRect( tDC, @rcAllT, hbrW )
                DeleteObject( hbrW )
                dim as HFONT hOldF = SelectObject( tDC, hf )
                SetBkMode( tDC, TRANSPARENT )
                SetTextColor( tDC, BGR(0,0,0) )
                dim as RECT rcT = type( 4, 2, TW - 4, TH - 2 )
                DrawTextW( tDC, "Handgloves Illimitable 0123", -1, cast(lpRect, @rcT), _
                           DT_LEFT or DT_NOPREFIX or DT_VCENTER or DT_SINGLELINE )
                SelectObject( tDC, hOldF )
                for y as long = 0 to TH - 1
                    for x as long = 0 to TW - 1
                        gdiPix( y * TW + x ) = GetPixel( tDC, x, y )
                    next
                next
            end scope

            DBufD2D_SetTextMode( DBUF_TEXTMODE_GDI_CLASSIC )
            scope
                dim as RECT rcAllT = type( 0, 0, TW, TH )
                dim bb as clsDoubleBuffer
                bb.BeginDoubleBuffer( hwnd, tDC, rcAllT )
                bb.SetBackColor( BGR(255,255,255) ) : bb.PaintRect( @rcAllT )
                bb.SetFont( hf )
                bb.SetForeColor( BGR(0,0,0) )
                dim as RECT rcT = type( 4, 2, TW - 4, TH - 2 )
                bb.PaintText( "Handgloves Illimitable 0123", @rcT, DT_LEFT )
                bb.EndDoubleBuffer()
            end scope

            ' Count differing pixels AND how far apart they are per channel. "745 pixels
            ' differ" says nothing on its own -- differing by 2 levels out of 255 is
            ' invisible, differing by 80 is a different-looking rasterization. The
            ' magnitude is the part that answers the question.
            dim as long diff = 0, maxDelta = 0, sumDelta = 0
            for y as long = 0 to TH - 1
                for x as long = 0 to TW - 1
                    dim as ulong a = GetPixel( tDC, x, y )
                    dim as ulong b = gdiPix( y * TW + x )
                    if a <> b then
                        diff += 1
                        dim as long dr = abs( clng( a and &hFF)        - clng( b and &hFF) )
                        dim as long dg = abs( clng((a shr  8) and &hFF) - clng((b shr  8) and &hFF) )
                        dim as long db = abs( clng((a shr 16) and &hFF) - clng((b shr 16) and &hFF) )
                        dim as long dmax = dr
                        if dg > dmax then dmax = dg
                        if db > dmax then dmax = db
                        if dmax > maxDelta then maxDelta = dmax
                        sumDelta += dmax
                    end if
                next
            next
            dim as long avgDelta = 0
            if diff > 0 then avgDelta = sumDelta \ diff

            ' Where does the ink actually sit in each? If the rasterization matches but
            ' the BOX does not, the difference is placement, not quality -- and that is
            ' the 13% line-height discrepancy of F10 showing up as a visible offset,
            ' because DT_VCENTER and DWRITE_PARAGRAPH_ALIGNMENT_CENTER centre against
            ' different definitions of "the height of a line".
            dim as long gTop = -1, gBot = -1, dTop = -1, dBot = -1, gLeft = -1, dLeft = -1
            for y as long = 0 to TH - 1
                for x as long = 0 to TW - 1
                    if gdiPix( y * TW + x ) <> BGR(255,255,255) then
                        if gTop = -1 then gTop = y
                        gBot = y
                        if (gLeft = -1) orelse (x < gLeft) then gLeft = x
                    end if
                    if GetPixel( tDC, x, y ) <> BGR(255,255,255) then
                        if dTop = -1 then dTop = y
                        dBot = y
                        if (dLeft = -1) orelse (x < dLeft) then dLeft = x
                    end if
                next
            next
            print "      GDI_CLASSIC vs GDI DrawText: " & diff & " differing pixels of " & (TW * TH) & _
                  "  (max channel delta " & maxDelta & "/255, mean " & avgDelta & ")"
            print "        GDI ink rows " & gTop & ".." & gBot & "  first ink col " & gLeft
            print "        D2D ink rows " & dTop & ".." & dBot & "  first ink col " & dLeft
            print "        -> vertical offset " & (dTop - gTop) & " px, horizontal " & (dLeft - gLeft) & " px"

            DBufD2D_SetTextMode( savedMode )
        #endif

        if hf then DeleteObject( hf )
        next si

        print "      (HIGHER solid/ink = more fully-saturated stem pixels = crisper)"
        SelectObject( tDC, tOld )
        DeleteObject( tBmp )
        DeleteDC( tDC )
    end scope

    DeleteObject( gBmp )
    DeleteDC( gDC )

    dim as string sSummary = "=== " & (gRun - gFail) & " of " & gRun & " assertions passed"
    if gFail > 0 then sSummary &= "  (" & gFail & " FAILED)"
    sSummary &= " ==="
    print sSummary
end sub
