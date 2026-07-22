'    direct-test - Direct2D / DirectWrite proof of concept
'    Copyright (C) 2016-2026 Paul Squires, PlanetSquires Software
'
'    This program is free software: you can redistribute it and/or modify
'    it under the terms of the GNU General Public License as published by
'    the Free Software Foundation, either version 3 of the License, or
'    (at your option) any later version.

#pragma once

' ========================================================================================
' DIRECT2D / DIRECTWRITE DEVICE AND RESOURCE MANAGEMENT
'
' Everything here is process- or window-lifetime state. It is deliberately NOT in
' clsDoubleBuffer.inc: that file is the buffer's drawing methods, and a buffer is a
' per-WM_PAINT stack object, whereas a render target must outlive hundreds of them. Mixing
' the two concerns in one 1000-line file was how the question "who owns this?" got hard to
' answer in the first place.
'
' Only compiled under DBUF_IMPL_D2D.
'
' ----------------------------------------------------------------------------------------
' WHY A REGISTRY AND NOT A MEMBER
'
' Under GDI and GDI+ the buffer owns everything it uses: a compatible DC and a bitmap,
' both created in BeginDoubleBuffer and destroyed in End. That works because both are
' cheap. An ID2D1HwndRenderTarget is not -- it wraps a device, and creating one per
' WM_PAINT would dominate every measurement in this project.
'
' So the render target is keyed on the HWND and outlives the buffer. The public API does
' not change (BeginDoubleBuffer still takes just an hwnd), at the cost of ONE new host
' obligation:
'
'     case WM_DESTROY : clsDoubleBuffer_ReleaseTarget( hwnd )
'
' Forgetting it leaks a device per window. clsDoubleBuffer_ShutdownD2D sweeps whatever is
' left as a backstop and says so, loudly, so the omission is findable rather than silent.
' ========================================================================================

#include once "AfxNova\AfxD2D1.bi"
#include once "AfxNova\AfxDWrite.bi"


' ----------------------------------------------------------------------------------------
' Crash-survivable trace (DBUF_D2DTRACE=1). Opens and closes the file per line, so whatever
' was written before a fault is on disk. Ordinary "print" is buffered when stdout is
' redirected to a pipe, which means a crash loses the very output that would locate it --
' that cost an hour once, so the tool exists.
' ----------------------------------------------------------------------------------------
sub DBufD2D_Trace( byval s as string )
    static as long enabled = -1
    if enabled = -1 then
        if environ("DBUF_D2DTRACE") = "1" then enabled = 1 else enabled = 0
    end if
    if enabled = 0 then exit sub
    dim as long f = freefile
    if open( "d2dtrace.log" for append as #f ) <> 0 then exit sub
    print #f, s
    close #f
end sub


' ----------------------------------------------------------------------------------------
' Process-wide singletons. Created by clsDoubleBuffer_InitD2D, which the host must call
' before the first WM_PAINT -- the same contract AfxGdipInit already has.
' ----------------------------------------------------------------------------------------
dim shared gD2DFactory      as ID2D1Factory ptr
dim shared gDWriteFactory   as IDWriteFactory ptr
' Both are only needed to build a private font collection, both are a QueryInterface away
' from the base factory, and both may legitimately be absent on an old system -- so every
' use is guarded rather than assumed. Factory5 is Windows 10 1709+; that is the floor for
' private fonts here, and the collection simply does not get built below it.
dim shared gDWriteFactory3  as IDWriteFactory3 ptr
dim shared gDWriteFactory5  as IDWriteFactory5 ptr

' Custom font collection holding any font handed to clsDoubleBuffer_AddPrivateFont.
' Null until built, and rebuilt whenever a font is added.
dim shared gDWritePrivateFonts as IDWriteFontCollection1 ptr
dim shared gPrivateFontPaths(any) as DWSTRING
dim shared gPrivateFontCount   as long
dim shared gPrivateFontsDirty  as boolean


' ----------------------------------------------------------------------------------------
' HWND -> render target registry.
' ----------------------------------------------------------------------------------------
type DBUF_D2DTARGET
    hwnd     as HWND
    pRT      as ID2D1HwndRenderTarget ptr
    cx       as long              ' pixel size the target was last built/resized to
    cy       as long
end type

dim shared gD2DTargets(any) as DBUF_D2DTARGET
dim shared gD2DTargetCount  as long

' Counts targets that ShutdownD2D had to clean up because a host never released them.
' Reported rather than silently absorbed -- see the header note.
dim shared gD2DLeakedTargets as long

' The second render target kind. Declared up here with the other process-wide state
' because clsDoubleBuffer_ShutdownD2D releases it; its factory function lives further
' down, next to the explanation of why two kinds are needed.
dim shared gD2DDCTarget as ID2D1DCRenderTarget ptr

' ----------------------------------------------------------------------------------------
' TEXT RENDERING MODE
'
' Which rasterizer DirectWrite uses, and it is the knob that decides whether D2D text looks
' like GDI's or like DirectWrite's own.
'
'   NATURAL      DirectWrite's own default (NATURAL_SYMMETRIC). Glyphs are positioned at
'                SUBPIXEL offsets, so spacing is proportionally accurate and text scales
'                smoothly. At small sizes on a ~96 DPI display this reads as SOFTER than
'                GDI, because a stem that belongs at x=10.4 is drawn across two pixels
'                rather than snapped to one.
'   GDI_CLASSIC  What GDI's ClearType does: hint every stem onto the pixel grid, integer
'                advances, no vertical antialiasing. Measured pixel-identical to GDI's
'                DrawText here (mean 3/255 apart, zero offset), which makes it the exact
'                drop-in when a port must not shift anything.
'   GDI_NATURAL  GDI-compatible advances with DirectWrite's vertical rendering, and THE
'                DEFAULT. Measured crisper than GDI itself at UI sizes.
'
' D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE, which the render targets already set, is a DIFFERENT
' question -- it picks ClearType over greyscale, not WHICH ClearType. Both are needed.
'
' Gamma, contrast, ClearType level and pixel geometry are taken from the MONITOR'S OWN
' settings and passed through unchanged; only the rendering mode is overridden. Inventing
' values for those would change four variables at once and make any comparison worthless.
' ----------------------------------------------------------------------------------------
enum
    DBUF_TEXTMODE_NATURAL = 0
    DBUF_TEXTMODE_GDI_CLASSIC
    DBUF_TEXTMODE_GDI_NATURAL
    DBUF_TEXTMODE_COUNT
end enum

' DEFAULT: GDI_NATURAL, chosen on measurement rather than on DirectWrite's recommendation.
' At the 9-12 px UI sizes this family uses it produced MORE fully-saturated stem pixels
' than GDI itself (78 vs 70 on the F17 sample) while using less ink, and its advances stay
' GDI-compatible so existing GetTextExtentPoint32W layout remains valid.
'
' NATURAL -- DirectWrite's own default -- is deliberately NOT the default here. It is the
' better choice at high DPI and large sizes, where subpixel positioning wins, but at UI
' sizes on a standard-DPI display it is visibly softer than GDI. F3 cycles all three; the
' right answer is display-dependent and this is a considered starting point, not a law.
dim shared gTextMode as long = DBUF_TEXTMODE_GDI_CLASSIC
dim shared gRenderParams( 0 to DBUF_TEXTMODE_COUNT - 1 ) as IDWriteRenderingParams ptr

function DBufD2D_TextModeName( byval m as long ) as string
    select case m
        case DBUF_TEXTMODE_NATURAL     : return "NATURAL (DirectWrite default, subpixel positioned)"
        case DBUF_TEXTMODE_GDI_CLASSIC : return "GDI_CLASSIC (GDI's ClearType: hinted to the pixel grid)"
        case DBUF_TEXTMODE_GDI_NATURAL : return "GDI_NATURAL (GDI advances, DirectWrite vertical)"
    end select
    return "?"
end function

' The measuring mode that MUST accompany each rendering mode. Setting the rasterizer to
' GDI_CLASSIC while still measuring NATURAL gives GDI-shaped glyphs at DirectWrite
' positions, which looks worse than either mode done consistently.
function DBufD2D_MeasuringMode() as DWRITE_MEASURING_MODE
    select case gTextMode
        case DBUF_TEXTMODE_GDI_CLASSIC : return DWRITE_MEASURING_MODE_GDI_CLASSIC
        case DBUF_TEXTMODE_GDI_NATURAL : return DWRITE_MEASURING_MODE_GDI_NATURAL
    end select
    return DWRITE_MEASURING_MODE_NATURAL
end function

function DBufD2D_RenderParamsFor( byval m as long ) as IDWriteRenderingParams ptr
    if (m < 0) orelse (m >= DBUF_TEXTMODE_COUNT) then return 0
    if gRenderParams(m) then return gRenderParams(m)
    if gDWriteFactory = 0 then return 0

    ' The monitor's real settings, so only the rendering mode differs between the three.
    dim as POINT ptOrigin = type( 0, 0 )
    dim as HMONITOR hMon = MonitorFromPoint( ptOrigin, MONITOR_DEFAULTTOPRIMARY )
    dim as IDWriteRenderingParams ptr pBase
    if gDWriteFactory->CreateMonitorRenderingParams( hMon, pBase ) <> S_OK then return 0
    if pBase = 0 then return 0

    dim as DWRITE_RENDERING_MODE rm = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC
    select case m
        case DBUF_TEXTMODE_GDI_CLASSIC : rm = DWRITE_RENDERING_MODE_GDI_CLASSIC
        case DBUF_TEXTMODE_GDI_NATURAL : rm = DWRITE_RENDERING_MODE_GDI_NATURAL
    end select

    gDWriteFactory->CreateCustomRenderingParams( _
            pBase->GetGamma(), pBase->GetEnhancedContrast(), pBase->GetClearTypeLevel(), _
            pBase->GetPixelGeometry(), rm, gRenderParams(m) )
    pBase->Release()
    return gRenderParams(m)
end function

sub DBufD2D_SetTextMode( byval m as long )
    if (m < 0) orelse (m >= DBUF_TEXTMODE_COUNT) then exit sub
    gTextMode = m
end sub


' ========================================================================================
' Pick the text mode from the display's DPI.
'
' WHY A THRESHOLD AND NOT DirectWrite's OWN RECOMMENDATION. IDWriteFontFace::
' GetRecommendedRenderingMode looks like exactly the right API and is not: it MIRRORS the
' measuring mode handed to it. Ask it with MEASURING_MODE_GDI_CLASSIC and it says
' GDI_CLASSIC; ask with NATURAL and it says NATURAL. Measured across 9-16 px and 96-288
' DPI, it never once crossed between the two families. It refines WITHIN a family; it does
' not choose one.
'
' It did supply the threshold, though. Within the natural family it switches NATURAL ->
' NATURAL_SYMMETRIC at exactly 144 DPI -- Microsoft's own "there is enough resolution now"
' line. Reusing that number puts the GDI/natural switch on measured ground rather than on
' a figure invented here:
'
'   < 144 DPI  (below 150% scaling)   GDI_CLASSIC
'                                     Stems land on whole pixels. At 9-12 px this measured
'                                     crisper than DirectWrite's own default and identical
'                                     to GDI's ClearType (F17), and advances stay
'                                     GDI-compatible.
'   >= 144 DPI (150% and above)       NATURAL
'                                     Enough pixels per stem that subpixel positioning buys
'                                     accurate spacing instead of costing sharpness.
'
' F3 still overrides at runtime; this only sets the starting point.
'
' TWO LIMITS, both real:
'   * The DPI comes from GetDeviceCaps on the window's DC. In a process that has NOT
'     declared DPI awareness -- which this demo has not -- Windows reports 96 whatever the
'     user's scaling is, and then bitmap-stretches the result. So in THIS demo the
'     selection always lands on GDI_CLASSIC. DIRECTTEST_DPI=n overrides the reading so the
'     logic itself can be exercised; a real host that declares awareness gets real numbers.
'   * It is evaluated per call, not per monitor. Dragging a window between displays of
'     different DPI needs a re-evaluation -- see the WM_DPICHANGED handler in frmMain.
' ========================================================================================
function DBufD2D_DpiForWindow( byval hwnd as HWND ) as long
    ' Test override first, so the threshold can be exercised on any machine.
    dim as string sOverride = environ("DIRECTTEST_DPI")
    if len(sOverride) then
        dim as long n = valint(sOverride)
        if n >= 48 then return n
    end if
    dim as long dpi = 96
    dim as HDC hdc = GetDC( hwnd )
    if hdc then
        dim as long n = GetDeviceCaps( hdc, LOGPIXELSX )
        if n > 0 then dpi = n
        ReleaseDC( hwnd, hdc )
    end if
    return dpi
end function

function DBufD2D_AutoSelectTextMode( byval hwnd as HWND ) as long
    dim as long dpi = DBufD2D_DpiForWindow( hwnd )
    dim as long m
    if dpi >= 144 then
        m = DBUF_TEXTMODE_NATURAL
    else
        m = DBUF_TEXTMODE_GDI_CLASSIC
    end if
    DBufD2D_SetTextMode( m )
    return m
end function

function DBufD2D_GetTextMode() as long
    return gTextMode
end function


' HFONT -> IDWriteTextFormat cache. Same reason for living up here: shutdown frees it.
' Built and explained further down.
type DBUF_FONTCACHE
    inUse       as boolean
    faceName    as wstring * 32
    height      as long
    weight      as long
    italic      as ubyte
    pFormat     as IDWriteTextFormat ptr
    ' The "..." drawn when text is trimmed. One per format, because it inherits the
    ' format's font and size -- an ellipsis built from a 9pt format looks wrong appended
    ' to 12pt text. Created lazily: most formats never trim.
    pEllipsis   as IDWriteInlineObject ptr
end type

const DBUF_FONTCACHE_MAX = 16
dim shared gFontCache( 0 to DBUF_FONTCACHE_MAX - 1 ) as DBUF_FONTCACHE

sub DBufD2D_ReleaseFontCache()
    for i as long = 0 to DBUF_FONTCACHE_MAX - 1
        with gFontCache(i)
            if .pEllipsis then .pEllipsis->Release() : .pEllipsis = 0
            if .pFormat   then .pFormat->Release()   : .pFormat = 0
            .inUse = false
        end with
    next
end sub


' ========================================================================================
' COLORREF (0x00BBGGRR) -> D2D1_COLOR_F (four floats, 0..1, RGBA).
'
' Same reversal trap as the GDI+ path's DBufToARGB: the byte order is reversed, not merely
' shifted, and getting it wrong yields a picture that still renders -- so it looks like it
' works -- with red and blue swapped.
' ========================================================================================
private function DBufToColorF( byval clr as COLORREF, byval nAlpha as ubyte ) as D2D1_COLOR_F
    dim as D2D1_COLOR_F c
    c.r = csng( (clr        ) and &hFF ) / 255.0
    c.g = csng( (clr shr  8 ) and &hFF ) / 255.0
    c.b = csng( (clr shr 16 ) and &hFF ) / 255.0
    c.a = csng( nAlpha ) / 255.0
    return c
end function


' ========================================================================================
' Build (or rebuild) the private font collection from the accumulated paths.
'
' THE TRAP THIS EXISTS FOR, and it is the one that would have been hardest to spot:
'
'   AddFontResourceEx( path, FR_PRIVATE, NULL ) makes a font visible to GDI and to GDI+.
'   It does NOT make it visible to DirectWrite. DWrite's system font collection is built
'   from installed fonts; a process-private GDI font is not in it.
'
' The demo loads SegoeFluentIcons.ttf exactly that way, and every glyph in the UI comes
' from it. Under DirectWrite those lookups would fall back to a default font and render as
' the wrong glyphs -- or, on a machine where Windows happens to ship Segoe Fluent Icons
' system-wide, render CORRECTLY and hide the bug until it reached a machine that does not.
'
' So the same file is registered a second time, with DirectWrite, through a font set
' builder. Both registrations are kept: the GDI one still serves the GDI/GDI+ backends and
' anything that measures with an HFONT.
'
' ----------------------------------------------------------------------------------------
' WHY THIS TAKES THE FILE -> FONT FILE -> FONT SET ROUTE RATHER THAN THE OBVIOUS ONE
'
' The documented one-step route is IDWriteFactory3::CreateFontFaceReference( path, ... ),
' and it was written that way first. It ACCESS-VIOLATES on this machine -- through
' AfxNova's declaration and through a hand-built raw vtable call with the correct
' signature, with a NULL lastWriteTime and with a real one. Its sibling
' CreateFontFaceReference2 (taking an IDWriteFontFile instead of a path) faults the same
' way, while every neighbouring slot on the same interface -- GetSystemFontSet,
' CreateFontSetBuilder, CreateFontCollectionFromFontSet -- works normally. The cause was
' not established; what IS established is that the fault is not AfxNova's binding, since
' bypassing AfxNova entirely reproduces it exactly.
'
' The route below avoids both of those methods and is built only from calls verified
' working in this project:
'
'   IDWriteFactory::CreateFontFileReference   (base interface)
'   IDWriteFactory5::CreateFontSetBuilder5    -> IDWriteFontSetBuilder1
'   IDWriteFontSetBuilder1::AddFontFile       (takes the font FILE, no face reference)
'   IDWriteFontSetBuilder::CreateFontSet
'   IDWriteFactory3::CreateFontCollectionFromFontSet
'
' Cost of the detour: it needs IDWriteFactory5, so Windows 10 1709 rather than 1607. That
' is an acceptable floor here, and it degrades to "no private collection" rather than
' crashing.
' ========================================================================================
private function DBufD2D_EnsurePrivateFonts() as IDWriteFontCollection1 ptr
    if (gPrivateFontsDirty = false) andalso (gDWritePrivateFonts <> 0) then
        return gDWritePrivateFonts
    end if
    if gPrivateFontCount = 0 then return 0
    ' Factory5 builds the set, Factory3 turns it into a collection. Missing either means
    ' no private fonts -- reported by the self-test, not silently tolerated.
    if gDWriteFactory5 = 0 then return 0
    if gDWriteFactory3 = 0 then return 0

    if gDWritePrivateFonts then
        gDWritePrivateFonts->Release()
        gDWritePrivateFonts = 0
    end if

    dim as IDWriteFontSetBuilder1 ptr pBuilder
    if gDWriteFactory5->CreateFontSetBuilder5( pBuilder ) <> S_OK then return 0
    if pBuilder = 0 then return 0

    dim as long nAdded = 0
    for i as long = 0 to gPrivateFontCount - 1
        DBufD2D_Trace( "private font: " & gPrivateFontPaths(i) )
        dim as IDWriteFontFile ptr pFile
        ' lastWriteTime NULL: expressed as a dereferenced null pointer, which is how a
        ' BYREF parameter is given the NULL the C API documents as optional.
        dim as HRESULT hr = gDWriteFactory->CreateFontFileReference( _
                                *gPrivateFontPaths(i).vptr, *cast(FILETIME ptr, NULL), pFile )
        if (hr = S_OK) andalso (pFile <> 0) then
            ' AddFontFile takes the file directly -- every face in it is added, so a .ttc
            ' works without enumerating faces.
            if pBuilder->AddFontFile( pFile ) = S_OK then nAdded += 1
            pFile->Release()
        end if
    next

    if nAdded > 0 then
        ' NOT "pSet": PSET is a FreeBASIC graphics statement, and a local named that way
        ' collides with it -- reported as "duplicated definition", which points at the
        ' declaration rather than at the reserved word. Same family of trap as the
        ' local-vs-member-procedure collision already in Learnings.md.
        dim as IDWriteFontSet ptr pFontSet
        if pBuilder->CreateFontSet( pFontSet ) = S_OK then
            if pFontSet then
                gDWriteFactory3->CreateFontCollectionFromFontSet( pFontSet, gDWritePrivateFonts )
                pFontSet->Release()
            end if
        end if
    end if

    pBuilder->Release()
    gPrivateFontsDirty = false
    return gDWritePrivateFonts
end function


' ========================================================================================
' Register a font FILE with DirectWrite. The host calls this alongside its existing
' AddFontResourceEx call -- see the trap described above.
' ========================================================================================
function clsDoubleBuffer_AddPrivateFont( byval wszPath as DWSTRING ) as boolean
    if len( wszPath ) = 0 then return false
    if gPrivateFontCount > ubound( gPrivateFontPaths ) then
        redim preserve gPrivateFontPaths( 0 to gPrivateFontCount + 3 )
    end if
    gPrivateFontPaths( gPrivateFontCount ) = wszPath
    gPrivateFontCount += 1
    gPrivateFontsDirty = true
    return (DBufD2D_EnsurePrivateFonts() <> 0)
end function


' ========================================================================================
' The font collection a text format should be created against: the private collection when
' there is one, else 0 meaning "the system collection".
'
' Passing the private collection does NOT hide the system fonts -- a collection built from
' a font set contains only what was added, so "Segoe UI" would not resolve from it. Phase 4
' therefore has to try the private collection first and fall back to the system one, per
' family name. Noted here because it is the non-obvious consequence of this design.
' ========================================================================================
function DBufD2D_PrivateFontCollection() as IDWriteFontCollection1 ptr
    return DBufD2D_EnsurePrivateFonts()
end function


' ========================================================================================
' Process init / shutdown. Bracket the message loop, exactly where AfxGdipInit already is.
' ========================================================================================
function clsDoubleBuffer_InitD2D() as boolean
    if gD2DFactory then return true              ' idempotent

    dim as D2D1_FACTORY_OPTIONS opts             ' debug level NONE
    dim as any ptr pRaw
    ' SINGLE_THREADED: every control in this family paints on the UI thread, and the
    ' multithreaded factory pays for a lock on every call to buy nothing here.
    if D2D1CreateFactory( D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_ID2D1Factory, opts, pRaw ) <> S_OK then
        return false
    end if
    gD2DFactory = cast( ID2D1Factory ptr, pRaw )

    ' SHARED rather than ISOLATED: the shared factory caches font data across everything in
    ' the process that uses DirectWrite, which is what a font-heavy UI wants.
    dim as any ptr pDW
    if DWriteCreateFactory( DWRITE_FACTORY_TYPE_SHARED, IID_IDWriteFactory, pDW ) <> S_OK then
        gD2DFactory->Release() : gD2DFactory = 0
        return false
    end if
    gDWriteFactory = cast( IDWriteFactory ptr, pDW )

    ' Optional: only needed for private font collections. Absence is not fatal.
    dim as any ptr pDW3
    if gDWriteFactory->QueryInterface( @IID_IDWriteFactory3, @pDW3 ) = S_OK then
        gDWriteFactory3 = cast( IDWriteFactory3 ptr, pDW3 )
    end if
    dim as any ptr pDW5
    if gDWriteFactory->QueryInterface( @IID_IDWriteFactory5, @pDW5 ) = S_OK then
        gDWriteFactory5 = cast( IDWriteFactory5 ptr, pDW5 )
    end if
    DBufD2D_Trace( "InitD2D: factory3=" & (gDWriteFactory3 <> 0) & _
                   " factory5=" & (gDWriteFactory5 <> 0) )

    gD2DTargetCount   = 0
    gD2DLeakedTargets = 0
    redim gD2DTargets( 0 to 7 )
    redim gPrivateFontPaths( 0 to 3 )
    return true
end function


sub clsDoubleBuffer_ShutdownD2D()
    ' Sweep any render target a host forgot to release. Counted, not just freed: a silent
    ' cleanup here means the leak is never found, and it is a per-window device.
    for i as long = 0 to gD2DTargetCount - 1
        if gD2DTargets(i).pRT then
            gD2DTargets(i).pRT->Release()
            gD2DTargets(i).pRT = 0
            gD2DLeakedTargets += 1
        end if
    next
    gD2DTargetCount = 0

    DBufD2D_ReleaseFontCache()
    for i as long = 0 to DBUF_TEXTMODE_COUNT - 1
        if gRenderParams(i) then gRenderParams(i)->Release() : gRenderParams(i) = 0
    next
    if gD2DDCTarget       then gD2DDCTarget->Release()         : gD2DDCTarget = 0
    if gDWritePrivateFonts then gDWritePrivateFonts->Release() : gDWritePrivateFonts = 0
    if gDWriteFactory5    then gDWriteFactory5->Release()      : gDWriteFactory5 = 0
    if gDWriteFactory3    then gDWriteFactory3->Release()      : gDWriteFactory3 = 0
    if gDWriteFactory     then gDWriteFactory->Release()       : gDWriteFactory = 0
    if gD2DFactory        then gD2DFactory->Release()          : gD2DFactory = 0

    if gD2DLeakedTargets > 0 then
        dim as string s = "clsDoubleBuffer: " & gD2DLeakedTargets & _
                          " render target(s) were still alive at shutdown -- a window is " & _
                          "missing clsDoubleBuffer_ReleaseTarget( hwnd ) in its WM_DESTROY."
        print s
    end if
end sub


' ----------------------------------------------------------------------------------------
' Registry lookup. Linear: this family runs a handful of windows, and a hash would be more
' code than the scan it replaces.
' ----------------------------------------------------------------------------------------
private function DBufD2D_FindSlot( byval hwnd as HWND ) as long
    for i as long = 0 to gD2DTargetCount - 1
        if gD2DTargets(i).hwnd = hwnd then return i
    next
    return -1
end function


' ========================================================================================
' Release the render target for one window. The host obligation, from WM_DESTROY.
' Safe to call for a window that never painted, and safe to call twice.
' ========================================================================================
sub clsDoubleBuffer_ReleaseTarget( byval hwnd as HWND )
    dim as long i = DBufD2D_FindSlot( hwnd )
    if i < 0 then exit sub
    if gD2DTargets(i).pRT then gD2DTargets(i).pRT->Release()
    ' Compact by moving the last entry into the hole; order is meaningless here.
    gD2DTargets(i) = gD2DTargets( gD2DTargetCount - 1 )
    gD2DTargetCount -= 1
end sub


' ========================================================================================
' The render target for a window: found, or created, or resized to match the client area.
'
' AUTO-RESIZE RATHER THAN A WM_SIZE OBLIGATION. A render target has a fixed pixel size and
' silently clips if the window outgrows it. That could have been pushed onto the host as a
' second obligation next to the WM_DESTROY one, but a missed Resize produces a subtly
' wrong picture rather than an obvious failure, and this family has eleven controls that
' would each have to remember. Comparing the size here costs two integer compares on a
' path that is about to do far more work than that.
' ========================================================================================
function DBufD2D_TargetFor( byval hwnd as HWND ) as ID2D1HwndRenderTarget ptr
    if gD2DFactory = 0 then return 0
    if hwnd = 0 then return 0

    dim as RECT rc
    GetClientRect( hwnd, @rc )
    dim as long cx = rc.right - rc.left
    dim as long cy = rc.bottom - rc.top
    ' A zero extent is legal for a collapsed window but not for a render target.
    if cx < 1 then cx = 1
    if cy < 1 then cy = 1

    ' ------------------------------------------------------------------------------------
    ' THE TARGET IS QUANTISED, AND IT IS NOT ALLOWED TO SHRINK.
    '
    ' ID2D1HwndRenderTarget::Resize reallocates a swap chain. Measured on this machine it
    ' costs a MEAN OF 8.9 ms, with outliers past 480 ms -- and a window being resized
    ' continuously, which is what every splitter drag does to its panes, would pay that on
    ' every mouse move. The CSplitter demo made it obvious: dragging the bar that moves
    ' sideways resizes the other bar's window each step, and the per-move cost went from
    ' ~1.0 ms to ~11.9 ms, i.e. a visible stutter. GDI and GDI+ have no equivalent cost --
    ' they just paint -- so this is a Direct2D-specific cliff a host cannot see coming.
    '
    ' Rounding the allocation UP to a 64 px grid and never shrinking turns a resize per
    ' move into a resize per 64 px of travel. A target LARGER than the client is fine: the
    ' window shows its top-left corner, every control paints its own background over the
    ' whole client first, and nothing reads back the slack. What is NOT fine is a target
    ' SMALLER than the client, so growth is still immediate and exact -- the quantum only
    ' ever adds.
    '
    ' The slack is bounded at 63 px per axis, which for a 32bpp surface is a few hundred KB
    ' on a large window and nothing at all on a scrollbar. Cheaper than one resize.
    '
    ' The device self-test asserts this contract directly (covers the client, quantised,
    ' never shrinks), because it replaced an earlier assertion that the pixel size EQUALLED
    ' the client rect -- which is exactly the invariant being traded away here.
    ' ------------------------------------------------------------------------------------
    const DBUF_RT_QUANTUM = 64
    dim as long wantCx = ((cx + DBUF_RT_QUANTUM - 1) \ DBUF_RT_QUANTUM) * DBUF_RT_QUANTUM
    dim as long wantCy = ((cy + DBUF_RT_QUANTUM - 1) \ DBUF_RT_QUANTUM) * DBUF_RT_QUANTUM

    dim as long i = DBufD2D_FindSlot( hwnd )
    if i >= 0 then
        if gD2DTargets(i).pRT then
            ' Grow only. A window that got smaller keeps the larger surface.
            dim as boolean bGrow = (gD2DTargets(i).cx < cx) orelse (gD2DTargets(i).cy < cy)
            if bGrow then
                if wantCx < gD2DTargets(i).cx then wantCx = gD2DTargets(i).cx
                if wantCy < gD2DTargets(i).cy then wantCy = gD2DTargets(i).cy
                dim as D2D1_SIZE_U newSize = D2D1_SizeU( wantCx, wantCy )
                if gD2DTargets(i).pRT->Resize( newSize ) = S_OK then
                    gD2DTargets(i).cx = wantCx
                    gD2DTargets(i).cy = wantCy
                else
                    ' Resize failed -- drop it and fall through to a fresh create.
                    gD2DTargets(i).pRT->Release()
                    gD2DTargets(i).pRT = 0
                end if
            end if
            if gD2DTargets(i).pRT then return gD2DTargets(i).pRT
        end if
    end if

    ' ------------------------------------------------------------------------------------
    ' Create. Two settings here are load-bearing:
    '
    ' 96 DPI, PINNED. Not the desktop DPI, which is what every Direct2D sample uses. This
    ' control family does its own DPI scaling through CWindow.ScaleX/ScaleY and hands the
    ' buffer PIXELS. A render target created at the desktop DPI would scale those pixels a
    ' SECOND time, so at 150% every rect would come out 1.5x too big -- and look
    ' deliberate. At 96 DPI one DIP is one pixel and every existing coordinate stays
    ' correct with no conversion anywhere.
    '
    ' ALPHA_MODE_IGNORE. An opaque target is a precondition for ClearType text; a
    ' premultiplied one silently downgrades to greyscale antialiasing. Since a control
    ' always fills its own background first, there is nothing to lose by being opaque.
    ' ------------------------------------------------------------------------------------
    dim as D2D1_RENDER_TARGET_PROPERTIES props = D2D1_RenderTargetProperties( _
            D2D1_RENDER_TARGET_TYPE_DEFAULT, _
            D2D1_PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE ), _
            96.0, 96.0, _
            D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT )

    ' ------------------------------------------------------------------------------------
    ' PRESENT OPTIONS. Read this before changing it; the default is a performance trap.
    '
    ' D2D1_PRESENT_OPTIONS_NONE makes EndDraw WAIT FOR THE VERTICAL BLANK. That is right
    ' for a game looping at the refresh rate and wrong for a UI that repaints on demand:
    ' it turns every single repaint into a blocking wait of up to one frame, on the UI
    ' thread. It was measured here before it was believed -- one CColumnHeader repaint
    ' cost 6948 us against GDI's 55, and 6.944 ms is exactly one frame at the 144 Hz this
    ' machine runs. The number was the monitor, not the renderer.
    '
    ' IMMEDIATELY drops the wait. Tearing is the theoretical cost, and it does not apply
    ' to on-demand repainting of static UI -- nothing here animates fast enough to tear.
    ' DBUF_D2D_VSYNC=1 restores the waiting behaviour for anyone who wants to see it.
    '
    ' Either way the back buffer is undefined after a present, so every paint must redraw
    ' the whole client. The controls do that anyway, and README F2 established that the
    ' current build already repaints everything on a scroll, so nothing is given up.
    ' ------------------------------------------------------------------------------------
    static as long sPresentOpt = -1
    if sPresentOpt = -1 then
        if environ("DBUF_D2D_VSYNC") = "1" then
            sPresentOpt = D2D1_PRESENT_OPTIONS_NONE
        else
            sPresentOpt = D2D1_PRESENT_OPTIONS_IMMEDIATELY
        end if
    end if
    ' Created at the QUANTISED size, not the exact client size -- otherwise the very first
    ' pixel of growth would trigger the resize this quantisation exists to avoid.
    dim as D2D1_HWND_RENDER_TARGET_PROPERTIES hprops = D2D1_HwndRenderTargetProperties( _
            hwnd, D2D1_SizeU( wantCx, wantCy ), cast( D2D1_PRESENT_OPTIONS, sPresentOpt ) )

    dim as ID2D1HwndRenderTarget ptr pRT
    if gD2DFactory->CreateHwndRenderTarget( props, hprops, pRT ) <> S_OK then return 0
    if pRT = 0 then return 0

    if i < 0 then
        if gD2DTargetCount > ubound( gD2DTargets ) then
            redim preserve gD2DTargets( 0 to gD2DTargetCount + 7 )
        end if
        i = gD2DTargetCount
        gD2DTargetCount += 1
    end if
    with gD2DTargets(i)
        .hwnd = hwnd
        .pRT  = pRT
        ' The ALLOCATED size, which is what the grow test above compares against. Storing
        ' the client size here instead would make every window resize look like growth.
        .cx   = wantCx
        .cy   = wantCy
    end with
    return pRT
end function


' ========================================================================================
' HFONT -> IDWriteTextFormat.
'
' The whole control family passes HFONTs around and the host owns them, so keeping
' SetFont(HFONT) working is what lets existing controls compile untouched. The LOGFONT is
' read back out of the HFONT and used to build a text format.
'
' Cached in a small table keyed on the fields that matter. A CListBox repaint calls
' SetFont once per row and a text format is not cheap to build, so without this the font
' lookup would dominate.
'
' TWO CONVERSIONS THAT ARE EASY TO GET WRONG:
'
'   lfHeight < 0  is the em size in PIXELS -- negate it and it is the DIP size, because
'                 the render target is pinned to 96 DPI. This is what CWindow.CreateFont
'                 produces, so it is the case that actually runs.
'   lfHeight > 0  is a CELL height, which includes internal leading and is therefore
'                 LARGER than the em size. Scaling it as if it were an em size makes every
'                 glyph noticeably too big. Converted approximately here via the usual
'                 ~1.21 cell-to-em ratio; exact would need the font's own metrics.
'
' Family resolution tries the private collection first and falls back to the system one.
' A collection built from a font set contains ONLY what was added, so "Segoe UI" does not
' resolve from it -- see the note on DBufD2D_PrivateFontCollection.
' ========================================================================================
function DBufD2D_TextFormatFor( byval hFont as HFONT ) as IDWriteTextFormat ptr
    if gDWriteFactory = 0 then return 0
    if hFont = 0 then return 0

    dim as LOGFONTW lf
    if GetObjectW( hFont, sizeof(LOGFONTW), @lf ) = 0 then return 0

    for i as long = 0 to DBUF_FONTCACHE_MAX - 1
        with gFontCache(i)
            if .inUse andalso (.height = lf.lfHeight) andalso (.weight = lf.lfWeight) _
                      andalso (.italic = lf.lfItalic) andalso (.faceName = lf.lfFaceName) then
                return .pFormat
            end if
        end with
    next

    ' Em size in DIPs. At 96 DPI a DIP is a pixel, so a negative lfHeight converts by
    ' negation alone -- no DPI arithmetic, which is the point of pinning the target.
    dim as single emSize
    if lf.lfHeight < 0 then
        emSize = csng( -lf.lfHeight )
    elseif lf.lfHeight > 0 then
        emSize = csng( lf.lfHeight ) / 1.21      ' cell height -> em size, approximate
    else
        emSize = 12.0
    end if

    ' Private collection first (icon fonts live there), system collection otherwise.
    dim as IDWriteFontCollection ptr pUse
    dim as IDWriteFontCollection1 ptr pPriv = DBufD2D_PrivateFontCollection()
    if pPriv then
        dim as UINT32 idx
        dim as BOOLEAN bExists
        pPriv->FindFamilyName( lf.lfFaceName, @idx, bExists )
        if bExists <> 0 then pUse = cast( IDWriteFontCollection ptr, pPriv )
    end if

    dim as IDWriteTextFormat ptr pFmt
    dim as HRESULT hr = gDWriteFactory->CreateTextFormat( _
            lf.lfFaceName, pUse, _
            iif( lf.lfWeight >= FW_BOLD, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_WEIGHT_REGULAR ), _
            iif( lf.lfItalic <> 0, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STYLE_NORMAL ), _
            DWRITE_FONT_STRETCH_NORMAL, emSize, "", pFmt )
    if (hr <> S_OK) orelse (pFmt = 0) then return 0

    for i as long = 0 to DBUF_FONTCACHE_MAX - 1
        with gFontCache(i)
            if .inUse = false then
                .inUse    = true
                .faceName = lf.lfFaceName
                .height   = lf.lfHeight
                .weight   = lf.lfWeight
                .italic   = lf.lfItalic
                .pFormat  = pFmt
                exit for
            end if
        end with
    next
    return pFmt
end function


' ========================================================================================
' Apply (or clear) end-ellipsis trimming on a cached text format.
'
' THE TRAP, AND IT IS THE REASON THIS IS A SUB RATHER THAN A GETTER: text formats are
' CACHED AND SHARED. Trimming is a property of the format, not of a draw call, so setting
' it for one DT_END_ELLIPSIS caller leaves it set for every later caller using the same
' font -- captions that should never trim would silently start showing "...". So every
' PaintText MUST state its intent, both ways, on every call. That is what this does.
' ========================================================================================
sub DBufD2D_SetEllipsis( byval pFmt as IDWriteTextFormat ptr, byval bWanted as boolean )
    if pFmt = 0 then exit sub

    ' NOT "trim": TRIM is a FreeBASIC string function and a local of that name collides
    ' with it. Third one of these in this project after pSet/PSET and the family's ok/Ok --
    ' fbc's built-in names are numerous and unqualified, so a natural-looking identifier
    ' from a COM API lands on one every so often.
    dim as DWRITE_TRIMMING trimOpts
    if bWanted = false then
        trimOpts.granularity = DWRITE_TRIMMING_GRANULARITY_NONE
        pFmt->SetTrimming( @trimOpts, NULL )
        exit sub
    end if

    ' Find this format's slot so the sign can be cached with it.
    dim as IDWriteInlineObject ptr pSign
    for i as long = 0 to DBUF_FONTCACHE_MAX - 1
        with gFontCache(i)
            if .inUse andalso (.pFormat = pFmt) then
                if .pEllipsis = 0 then
                    if gDWriteFactory then
                        gDWriteFactory->CreateEllipsisTrimmingSign( pFmt, .pEllipsis )
                    end if
                end if
                pSign = .pEllipsis
                exit for
            end if
        end with
    next

    ' CHARACTER, not WORD: DrawText's DT_END_ELLIPSIS cuts mid-word, and a word-granular
    ' trim would drop a whole final word instead, which reads as different text rather
    ' than as truncation.
    trimOpts.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER
    pFmt->SetTrimming( @trimOpts, pSign )
end sub


' ========================================================================================
' The process-wide DC render target, re-bound per use.
'
' This is the second of the two render target kinds, and it is not optional:
'
'   * The geometry self-test renders offscreen and reads the pixels back with GetPixel.
'     An HWND render target cannot be read back, so without this the family's ground-truth
'     assertions -- the ones that caught the RoundRect radius and ellipse off-by-ones
'     during the GDI+ work -- would simply stop running under D2D.
'   * Until CListBox is rewritten (Phase 5) its rows still arrive as WM_DRAWITEM with an
'     HDC, which an HWND target cannot reach either.
'
' Cached and re-bound rather than created per use, for the same reason as the HWND ones:
' creating a target wraps a device. BindDC is the cheap part.
' ========================================================================================
function DBufD2D_DCTargetFor( byval hdc as HDC, byref rcBind as RECT ) as ID2D1DCRenderTarget ptr
    if gD2DFactory = 0 then return 0
    if hdc = 0 then return 0

    if gD2DDCTarget = 0 then
        ' Same two load-bearing settings as the HWND target: 96 DPI so one DIP is one
        ' pixel, and an opaque pixel format so ClearType stays available.
        dim as D2D1_RENDER_TARGET_PROPERTIES props = D2D1_RenderTargetProperties( _
                D2D1_RENDER_TARGET_TYPE_DEFAULT, _
                D2D1_PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE ), _
                96.0, 96.0, _
                D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT )
        if gD2DFactory->CreateDCRenderTarget( props, gD2DDCTarget ) <> S_OK then return 0
        if gD2DDCTarget = 0 then return 0
    end if

    ' BindDC also resizes the target to the bound rect, so there is no separate Resize
    ' path here. Coordinates inside a draw are relative to the rect's top-left.
    if gD2DDCTarget->BindDC( hdc, rcBind ) <> S_OK then return 0
    return gD2DDCTarget
end function


' ========================================================================================
' Device loss. EndDraw reports D2DERR_RECREATE_TARGET when the GPU device backing the
' target has gone away -- a driver reset, a remote-desktop transition, a GPU hot-swap.
'
' This is an obligation GDI and GDI+ never had, and getting it wrong does not crash: the
' target simply stops updating, so the window freezes on its last good frame. Dropping the
' target here means the next paint builds a fresh one.
' ========================================================================================
sub DBufD2D_HandleDeviceLoss( byval hwnd as HWND )
    clsDoubleBuffer_ReleaseTarget( hwnd )
    if hwnd then InvalidateRect( hwnd, NULL, FALSE )
end sub


' ========================================================================================
' Device-layer self-test, run with DBUF_D2DTEST=1.
'
' Exists because "it compiles" proves nothing about any of this, and because an earlier
' verification in this project reported three successful builds of three backends when it
' had built one backend three times (README F4). Every claim below is checked against the
' live object, not against the code's intent:
'
'   * the 96 DPI pinning is read back off the render target, not assumed from the struct
'     that was passed in;
'   * the registry's caching is checked by POINTER IDENTITY, which is the only thing that
'     distinguishes "cached" from "created a second one that happens to work";
'   * the private font collection is checked by looking the family name up in it, which is
'     the assertion that would fail on a machine where Segoe Fluent Icons is NOT installed
'     system-wide -- exactly the machine where a fallback would otherwise hide the bug.
' ========================================================================================
namespace D2DSelfTest
    dim shared as long gRun, gFail

    private sub Chk( byval sName as string, byval bOK as boolean, byval sDetail as string )
        gRun += 1
        dim as string s
        if bOK then s = "PASS  " else s = "FAIL  " : gFail += 1
        s &= sName & "   [" & sDetail & "]"
        print s
    end sub
end namespace

sub clsDoubleBuffer_RunD2DSelfTest()
    using D2DSelfTest
    gRun = 0 : gFail = 0

    print ""
    print "=== Direct2D device-layer self-test ==="

    ' --- factories ----------------------------------------------------------------------
    Chk( "InitD2D is idempotent", clsDoubleBuffer_InitD2D(), "second call returns true" )
    Chk( "ID2D1Factory created",  (gD2DFactory <> 0), "ptr " & hex(cast(integer, gD2DFactory)) )
    Chk( "IDWriteFactory created",(gDWriteFactory <> 0), "ptr " & hex(cast(integer, gDWriteFactory)) )
    ' Not fatal if absent -- but on Win10 1607+ it should be there, and the private font
    ' collection silently does not exist without it, so it is asserted rather than hoped.
    Chk( "IDWriteFactory3 available", (gDWriteFactory3 <> 0), _
         "needed to turn a font set into a collection" )
    Chk( "IDWriteFactory5 available", (gDWriteFactory5 <> 0), _
         "needed for AddFontFile -- Windows 10 1709+" )

    ' --- private font collection --------------------------------------------------------
    scope
        dim as IDWriteFontCollection1 ptr pColl = DBufD2D_PrivateFontCollection()
        Chk( "private font collection built", (pColl <> 0), _
             "paths registered = " & gPrivateFontCount )
        if pColl then
            dim as UINT32 nFam = pColl->GetFontFamilyCount()
            Chk( "collection is non-empty", (nFam > 0), "families = " & nFam )

            ' THE assertion this whole mechanism exists for. AddFontResourceEx(FR_PRIVATE)
            ' does not put a font into DirectWrite's world; if this fails, every glyph in
            ' the UI silently falls back to the wrong font.
            dim as UINT32 idx
            dim as BOOLEAN bExists
            pColl->FindFamilyName( "Segoe Fluent Icons", @idx, bExists )
            Chk( "'Segoe Fluent Icons' resolves from the PRIVATE collection", _
                 (bExists <> 0), "exists=" & (bExists <> 0) & " index=" & idx )
        end if
    end scope

    ' --- render target registry ---------------------------------------------------------
    ' A scratch offscreen window, so nothing here disturbs the demo's own windows.
    dim as HWND hTest = CreateWindowExW( 0, "STATIC", "", WS_POPUP, _
                                         -2000, -2000, 200, 100, NULL, NULL, NULL, NULL )
    if hTest = 0 then
        Chk( "scratch window created", false, "CreateWindowExW failed -- registry untested" )
    else
        dim as ID2D1HwndRenderTarget ptr pRT1 = DBufD2D_TargetFor( hTest )
        Chk( "render target created", (pRT1 <> 0), "ptr " & hex(cast(integer, pRT1)) )

        if pRT1 then
            ' 96 DPI pinning, READ BACK off the target. If this ever reports the desktop
            ' DPI, every coordinate in the family is being scaled twice.
            ' Note the shape: on the RAW interface these are subs with byref outs. The
            ' GetDpiX()/GetSize() function forms belong to AfxNova's CID2D1RenderTarget
            ' wrapper class, which is not what is held here.
            dim as single dx, dy
            pRT1->GetDpi( dx, dy )
            Chk( "DPI pinned to 96 (1 DIP = 1 pixel)", (dx = 96.0) andalso (dy = 96.0), _
                 "dpiX=" & dx & " dpiY=" & dy )

            ' The consequence of the above, stated directly: DIPs and pixels are the same
            ' number. This is the assertion that would catch a future edit passing the
            ' desktop DPI, even if the SetDpi check above were somehow satisfied.
            dim as D2D1_SIZE_F szf
            dim as D2D1_SIZE_U szu
            pRT1->GetSize( szf )
            pRT1->GetPixelSize( szu )
            Chk( "size in DIPs = size in pixels", _
                 (szf.width = csng(szu.width)) andalso (szf.height = csng(szu.height)), _
                 "dips " & szf.width & "x" & szf.height & "  px " & szu.width & "x" & szu.height )

            ' ... and the pixel size COVERS the client area, quantised up to the 64 px grid.
            ' This assertion used to demand exact equality with the client rect. That
            ' invariant was deliberately traded away: an exact fit means a Resize() on every
            ' pixel of growth, and a Resize() costs ~9 ms here, which a splitter drag pays
            ' on every mouse move. What must still hold is that the surface is never
            ' SMALLER than the client -- that would clip real content.
            dim as RECT rcc : GetClientRect( hTest, @rcc )
            Chk( "pixel size covers the client, quantised up", _
                 (szu.width >= rcc.right) andalso (szu.height >= rcc.bottom) andalso _
                 ((szu.width mod 64) = 0) andalso ((szu.height mod 64) = 0), _
                 "rt " & szu.width & "x" & szu.height & "  client " & rcc.right & "x" & rcc.bottom )

            ' Caching: the SAME pointer, not merely a working one.
            dim as ID2D1HwndRenderTarget ptr pRT2 = DBufD2D_TargetFor( hTest )
            Chk( "second request returns the SAME target", (pRT2 = pRT1), _
                 "cached, not recreated" )

            ' Auto-resize: grow the window, ask again. The target must follow WITHOUT the
            ' host having handled WM_SIZE -- that is the whole point of the design.
            SetWindowPos( hTest, 0, -2000, -2000, 300, 150, SWP_NOZORDER or SWP_NOACTIVATE )
            dim as ID2D1HwndRenderTarget ptr pRT3 = DBufD2D_TargetFor( hTest )
            dim as D2D1_SIZE_U szu3
            pRT3->GetPixelSize( szu3 )
            GetClientRect( hTest, @rcc )
            Chk( "auto-resize covered the grown window", _
                 (szu3.width >= rcc.right) andalso (szu3.height >= rcc.bottom), _
                 "rt " & szu3.width & "x" & szu3.height & "  client " & rcc.right & "x" & rcc.bottom )
            Chk( "resize reused the target, did not recreate", (pRT3 = pRT1), _
                 "Resize() rather than a new device" )

            ' AND IT DOES NOT SHRINK BACK. This is the half that actually buys the speed:
            ' a window getting smaller must not reallocate, or a drag that sweeps back and
            ' forth pays on every move in one direction. Asserted by pointer AND by size,
            ' because "did not recreate" alone would still pass if it had resized down.
            dim as D2D1_SIZE_U szBig = szu3
            SetWindowPos( hTest, 0, -2000, -2000, 200, 100, SWP_NOZORDER or SWP_NOACTIVATE )
            dim as ID2D1HwndRenderTarget ptr pRT4 = DBufD2D_TargetFor( hTest )
            dim as D2D1_SIZE_U szu4
            pRT4->GetPixelSize( szu4 )
            Chk( "shrinking the window does NOT resize the target", _
                 (pRT4 = pRT1) andalso (szu4.width = szBig.width) andalso _
                 (szu4.height = szBig.height), _
                 "rt " & szu4.width & "x" & szu4.height & "  was " & szBig.width & "x" & szBig.height )
        end if

        ' Release removes the slot.
        dim as long nBefore = gD2DTargetCount
        clsDoubleBuffer_ReleaseTarget( hTest )
        Chk( "ReleaseTarget removed the slot", (gD2DTargetCount = nBefore - 1), _
             "count " & nBefore & " -> " & gD2DTargetCount )
        ' ... and is safe to call again for a window already released.
        clsDoubleBuffer_ReleaseTarget( hTest )
        Chk( "ReleaseTarget is safe twice", (gD2DTargetCount = nBefore - 1), _
             "count still " & gD2DTargetCount )

        DestroyWindow( hTest )
    end if

    ' --- text formats and the trimming invariant ----------------------------------------
    scope
        dim as HFONT hf = CreateFontW( -12, 0, 0, 0, FW_NORMAL, 0, 0, 0, _
                                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI" )
        if hf = 0 then
            Chk( "text format: test font created", false, "CreateFontW failed" )
        else
            dim as IDWriteTextFormat ptr pFmt = DBufD2D_TextFormatFor( hf )
            Chk( "HFONT resolves to an IDWriteTextFormat", (pFmt <> 0), _
                 "LOGFONT -> text format" )

            ' The cache must return the SAME object for the same font, or every row of a
            ' list would build a fresh text format.
            dim as IDWriteTextFormat ptr pFmt2 = DBufD2D_TextFormatFor( hf )
            Chk( "text format is cached, not rebuilt", (pFmt2 = pFmt), "same pointer" )

            ' Em size: a negative lfHeight is a PIXEL em size, and at the 96 DPI the
            ' targets are pinned to that is also the DIP size. If this ever reports
            ' something scaled, the DPI pinning has been broken somewhere.
            if pFmt then
                dim as single em = pFmt->GetFontSize()
                Chk( "lfHeight -12 -> 12 DIP em size", (em = 12.0), "em = " & em )
            end if

            ' THE INVARIANT. Text formats are cached and shared, so trimming set by one
            ' DT_END_ELLIPSIS caller must not survive into the next caller -- otherwise
            ' captions that should never trim silently sprout "...". Asserted by round
            ' trip rather than by eye, because the failure is a subtle visual one.
            if pFmt then
                dim as DWRITE_TRIMMING got
                dim as IDWriteInlineObject ptr pSignBack

                DBufD2D_SetEllipsis( pFmt, true )
                pFmt->GetTrimming( @got, pSignBack )
                Chk( "ellipsis ON sets CHARACTER trimming + a sign", _
                     (got.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER) andalso _
                     (pSignBack <> 0), _
                     "granularity=" & got.granularity & " sign=" & (pSignBack <> 0) )

                DBufD2D_SetEllipsis( pFmt, false )
                pFmt->GetTrimming( @got, pSignBack )
                Chk( "ellipsis OFF clears it again (no leak to the next caller)", _
                     (got.granularity = DWRITE_TRIMMING_GRANULARITY_NONE), _
                     "granularity=" & got.granularity )
            end if
            DeleteObject( hf )
        end if
    end scope

    ' Nothing this test created may still be registered -- otherwise the leak counter at
    ' shutdown would fire on the test's own mess and mask a real one.
    Chk( "no targets leaked by this test", (gD2DTargetCount = 0), _
         "registered targets = " & gD2DTargetCount )

    dim as string sSummary = "=== " & (gRun - gFail) & " of " & gRun & " assertions passed"
    if gFail > 0 then sSummary &= "  (" & gFail & " FAILED)"
    sSummary &= " ==="
    print sSummary
end sub
