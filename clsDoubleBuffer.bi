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

#pragma once

' ========================================================================================
' RENDERING BACKEND -- pick EXACTLY ONE
' ----------------------------------------------------------------------------------------
' All three implement the SAME public surface, so this identical source tree builds any of
' them. That is what makes an A/B/C screenshot diff or timing run a one-variable
' experiment, and what makes rolling one back a one-line edit instead of a revert.
'
'   DBUF_BACKEND_GDI      the original renderer. Everything through GDI.
'   DBUF_BACKEND_GDIPLUS  geometry through GDI+ (antialiased curves, alpha),
'                         text still through GDI DrawText.
'   DBUF_BACKEND_D2D      geometry through Direct2D, text through DirectWrite.
'
' On the GDI+ backend text stays on GDI deliberately: GDI+ measures and lays out text
' differently, so every GetTextExtentPoint32W / GetTextMetricsW site in the control
' family's LayoutItems would have to convert to MeasureString in lockstep or text clips,
' and the icon glyphs (Segoe Fluent Icons) would shift. The D2D backend is where that
' problem is actually taken on, via DirectWrite and a MeasureText primitive.
' ========================================================================================
' The backend can come from the BUILD COMMAND instead of this file:
'
'     build.bat gdi        ->  fbc -d DBUF_BACKEND_GDI ...
'     build.bat gdiplus
'     build.bat d2d
'
' which is how the A/B/C runs select one. It matters that this is possible without editing
' the file: an earlier attempt at this comparison switched backends by rewriting this line
' with a regex, the regex silently failed to match, and three "successful" builds of three
' different backends were in fact the same backend three times. A build that cannot be
' misconfigured by the harness driving it is worth more than a comment saying be careful.
'
' The line below is only the DEFAULT, used when the command line said nothing.
#if not (defined(DBUF_BACKEND_GDI) or defined(DBUF_BACKEND_GDIPLUS) or defined(DBUF_BACKEND_D2D))
    #define DBUF_BACKEND_GDIPLUS
#endif

' ----------------------------------------------------------------------------------------
' Exactly one, enforced -- and the enforcement is itself tested (see the note above about
' verifications that did not verify anything).
'
' NOTE FOR ANYONE COPYING THIS GUARD: fbc's defined() yields -1 for true, not 1, following
' the FreeBASIC boolean convention. So "exactly one" is a sum of -1, and the natural-looking
' test `defined(A) + defined(B) + defined(C) > 1` can NEVER fire -- two selections sum to
' -2. That exact mistake sat here and passed a hand-run test of both failure cases, because
' the harness was broken in a way that masked it.
' ----------------------------------------------------------------------------------------
#if defined(DBUF_BACKEND_GDI) + defined(DBUF_BACKEND_GDIPLUS) + defined(DBUF_BACKEND_D2D) <> -1
    #error "clsDoubleBuffer: select EXACTLY ONE rendering backend (DBUF_BACKEND_GDI / _GDIPLUS / _D2D)"
#endif

' ----------------------------------------------------------------------------------------
' Implementation flags. The method bodies below test THESE, not the selection above.
' The split exists because the two are not the same question: the selection is what the
' author asked for, the implementation flag is which code actually runs.
'
' Exactly one implementation flag is on. The split from the selection above exists because
' the two are not the same question: the selection is what the author asked for, the
' implementation flag is which code actually runs. During Phase 2 they deliberately
' disagreed -- D2D selected the device layer but still drew through GDI+, and said so in
' its name. They agree from Phase 3 onwards.
' ----------------------------------------------------------------------------------------
#if defined(DBUF_BACKEND_GDIPLUS)
    #define DBUF_IMPL_GDIPLUS
    #define DBUF_BACKEND_NAME "GDI+"
#elseif defined(DBUF_BACKEND_D2D)
    #define DBUF_IMPL_D2D
    #define DBUF_BACKEND_NAME "D2D"
#else
    #define DBUF_IMPL_GDI
    #define DBUF_BACKEND_NAME "GDI"
#endif

' A CAPABILITY, not a backend. Both GDI+ and Direct2D antialias curved geometry; plain GDI
' does not. Assertions about smoothing are gated on this rather than on a backend name --
' the two antialiasing assertions in the self-test were originally written as
' "#ifdef DBUF_IMPL_GDIPLUS" and so silently stopped running the moment a third backend
' arrived, which is exactly when they mattered most.
#if defined(DBUF_IMPL_GDIPLUS) or defined(DBUF_IMPL_D2D)
    #define DBUF_HAS_ANTIALIAS
#endif

' A second capability, and a DIFFERENT question from antialiasing even though the same two
' backends happen to answer yes today. This one is about the SetXxxColorA overloads: can a
' fill blend with what is already on the surface, or does the alpha get ignored and the
' fill land opaque? Plain GDI has no compositing here, so an effect built on translucency
' must either degrade to an opaque approximation or skip itself -- and which of those is
' right depends on the effect, so callers test this rather than being handed a policy.
#if defined(DBUF_IMPL_GDIPLUS) or defined(DBUF_IMPL_D2D)
    #define DBUF_HAS_ALPHA
#endif

#ifdef DBUF_IMPL_GDIPLUS
    ' Included HERE rather than left to the call site on purpose. CListBox.bi names
    ' typedefs it does not include and so only compiles where the host happens to have
    ' pre-loaded them (see CLAUDE.md); this file does not repeat that trap.
    '
    ' ONE THING THIS COSTS THE HOST, and it is worth knowing before you adopt it:
    ' GDI+'s Status enum defines Ok = 0 in namespace AfxNova. Every host in this family
    ' already says "using AfxNova", so including this header puts Ok into the host's
    ' namespace and ANY identifier named "ok" -- a variable, a parameter -- becomes a
    ' duplicated definition. Five of the sibling demos had a SelfTest_Check parameter
    ' called exactly that and stopped compiling the moment they took this file. The fix
    ' is to rename yours (bOK is what the family uses); it cannot be fixed from in here,
    ' because the host's own "using AfxNova" is what exposes the name.
    #include once "AfxNova\CGdiPlus.inc"
    using AfxNova
#endif

#ifdef DBUF_IMPL_D2D
    ' Device/resource management: factories, the HWND->render-target registry, and the
    ' private font collection. Separate file because all of it is process- or
    ' window-lifetime state, whereas a clsDoubleBuffer is a per-WM_PAINT stack object.
    #include once "dbufD2D.bi"
#endif

#ifndef DBUF_IMPL_D2D
' ========================================================================================
' The D2D host obligation, as a no-op on the backends that do not need it.
'
' Deliberately NOT left to the host to wrap in an #ifdef. A control writes
'
'     case WM_DESTROY : clsDoubleBuffer_ReleaseTarget( hwnd )
'
' unconditionally, and it compiles on every backend. Putting a host obligation behind a
' conditional is how a backend swap turns into a leak on one branch and a compile error on
' another -- the same reasoning that keeps AfxGdipInit unconditional in main.bas.
' ========================================================================================
sub clsDoubleBuffer_ReleaseTarget( byval hwnd as HWND )
end sub
#endif

' ========================================================================================
' Measure a string in one font, WITHOUT needing an active paint.
'
' The primitive is a free function rather than a buffer method because that is how it is
' actually used: layout runs before or outside painting (a column autosize callback, a
' control's LayoutItems), where there is no buffer and no DC. clsDoubleBuffer.MeasureText
' below is a thin convenience over it for the in-paint case.
'
' WHY THIS HAS TO EXIST AT ALL, and why the GDI+ refactor stopped short of it: the family
' measures with GetTextExtentPoint32W and, under D2D, draws with DirectWrite. Those two do
' not agree -- different rounding, different kerning, subpixel advances -- so a layout
' computed by one and rendered by the other clips or leaves gaps. Routing every measurement
' through the SAME engine that draws is what makes the two consistent again.
' ========================================================================================
declare function clsDoubleBuffer_MeasureText( byval wszText as DWSTRING, byval hFont as HFONT ) as SIZE

declare function isMouseOverRECT( byval hWin as HWND, byval rc as RECT ) as boolean
declare function isMouseOverWindow( byval hChild as HWND ) as boolean
declare function PaintRect( byval hDC as HDC, byval rc as RECT ptr, byval clr as COLORREF ) as long

type clsDoubleBuffer
    private:
        _hwnd            as HWND
        _hDC             as HDC
        _memDC           as HDC
        _hbit            as HBITMAP
        _ps              as PAINTSTRUCT
        _rc              as RECT
        _pencolor        as COLORREF
        _forecolor       as COLORREF
        _backcolor       as COLORREF
        ' Alpha is carried alongside the COLORREF rather than folded into it: every
        ' existing SetXxxColor call keeps working unchanged and simply means "opaque".
        ' Only the SetXxxColorA overloads ever set these to anything but 255.
        _penalpha        as ubyte = 255
        _forealpha       as ubyte = 255
        _backalpha       as ubyte = 255
        _hFont           as HFONT         ' caller-supplied font; the control/host owns it
        ' Set ONLY by the descriptor SetFont overload on the GDI backends, which has to
        ' synthesise an HFONT because those backends have no other way to name a typeface.
        ' Owned by this buffer and destroyed with it -- distinct from _hFont, which the
        ' host owns and must never be deleted here.
        _hFontOwned      as HFONT
        _UsePaint        as boolean       ' use Begin/EndPaint. Used when WM_PAINT or WM_DRAWITEM
        _owns            as boolean = true ' does this object own _memDC/_hbit (delete on End)?

    #ifdef DBUF_IMPL_D2D
        ' The active render target for this paint. NOT owned: it is either the window's
        ' entry from the registry, or the process-wide DC render target. Either way it
        ' outlives the buffer, which is the whole point of the registry.
        _pRT             as ID2D1RenderTarget ptr
        ' One cached solid brush, recoloured rather than reallocated. A CListBox repaint
        ' runs this once per row per cell, so allocating per fill would be pure churn --
        ' the same reasoning as the GDI+ path's brush cache.
        _pBrush          as ID2D1SolidColorBrush ptr
        _brushColorSet   as boolean
        _brushR          as single
        _brushG          as single
        _brushB          as single
        _brushA          as single
        ' BeginDraw/EndDraw must be balanced exactly once. Tracked rather than assumed
        ' because an early return between Begin and End would otherwise leave the target
        ' mid-draw and every later paint would fail.
        _bDrawing        as boolean
        ' True when the target is bound to a DC and EndDoubleBuffer must still BitBlt
        ' (the row/offscreen path); false for the HWND target, which presents itself.
        _bBlitOnEnd      as boolean
        ' A clip pushed to match the window's UPDATE REGION. Must be popped exactly once,
        ' before EndDraw. See the long note on the WM_PAINT overload in the .inc -- this is
        ' what makes a partial InvalidateRect behave the same here as it does under GDI.
        _bClipPushed     as boolean

        declare function BrushFor( byval clr as COLORREF, byval nAlpha as ubyte ) as ID2D1SolidColorBrush ptr
        declare sub      ReleaseD2DObjects()
        declare sub      PopClipIfPushed()
    #endif

    #ifdef DBUF_IMPL_GDIPLUS
        ' One Graphics per buffer, built on first use and torn down before the blit.
        _pGraphics       as CGpGraphics ptr
        ' Brush and pen are cached and reused across calls, keyed on what they were
        ' built from. A CListBox repaint runs this path once per visible row, so
        ' allocating a fresh GDI+ object per fill would be pure churn.
        _pBrush          as CGpSolidBrush ptr
        _brushARGB       as ARGB
        _pPen            as CGpPen ptr
        _penARGB         as ARGB
        _penWidthCached  as single
        ' Set by any GDI+ draw, cleared by a flush. See EnsureGdiReady.
        _gpDirty         as boolean

        declare function EnsureGraphics() as CGpGraphics ptr
        declare function BrushFor( byval clr as ARGB ) as CGpBrush ptr
        declare function PenFor( byval clr as ARGB, byval nWidth as single ) as CGpPen ptr
        ' GDI+ batches its drawing; GDI does not. Mixing the two on one HDC without a
        ' flush loses shapes intermittently -- so every GDI text call goes through this
        ' first. Centralised here so no control or host ever has to think about it.
        declare sub      EnsureGdiReady()
        declare sub      ReleaseGpObjects()
        declare sub      BuildRoundPath( _
                    byval pPath as CGpGraphicsPath ptr, _
                    byval x as single, _
                    byval y as single, _
                    byval w as single, _
                    byval h as single, _
                    byval radius as single _
                    )
    #endif

    public:

    declare destructor()
    declare function BeginDoubleBuffer( byval hwnd as HWND ) as long
    declare function BeginDoubleBuffer( byval hwnd as HWND, byval hdc as HDC, byval rcItem as RECT ) as long
    ' Cached variant: reuse a caller-owned memDC (with its bitmap already selected);
    ' EndDoubleBuffer will blit but NOT delete it. Used to avoid per-row GDI churn.
    declare function BeginDoubleBuffer( byval hwnd as HWND, byval hdc as HDC, byval rcItem as RECT, byval cachedMemDC as HDC ) as long
    declare function EndDoubleBuffer() as long
    declare function PaintClientRect() as long
    declare function SetupBitmap() as long
    ' Painting always uses the CURRENT fore/back colors - hot/hover styling is
    ' the caller's responsibility: decide (e.g. via isMouseOverRECT or tracked
    ' hover state) and set the colors BEFORE painting.
    declare function PaintRectFactory( _
                byval rc as RECT ptr, _
                byval iStyle as long, _
                byval nPenWidth as long = 1, _
                byval nCurvature as long = 0 _
                ) as long
    declare function PaintRect( byval rc as RECT ptr ) as long
    declare function PaintBorderRect( _
                byval rc as RECT ptr, _
                byval nPenWidth as long = 1 _
                ) as long
    declare function PaintRoundRect( _
                byval rc as RECT ptr, _
                byval nCurvature as long = 20 _
                ) as long
    declare function PaintRoundBorderRect( _
                byval rc as RECT ptr, _
                byval nCurvature as long = 20, _
                byval nPenWidth as long = 1 _
                ) as long
    ' Stroke a rounded rect WITHOUT filling it. PaintRoundBorderRect always paints the
    ' interior, which is wrong for anything drawn over existing pixels -- a focus ring
    ' around an already-painted control being the case that needed it.
    declare function PaintRoundOutline( _
                byval rc as RECT ptr, _
                byval nCurvature as long = 20, _
                byval nPenWidth as long = 1 _
                ) as long
    ' Filled ellipse, optionally stroked. nPenWidth 0 = fill only.
    declare function PaintEllipse( _
                byval rc as RECT ptr, _
                byval nPenWidth as long = 0 _
                ) as long
    declare function PaintIconButton( _
            byval wszText as DWSTRING, _
            byval rc as RECT ptr, _
            byval nCurvature as long = 20 _
            ) as long
    declare function PaintLine( _
                byval nWidth as long, _
                byval nLeft as long, _
                byval nTop as long, _
                byval nRight as long, _
                byval nBottom as long _
                ) as long
    ' SINGLE LINE, ALWAYS, and vertically centred in rc. DT_VCENTER and DT_SINGLELINE are
    ' forced on by this method's contract rather than read from wsStyle, and DT_WORDBREAK
    ' is IGNORED. Embedded CR/LF are collapsed to spaces on every backend -- for several
    ' lines, call this once per line.
    '
    ' That last part is not free politeness: GDI's DT_SINGLELINE swallows newlines while
    ' DirectWrite's NO_WRAP does not (it only disables AUTOMATIC wrapping; a CR is still a
    ' hard paragraph break), so the same string rendered one line under GDI and three
    ' clipped ones under D2D until the collapse was added. wsStyle usefully carries only
    ' DT_LEFT / DT_CENTER / DT_RIGHT and DT_END_ELLIPSIS.
    declare function PaintText( _
                byval wszText as DWSTRING, _
                byval rc as RECT ptr, _
                byval wsStyle as DWORD _
                ) as long
    declare function PaintChar( _
                byval wszChar as DWSTRING, _
                byval rc as RECT ptr, _
                byval forecolor as COLORREF _
                ) as long
    declare function SetFont( byval hFont as HFONT ) as long
    ' Descriptor overload: name the typeface directly, with no GDI font object involved.
    ' Additive -- SetFont(HFONT) still works, so existing controls compile untouched. On
    ' the GDI backends this synthesises an HFONT and the buffer owns it; on D2D no HFONT
    ' is ever created.
    declare function SetFont( _
                byval wszFamily as DWSTRING, _
                byval nSizeDIP as single, _
                byval nWeight as long = FW_NORMAL, _
                byval bItalic as boolean = false _
                ) as long
    ' Measure using the buffer's current font. Convenience over the free function above.
    declare function MeasureText( byval wszText as DWSTRING ) as SIZE
    declare function SetForeColor( byval forecolor as COLORREF ) as long
    declare function SetBackColor( byval backcolor as COLORREF ) as long
    declare function SetColors( byval forecolor as COLORREF, byval backcolor as COLORREF ) as long
    declare function SetPenColor( byval pencolor as COLORREF ) as long
    ' --- Alpha-bearing variants (GDI+ backend only; the GDI backend ignores the alpha
    '     and behaves exactly like the plain setter). Additive: nothing existing calls
    '     these, so no current pixel changes because they exist. ---
    declare function SetForeColorA( byval forecolor as COLORREF, byval nAlpha as ubyte ) as long
    declare function SetBackColorA( byval backcolor as COLORREF, byval nAlpha as ubyte ) as long
    declare function SetPenColorA( byval pencolor as COLORREF, byval nAlpha as ubyte ) as long
    declare function rcClient() as RECT
    declare function rcClientWidth() as long
    declare function rcClientHeight() as long
    declare function getMemDC() as HDC

end type
