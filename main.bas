' ========================================================================================
' CustomListbox. Ownerdraw listbox 
' ========================================================================================

#define UNICODE
#define _WIN32_WINNT &h0602  

#include once "windows.bi"
#include once "AfxNova\CWindow.inc"
#include once "AfxNova\AfxStr.inc"
#include once "AfxNova\AfxGdiplus.inc"

using AfxNova


#define APPNAME          wstr("Custom Listbox")
#define APPCLASSNAME     wstr("custom_listbox_class")

#DEFINE GUIFONT          wstr("Segoe UI")
#DEFINE GUIFIXEDFONT     wstr("Consolas")
#DEFINE SYMBOLFONT       wstr("Segoe Fluent Icons")

#DEFINE GUIFONT_9        0
#DEFINE GUIFONT_10       1
#DEFINE GUIFONTBOLD_10   2
#DEFINE ITALICFONT_10    3
#DEFINE INFOFONT_11      4
#DEFINE SYMBOLFONT_9     5
#DEFINE SYMBOLFONT_10    6
#DEFINE SYMBOLFONT_12    7
#DEFINE MAXFONTS         8

dim shared ghFont(MAXFONTS) as HFONT

dim shared as HWND HWND_FRMMAIN
dim shared as HWND HWND_FRMEXPLORER
dim shared as HWND HWND_FRMEXPLORER2
dim shared as HWND HWND_COLHDR


type THEME_TYPE
    BackColorPanel     as COLORREF = BGR(220,220,220)
    BackColorScrollBar as COLORREF = BGR(33,37,43)
    ForeColorScrollBar as COLORREF = BGR(53,59,69)
    ForeColorScrollBarHot as COLORREF = BGR(90,98,112)
    ForeColor          as COLORREF = BGR(215,218,224)
    BackColor          as COLORREF = BGR(33,37,43)
    ForeColorHot       as COLORREF = BGR(204,204,204)
    BackColorHot       as COLORREF = BGR(44,49,58)
    ForeColorSelect    as COLORREF = BGR(255,255,255)
    BackColorSelect    as COLORREF = BGR(38,79,120)
    FocusAccent        as COLORREF = BGR(86,156,214)
end type
dim shared theme as THEME_TYPE



#include once "clsDoubleBuffer.inc"
#include once "CVScrollBar.inc"
#include once "CColumnHeader.inc"
#include once "CListBox.inc"
#include once "frmMain.inc"


' ========================================================================================
' WinMain
' ========================================================================================
function WinMain( _
            byval hInstance     as HINSTANCE, _
            byval hPrevInstance as HINSTANCE, _
            byval szCmdLine     as zstring ptr, _
            byval nCmdShow      as long _
            ) as long


    ' Initialize the COM library
    CoInitialize(null)

    ' Load the Segoe Fluent Icons ttf file that is used for displaying the various
    ' icons used within the editor.
    dim as DWSTRING wszFontFile 
    wszFontFile = AfxGetExePathName + "SegoeFluentIcons.ttf"
    if AddFontResourceEx(wszFontFile.vptr, FR_PRIVATE, NULL) = 0 then
        MessageBox( 0, _
                    "Unable to load application font 'SegoeFluentIcons.ttf'. Aborting application." , _
                    "Error", _
                    MB_OK or MB_ICONWARNING or MB_DEFBUTTON1 or MB_APPLMODAL )
        return 1
    end if


    ' Show the main form
    ' Initialize GDI+ (clsDoubleBuffer's rendering backend -- see DBUF_GDIPLUS). Must be
    ' running before the first WM_PAINT builds a buffer, and must outlive every one of
    ' them, so it brackets frmMain_Show.
    dim as ULONG_PTR gdipToken = AfxGdipInit()

    function = frmMain_Show( 0 )


    ' Unload the font file
    if len(wszFontFile) then RemoveFontResource(wszFontFile)

    ' Uninitialize the COM library
    ' Every window is destroyed and every clsDoubleBuffer has run its destructor by here,
    ' so no CGp* object can still be alive. Precedes CoUninitialize: GDI+ leans on COM.
    AfxGdipShutdown( gdipToken )

    CoUninitialize


end function


' ========================================================================================
' Main program entry point
' ========================================================================================
end WinMain( GetModuleHandle(null), null, command(), SW_NORMAL )

