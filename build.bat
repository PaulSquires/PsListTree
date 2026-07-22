@echo off
rem ========================================================================================
rem  clsDoubleBuffer - demo harness build script
rem
rem  Usage:  build.bat              builds with the default backend (set in clsDoubleBuffer.bi)
rem          build.bat gdi         builds forcing DBUF_BACKEND_GDI
rem          build.bat gdiplus     ... DBUF_BACKEND_GDIPLUS
rem          build.bat d2d         ... DBUF_BACKEND_D2D
rem          build.bat clean       deletes build output
rem
rem  Selecting the backend from here rather than by editing clsDoubleBuffer.bi is
rem  deliberate. The first attempt at an A/B/C comparison switched backends by rewriting
rem  that #define with a regex; the regex silently failed to match, and three "successful"
rem  builds of three different backends were the same backend three times. Passing -d
rem  cannot fail that way -- and if it ever did, the exactly-one #error in the header
rem  catches it rather than producing a mislabelled result.
rem ========================================================================================

setlocal

rem fbc is not on PATH in this workspace; the toolchain is vendored under tiko_editor.
set FBC="C:\dev\tiko_editor\toolchains\FreeBASIC-1.10.1-winlibs-gcc-9.3.0\fbc64.exe"

rem Sources include AfxNova as "AfxNova\...", i.e. relative to the workspace root, so the
rem include path must be C:\dev or every AfxNova include fails to resolve.
set INCLUDE_ROOT="C:\dev"

rem main.rc embeds main_manifest.xml as resource type 24 (RT_MANIFEST). That manifest is
rem what makes the process DPI-AWARE. Without it Windows reports 96 DPI whatever the user's
rem scaling is and bitmap-stretches the window -- which silently invalidates every geometry
rem assertion that derives a scaled pen width, and makes any font-quality comparison a
rem measurement of a stretched bitmap. fbc compiles a .rc listed on the command line and
rem links the result automatically.
set RESOURCE=main.rc

if /i "%~1"=="clean" (
    if exist main.exe del main.exe
    if exist main.o   del main.o
    if exist main.obj del main.obj
    echo Cleaned.
    goto :eof
)

if not exist %FBC% (
    echo ERROR: fbc64.exe not found at %FBC%
    exit /b 1
)

set BACKEND_ARG=
set BACKEND_LABEL=default (see clsDoubleBuffer.bi)
if /i "%~1"=="gdi"     ( set BACKEND_ARG=-d DBUF_BACKEND_GDI     & set BACKEND_LABEL=GDI     )
if /i "%~1"=="gdiplus" ( set BACKEND_ARG=-d DBUF_BACKEND_GDIPLUS & set BACKEND_LABEL=GDI+    )
if /i "%~1"=="d2d"     ( set BACKEND_ARG=-d DBUF_BACKEND_D2D     & set BACKEND_LABEL=D2D     )

if not "%~1"=="" if "%BACKEND_ARG%"=="" (
    echo ERROR: unknown argument "%~1" -- expected gdi ^| gdiplus ^| d2d ^| clean
    exit /b 1
)

if not exist %RESOURCE% (
    echo ERROR: %RESOURCE% not found -- the app would build DPI-unaware and the
    echo        PaintLine assertions would pass for the wrong reason.
    exit /b 1
)

echo Building main.exe   [backend: %BACKEND_LABEL%]
%FBC% -i %INCLUDE_ROOT% %BACKEND_ARG% main.bas %RESOURCE%
if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    exit /b 1
)
echo Build OK.

endlocal
