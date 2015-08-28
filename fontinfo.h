#define INCL_DOSERRORS
#define INCL_DOSMISC
#define INCL_DOSRESOURCES
#define INCL_GPI
#define INCL_WIN
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uconv.h>
#include <unidef.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_TABLES_H

#include "ft2table.h"
#include "resource.h"

// ----------------------------------------------------------------------------
// CONSTANTS

#define SZ_PRODUCT_INFO         "Font Information Tool v0.1\r\n\r\n(C)2015 Alex Taylor\r\n\r\n"

// min. size of the program window
#define US_MIN_WIDTH            333
#define US_MIN_HEIGHT           195

// Maximum string length...
#define SZRES_MAXZ              256     // ...of a generic string resource
#define TITLEBAR_MAXZ           80      // ...of the titlebar string
#define FONTNAME_MAXZ           128     // ...of the default font name string
#define PLATFORM_MAXZ           16      // ...of a platform string
#define ENCODING_MAXZ           32      // ...of an encoding string
#define LANGUAGE_MAXZ           32      // ...of a language string
#define NAMETYPE_MAXZ           32      // ...of a name entry type
#define NAMETEXT_MAXZ           128     // ...of a name value

// The Unicode UCS-2 codepage
#define UNICODE                 1200
// Codepage 1207 is used for drawing Unicode text under Presentation Manager
#define PM_UNICODE              1207

// custom messages for glyph preview window
#define UPW_SETGLYPH          ( WM_USER + 101 )
#define UPW_QUERYGLYPH        ( WM_USER + 102 )
#define UPW_SETFONT           ( WM_USER + 103 )


// ----------------------------------------------------------------------------
// MACROS

// Handy message box for errors and debug messages
#define ErrorPopup( text ) \
    WinMessageBox( HWND_DESKTOP, HWND_DESKTOP, text, "Error", 0, MB_OK | MB_ERROR )

// Convert a pair of bytes to a UniChar
#define BYTES2UNICHAR( bFirst, bSecond ) \
    (( bFirst << 8 ) | bSecond )

// See if byte flag from UlsQueryUconvObject() indicates a valid leading byte
#define ISLEADINGBYTE( bVal ) \
    (( bVal > 1 && bVal < 5 ) ? TRUE : FALSE )

#define NEAREST_LONGINT_BOUNDARY(a) (((a) + 3)& 0xfffffffc)

#define PIXGRID_ROUND( x )     (( x + 32 ) & -64 )

// ----------------------------------------------------------------------------
// TYPEDEFS

typedef struct _Global_Data {
    HAB         hab;                                // anchor-block handle
    HMQ         hmq;                                // main message queue
    CHAR        szFile[ CCHMAXPATH ];               // current font file
    ATOM        cfUniText;                          // text/unicode clipboard format
    UconvObject uconv1207,                          // conversion object for UPF-8
                uconv;                              // conversion object for current codepage
    HBITMAP     hbm;                                // currently-displayed bitmap
    FT_Library  library;
    FT_Face     face;
    FT_FaceRec  props;
} FIGLOBAL, *PFIGLOBAL;


// Record structure for the name list
typedef struct _Font_Name_Record {
    MINIRECORDCORE record;                          // standard data
    ULONG number,                                   // name index
          length;                                   // string length
    PSZ   pszPlatform,                              // platform
          pszEncoding,                              // encoding
          pszLanguage,                              // language
          pszNameType,                              // name type
          pszValue;                                 // string text
} FNRECORD, *PFNRECORD;


// Record structure for the cmap list
typedef struct _Font_CMap_Record {
    MINIRECORDCORE record;                          // standard data
    ULONG number;                                   // cmap index
    PSZ   pszPlatform,                              // platform
          pszEncoding;                              // encoding
} FCRECORD, *PFCRECORD;


// Control data for the glyph preview window
typedef struct _Preview_Data {
    FT_Face     face;                               // pointer to font face object
    LONG        cmap;                               // index of the current cmap
    LONG        glyph;                              // index of the current glyph within cmap
    HBITMAP     hbm;                                // current bitmap data
    LONG        offset;                             // vertical offset from baseline
} GLYPRCDATA, *PGLYPRCDATA;


// ----------------------------------------------------------------------------
// FUNCTIONS

MRESULT EXPENTRY MainWndProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 );
MRESULT EXPENTRY PreviewWndProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 );
void DrawNice3DBorder( HPS hps, RECTL rcl );
void WindowSetup( HWND hwnd );
void CentreWindow( HWND hwnd );
void OpenFontFile( HWND hwnd, PFIGLOBAL pGlobal );
void CloseFontFile( HWND hwnd, PFIGLOBAL pGlobal );
void PopulateNames( HWND hwnd, PFIGLOBAL pGlobal );
void PopulateCMaps( HWND hwnd, PFIGLOBAL pGlobal );
void PlatformAndEncoding( ULONG pid, PSZ pszPID, ULONG cbPID, ULONG eid, PSZ pszEID, ULONG cbEID );
void NameRecordType( ULONG id, PSZ pszText, ULONG cbText );
void ConvertUnicodeName( UconvObject uconv, PCH pchInput, ULONG length, PSZ *ppszOutput );
HBITMAP RenderPreviewGlyph( HPS hps, FT_Face face, LONG charmap, LONG glyph );
BOOL ConvertBitmap( PUCHAR pSource, PUCHAR pTarget, ULONG rows, ULONG s_pitch, ULONG t_pitch );


