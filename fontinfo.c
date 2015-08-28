#include "fontinfo.h"


/* ------------------------------------------------------------------------- *
 * Main program (including initialization, message loop, and final cleanup)  *
 * ------------------------------------------------------------------------- */
int main( int argc, char *argv[] )
{
    FIGLOBAL global = {0};
    HAB      hab;                          // anchor block handle
    HMQ      hmq;                          // message queue handle
    HWND     hwndFrame,                    // window handle
             hwndAccel;                    // acceleration table
    QMSG     qmsg;                         // message queue
    HATOMTBL hSATbl;                       // handle to system atom table
    CHAR     szError[ SZRES_MAXZ ] = "Error";
    BOOL     fInitFailure = FALSE;
    ULONG    ulrc;
    FT_Error error;

/*
    if ( argc < 2 ) {
        printf("Syntax: FONTINFO <fontfile>\n");
        return 0;
    }
*/

    hab = WinInitialize( 0 );
    if ( hab == NULLHANDLE ) {
        sprintf( szError, "WinInitialize() failed.");
        fInitFailure = TRUE;
    }

    if ( ! fInitFailure ) {
        hmq = WinCreateMsgQueue( hab, 0 );
        if ( hmq == NULLHANDLE ) {
            sprintf( szError, "Unable to create message queue:\nWinGetLastError() = 0x%X\n", WinGetLastError(hab) );
            fInitFailure = TRUE;
        }
    }

    // Initialize FreeType
    error = FT_Init_FreeType( &global.library );
    if ( error ) {
        sprintf( szError, "An error occurred during FreeType library initialization: 0x%X\n", (int) error );
        fInitFailure = TRUE;
    }
    // Create a face object from the font file.
    if ( argc > 1 ) {
        error = FT_New_Face( global.library, argv[1], 0, &global.face );
        if ( error ) {
            if ( error == FT_Err_Unknown_File_Format )
                sprintf( szError, "The file \"%s\" is not in a supported format.", argv[1] );
            else
                sprintf( szError, "The file \"%s\" could not be loaded.\n", argv[1] );
            ErrorPopup( szError );
            global.face = NULL;
        }
        else strncpy( global.szFile, argv[1], CCHMAXPATH-1 );
    }

    if ( ! fInitFailure ) {

        global.hab   = hab;
        global.hmq   = hmq;
        global.uconv = NULL;

        // ULS conversion object for Unicode/UCS <-> internal Unicode processing format
        ulrc = UniCreateUconvObject( (UniChar *)L"IBM-1207@path=no,map=display", &(global.uconv1207) );
        if ( ulrc != ULS_SUCCESS ) global.uconv1207 = NULL;

        // ULS conversion object for font UCS text <-> current display codepage
        ulrc = UniCreateUconvObject( (UniChar *)L"@endian=big,path=no,map=display", &(global.uconv) );
        if ( ulrc != ULS_SUCCESS ) global.uconv = NULL;

        // Make sure the Unicode clipboard format is registered
        hSATbl = WinQuerySystemAtomTable();
        global.cfUniText = WinAddAtom( hSATbl, "text/unicode");

        // Create custom controls & load the main dialog window
        WinRegisterClass( hab, "GlyphPreview", PreviewWndProc, CS_SIZEREDRAW, sizeof(PGLYPRCDATA) );
        hwndFrame = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP,
                                MainWndProc, 0, ID_FONTINFO, &global );
        if ( hwndFrame == NULLHANDLE ) {
            sprintf( szError, "Failed to load dialog resource:\nWinGetLastError() = 0x%X\n", WinGetLastError(hab) );
            fInitFailure = TRUE;
        }
    }

    if ( fInitFailure ) {
        WinMessageBox( HWND_DESKTOP, HWND_DESKTOP, szError, "Program Initialization Error", 0, MB_CANCEL | MB_ERROR );
    }
    else {

        // Initialize acceleration table
        hwndAccel = WinLoadAccelTable( hab, 0, ID_FONTINFO );
        WinSetAccelTable( hab, hwndAccel, hwndFrame );

        // Now run the main program message loop
        while ( WinGetMsg( hab, &qmsg, 0, 0, 0 )) WinDispatchMsg( hab, &qmsg );

    }

    // Clean up and exit
    if ( global.uconv1207 != NULL ) UniFreeUconvObject( global.uconv1207 );
    if ( global.uconv     != NULL ) UniFreeUconvObject( global.uconv     );
    WinDeleteAtom( hSATbl, global.cfUniText );
    WinDestroyWindow( hwndFrame );
    WinDestroyMsgQueue( hmq );
    WinTerminate( hab );

    if ( global.face )
        FT_Done_Face( global.face );

done:
    FT_Done_FreeType( global.library );
    return (int) error;

    return ( 0 );
}


/* ------------------------------------------------------------------------- *
 * Window procedure for the main program dialog.                             *
 * ------------------------------------------------------------------------- */
MRESULT EXPENTRY MainWndProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
    static PFIGLOBAL pGlobal;
    PFNRECORD pRec;
    FILEDLG   fd;
    HWND      hwndFD;
    PSWP      pswp;
    LONG      cmap,
              glyph;
    FT_Error  error;
    CHAR      szError[ SZRES_MAXZ ] = "Error";


    switch( msg ) {

        case WM_INITDLG:
            pGlobal = (PFIGLOBAL) mp2;
            WinSetWindowPtr( hwnd, 0, pGlobal );
            WindowSetup( hwnd );

            if ( pGlobal->face ) {
                OpenFontFile( hwnd, pGlobal );
            }

            WinShowWindow( hwnd, TRUE );
            return (MRESULT) FALSE;


        case WM_COMMAND:
            switch( SHORT1FROMMP( mp1 )) {

                case ID_OPEN:                   // Open a file
                    memset( &fd, 0, sizeof( FILEDLG ));
                    fd.cbSize = sizeof( FILEDLG );
                    fd.fl = FDS_CENTER | FDS_OPEN_DIALOG;
                    fd.pszTitle = NULL;
                    sprintf( fd.szFullFile, "*");
                    hwndFD = WinFileDlg( HWND_DESKTOP, hwnd, &fd );
                    if ( hwndFD && fd.lReturn == DID_OK )
                    {
                        int l;
                        // sanity check in case of buggy file dialog replacement
                        if ( fd.szFullFile && (( l = strlen( fd.szFullFile )) > 0 ) &&
                             ( fd.szFullFile[ l-1 ] == '\\')
                           )
                        {
                            sprintf( szError, "The file dialog returned an invalid filename: \"%s\".", fd.szFullFile );
                            ErrorPopup( szError );
                            break;
                        }

                        if ( pGlobal->face ) {
                            // Close the existing font and clean up
                            CloseFontFile( hwnd, pGlobal );
                        }
                        // Now initialize the new font
                        error = FT_New_Face( pGlobal->library, fd.szFullFile, 0, &(pGlobal->face) );
                        if ( error ) {
                            if ( error == FT_Err_Unknown_File_Format )
                                sprintf( szError, "The file \"%s\" is not in a supported format.", fd.szFullFile );
                            else
                                sprintf( szError, "The file \"%s\" could not be loaded.\n", fd.szFullFile );
                            ErrorPopup( szError );
                            pGlobal->face = NULL;
                        }
                        else {
                            strncpy( pGlobal->szFile, fd.szFullFile, CCHMAXPATH-1 );
                            OpenFontFile( hwnd, pGlobal );
                        }
                    }
                    break;

                case ID_QUIT:                   // Exit the program
                    WinPostMsg( hwnd, WM_CLOSE, 0, 0 );
                    break;

                case ID_ABOUT:                  // Product information
                    WinMessageBox( HWND_DESKTOP, hwnd, SZ_PRODUCT_INFO, "Product Information",
                                   0, MB_OK | MB_INFORMATION | MB_MOVEABLE );
                    break;

            } // end WM_COMMAND messages
            return (MRESULT) 0;


        case WM_CONTROL:
            if ((( SHORT1FROMMP( mp1 ) == IDD_INDEX ) && ( SHORT2FROMMP( mp1 ) == SPBN_CHANGE )) ||
                (( SHORT1FROMMP( mp1 ) == IDD_CMAPS ) && ( SHORT2FROMMP( mp1 ) == CN_EMPHASIS )))
            {
                if ( pGlobal->face == NULL )
                    return (MRESULT) 0;

                pRec = (PFNRECORD) WinSendDlgItemMsg( hwnd, IDD_CMAPS, CM_QUERYRECORDEMPHASIS,
                                                      MPFROMP( CMA_FIRST ), MPFROMSHORT( CRA_SELECTED ));
                if ( !pRec || ((LONG) pRec == -1 ))
                    cmap = 0;
                else
                    cmap = pRec->number;

                WinSendDlgItemMsg( hwnd, IDD_INDEX, SPBM_QUERYVALUE,
                                   MPFROMP( &glyph ), MPFROM2SHORT( 0, SPBQ_UPDATEIFVALID ));

                WinSendDlgItemMsg( hwnd, IDD_PREVIEW, UPW_SETGLYPH, MPFROMLONG(cmap), MPFROMLONG(glyph) );
            } // end WM_CONTROL messages
            return (MRESULT) 0;


        case WM_MINMAXFRAME:
            pswp = (PSWP) mp1;
            if ( pswp->fl & SWP_MINIMIZE ) {
                WinShowWindow( WinWindowFromID(hwnd, IDD_CMAPS),  FALSE );
            } else if ( pswp->fl & SWP_RESTORE ) {
                WinShowWindow( WinWindowFromID(hwnd, IDD_CMAPS),  TRUE );
            }
            break;


        case WM_CLOSE:
            WinPostMsg( hwnd, WM_QUIT, 0, 0 );
            return (MRESULT) 0;


        case WM_DESTROY:
            // free the allocated container memory
            pRec = (PFNRECORD) WinSendDlgItemMsg( hwnd, IDD_NAMES,
                                                  CM_QUERYRECORD, NULL,
                                                  MPFROM2SHORT( CMA_FIRST,
                                                                CMA_ITEMORDER ));
            while ( pRec ) {
                if ( pRec->pszPlatform ) free( pRec->pszPlatform );
                if ( pRec->pszEncoding ) free( pRec->pszEncoding );
                if ( pRec->pszLanguage ) free( pRec->pszLanguage );
                if ( pRec->pszNameType ) free( pRec->pszNameType );
                if ( pRec->pszValue    ) free( pRec->pszValue    );
                pRec = (PFNRECORD) pRec->record.preccNextRecord;
            }
            WinSendDlgItemMsg( hwnd, IDD_NAMES, CM_REMOVERECORD, NULL,
                               MPFROM2SHORT( 0, CMA_INVALIDATE | CMA_FREE ));
            WinSendDlgItemMsg( hwnd, IDD_NAMES, CM_REMOVEDETAILFIELDINFO, NULL,
                               MPFROM2SHORT( 0, CMA_INVALIDATE | CMA_FREE ));
            break;

    } /* end event handlers */

    return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


/* ------------------------------------------------------------------------- *
 * Window procedure for the glyph-preview control.                           *
 * ------------------------------------------------------------------------- */
MRESULT EXPENTRY PreviewWndProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
    PGLYPRCDATA pgpd;
    HPS         hps;
    RECTL       rcl,                    // preview window area
                rclClip;                // glyph clipping region
    POINTL      ptl;
    BITMAPINFOHEADER2 bmp;


    switch( msg ) {

        case WM_CREATE:
            // set the control data
            if ( !( pgpd = (PGLYPRCDATA) malloc( sizeof(GLYPRCDATA) ))) return (MRESULT) TRUE;
            memset( pgpd, 0, sizeof(GLYPRCDATA) );
            WinSetWindowPtr( hwnd, 0, pgpd );
            break;

        case WM_PAINT:
            pgpd = WinQueryWindowPtr( hwnd, 0 );
            hps = WinBeginPaint( hwnd, NULLHANDLE, NULLHANDLE );
            WinQueryWindowRect( hwnd, &rcl );
            WinFillRect( hps, &rcl, SYSCLR_WINDOW );
            DrawNice3DBorder( hps, rcl );

            if ( !pgpd->glyph ) return (MRESULT) 0;

            // define the actual display area inside the borders
            rclClip.xLeft   = rcl.xLeft + 2;
            rclClip.yBottom = rcl.yBottom + 2;
            rclClip.xRight  = rcl.xRight - 3;
            rclClip.yTop    = rcl.yTop - 3;

            // now draw the glyph
            if ( pgpd->hbm ) {
                bmp.cbFix = sizeof(bmp);
                GpiQueryBitmapInfoHeader( pgpd->hbm, &bmp );
                // center the glyph horizontally
                ptl.x = (( rclClip.xRight - rclClip.xLeft ) / 2 ) - (bmp.cx / 2);
                /* position the glyph baseline at 1/3 of the height, and
                 * shift the drawing position down by the character's descender height
                 */
                ptl.y = (( rclClip.yTop - rclClip.yBottom ) / 3 ) - ( bmp.cy - pgpd->face->glyph->bitmap_top );
                WinDrawBitmap( hps, pgpd->hbm, NULL, &ptl, 0, 0, DBM_NORMAL );
            }
            WinEndPaint( hps );
            return (MRESULT) 0;

        case UPW_QUERYGLYPH:
            pgpd = WinQueryWindowPtr( hwnd, 0 );
            return (MRESULT) TRUE;

        case UPW_SETFONT:
            pgpd = WinQueryWindowPtr( hwnd, 0 );
            pgpd->face = (FT_Face) mp1;
            return (MRESULT) TRUE;

        case UPW_SETGLYPH:
            pgpd = WinQueryWindowPtr( hwnd, 0 );
            if ( pgpd->face ) {
                pgpd->cmap = (LONG)mp1;
                pgpd->glyph = (LONG)mp2;
                if ( pgpd->hbm ) GpiDeleteBitmap( pgpd->hbm );
                hps = WinBeginPaint( hwnd, NULLHANDLE, NULLHANDLE );
                pgpd->hbm = RenderPreviewGlyph( hps, pgpd->face, pgpd->cmap, pgpd->glyph );
                WinEndPaint( hps );
                WinInvalidateRect( hwnd, NULL, FALSE );
            }
            return (MRESULT) TRUE;

        case WM_DESTROY:
            if (( pgpd = WinQueryWindowPtr( hwnd, 0 )) != NULL )
                free( pgpd );
            if ( pgpd->hbm ) GpiDeleteBitmap( pgpd->hbm );
            break;
    }

    return ( WinDefWindowProc( hwnd, msg, mp1, mp2 ));
}


/* ------------------------------------------------------------------------- *
 * DrawNice3DBorder                                                          *
 *                                                                           *
 * Draw an attractive MLE-style 3D border around a control.                  *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   HPS hps  : Handle of the control's presentation space.                  *
 *   RECTL rcl: Rectangle defining the boundaries of the control.            *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void DrawNice3DBorder( HPS hps, RECTL rcl )
{
    POINTL ptl;

    GpiSetColor( hps, SYSCLR_BUTTONLIGHT );
    ptl.x = rcl.xLeft;
    ptl.y = rcl.yBottom;
    GpiMove( hps, &ptl );
    ptl.x = rcl.xRight - 1;
    GpiLine( hps, &ptl );
    ptl.y = rcl.yTop - 1;
    GpiLine( hps, &ptl );
    GpiSetColor( hps, SYSCLR_DIALOGBACKGROUND );
    ptl.x = rcl.xLeft;
    ptl.y = rcl.yBottom + 1;
    GpiMove( hps, &ptl );
    ptl.x = rcl.xRight - 2;
    GpiLine( hps, &ptl );
    ptl.y = rcl.yTop;
    GpiLine( hps, &ptl );
    GpiSetColor( hps, SYSCLR_BUTTONDARK );
    ptl.x = rcl.xLeft;
    ptl.y = rcl.yBottom + 1;
    GpiMove( hps, &ptl );
    ptl.y = rcl.yTop - 1;
    GpiLine( hps, &ptl );
    ptl.x = rcl.xRight - 1;
    GpiLine( hps, &ptl );
    GpiSetColor( hps, CLR_BLACK );
    ptl.x = rcl.xLeft + 1;
    ptl.y = rcl.yBottom + 2;
    GpiMove( hps, &ptl );
    ptl.y = rcl.yTop - 2;
    GpiLine( hps, &ptl );
    ptl.x = rcl.xRight - 3;
    GpiLine( hps, &ptl );
}


/* ------------------------------------------------------------------------- *
 * WindowSetup                                                               *
 *                                                                           *
 * Perform some initial setup on the application window.                     *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   HWND hwnd: Handle of the main application window.                       *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void WindowSetup( HWND hwnd )
{
    PFIGLOBAL       pGlobal;                 // global data
    HPOINTER        hicon;                   // main application icon
    HWND            hwndCnr;                 // container handle
    CNRINFO         cnr;                     // container setup
    PFIELDINFO      pFld,                    // ...
                    pFld1st;                 // ...
    FIELDINFOINSERT finsert;                 // ...
//    ULONG           ulCP;                    // current codepage

    pGlobal = WinQueryWindowPtr( hwnd, 0 );

    // Load the menu-bar
    WinLoadMenu( hwnd, 0, ID_FONTINFO );
    WinSendMsg( hwnd, WM_UPDATEFRAME, (MPARAM) FCF_MENU, MPVOID );

    // Set the window mini-icon
    hicon = WinLoadPointer( HWND_DESKTOP, 0, ID_FONTINFO );
    WinSendMsg( hwnd, WM_SETICON, MPFROMP(hicon), MPVOID );

    // Set up the name container
    hwndCnr = WinWindowFromID( hwnd, IDD_NAMES );
    memset( &cnr, 0, sizeof( CNRINFO ));
    cnr.flWindowAttr  = CV_DETAIL | CA_DETAILSVIEWTITLES | CA_CONTAINERTITLE | CA_TITLELEFT | CA_TITLEREADONLY;// | CA_TITLESEPARATOR;
    cnr.pszCnrTitle   = "Font Name Entries:";
    cnr.cyLineSpacing = 1;
    WinSendMsg( hwndCnr, CM_SETCNRINFO, MPFROMP( &cnr ),
                MPFROMLONG( CMA_FLWINDOWATTR | CMA_LINESPACING | CMA_CNRTITLE ));
    pFld = (PFIELDINFO) WinSendMsg( hwndCnr, CM_ALLOCDETAILFIELDINFO, MPFROMLONG( 6L ), 0 );
    pFld1st = pFld;
    // (first column: entry number)
    pFld->cb = sizeof( FIELDINFO );
    pFld->pTitleData = "";
    pFld->flData     = CFA_ULONG | CFA_FIREADONLY | CFA_VCENTER;// | CFA_SEPARATOR;
    pFld->offStruct  = FIELDOFFSET( FNRECORD, number );
    pFld = pFld->pNextFieldInfo;
    // (second column: platform)
    pFld->cb = sizeof( FIELDINFO );
    pFld->pTitleData = "Platform";
    pFld->flData     = CFA_STRING | CFA_FIREADONLY | CFA_VCENTER | CFA_HORZSEPARATOR;// | CFA_SEPARATOR;
    pFld->offStruct  = FIELDOFFSET( FNRECORD, pszPlatform );
    pFld = pFld->pNextFieldInfo;
    // (third column: encoding)
    pFld->cb = sizeof( FIELDINFO );
    pFld->pTitleData = "Encoding";
    pFld->flData     = CFA_STRING | CFA_FIREADONLY | CFA_VCENTER | CFA_HORZSEPARATOR;// | CFA_SEPARATOR;
    pFld->offStruct  = FIELDOFFSET( FNRECORD, pszEncoding );
    pFld = pFld->pNextFieldInfo;
    // (fourth column: language)
    pFld->cb = sizeof( FIELDINFO );
    pFld->pTitleData = "Language";
    pFld->flData     = CFA_STRING | CFA_FIREADONLY | CFA_VCENTER | CFA_HORZSEPARATOR;// | CFA_SEPARATOR;
    pFld->offStruct  = FIELDOFFSET( FNRECORD, pszLanguage );
    pFld = pFld->pNextFieldInfo;
    // (fifth column: name ID)
    pFld->cb = sizeof( FIELDINFO );
    pFld->pTitleData = "Name Type";
    pFld->flData     = CFA_STRING | CFA_FIREADONLY | CFA_VCENTER | CFA_HORZSEPARATOR;// | CFA_SEPARATOR;
    pFld->offStruct  = FIELDOFFSET( FNRECORD, pszNameType );
    pFld = pFld->pNextFieldInfo;
    // (sixth column: length)
    pFld->cb = sizeof( FIELDINFO );
    pFld->pTitleData = "Value";
    pFld->flData     = CFA_STRING | CFA_FIREADONLY | CFA_VCENTER | CFA_HORZSEPARATOR;// | CFA_SEPARATOR;
    pFld->offStruct  = FIELDOFFSET( FNRECORD, pszValue );
    finsert.cb                   = (ULONG) sizeof( FIELDINFOINSERT );
    finsert.pFieldInfoOrder      = (PFIELDINFO) CMA_END;
    finsert.fInvalidateFieldInfo = TRUE;
    finsert.cFieldInfoInsert     = 6;
    WinSendMsg( hwndCnr, CM_INSERTDETAILFIELDINFO, MPFROMP( pFld1st ), MPFROMP( &finsert ));

    // Set up the cmap container
    hwndCnr = WinWindowFromID( hwnd, IDD_CMAPS );
    memset( &cnr, 0, sizeof( CNRINFO ));
    cnr.flWindowAttr  = CV_DETAIL | CA_DETAILSVIEWTITLES | CA_CONTAINERTITLE | CA_TITLELEFT | CA_TITLEREADONLY;// | CA_TITLESEPARATOR;
    cnr.pszCnrTitle   = "Font CMaps:";
    cnr.cyLineSpacing = 1;
    WinSendMsg( hwndCnr, CM_SETCNRINFO, MPFROMP( &cnr ),
                MPFROMLONG( CMA_FLWINDOWATTR | CMA_LINESPACING | CMA_CNRTITLE ));
    pFld = (PFIELDINFO) WinSendMsg( hwndCnr, CM_ALLOCDETAILFIELDINFO, MPFROMLONG( 3L ), 0 );
    pFld1st = pFld;
    // (first column: cmap number)
    pFld->cb = sizeof( FIELDINFO );
    pFld->pTitleData = "";
    pFld->flData     = CFA_ULONG | CFA_FIREADONLY | CFA_VCENTER;
    pFld->offStruct  = FIELDOFFSET( FCRECORD, number );
    pFld = pFld->pNextFieldInfo;
    // (second column: platform)
    pFld->cb = sizeof( FIELDINFO );
    pFld->pTitleData = "Platform";
    pFld->flData     = CFA_STRING | CFA_FIREADONLY | CFA_VCENTER | CFA_HORZSEPARATOR;
    pFld->offStruct  = FIELDOFFSET( FCRECORD, pszPlatform );
    pFld = pFld->pNextFieldInfo;
    // (third column: encoding)
    pFld->cb = sizeof( FIELDINFO );
    pFld->pTitleData = "Encoding";
    pFld->flData     = CFA_STRING | CFA_FIREADONLY | CFA_VCENTER | CFA_HORZSEPARATOR;
    pFld->offStruct  = FIELDOFFSET( FCRECORD, pszEncoding );
    finsert.cb                   = (ULONG) sizeof( FIELDINFOINSERT );
    finsert.pFieldInfoOrder      = (PFIELDINFO) CMA_END;
    finsert.fInvalidateFieldInfo = TRUE;
    finsert.cFieldInfoInsert     = 3;
    WinSendMsg( hwndCnr, CM_INSERTDETAILFIELDINFO, MPFROMP( pFld1st ), MPFROMP( &finsert ));

    // Set up the spinbutton
    WinSendDlgItemMsg( hwnd, IDD_INDEX, SPBM_SETLIMITS,
                       MPFROMLONG( 65535 ), MPFROMLONG( 0 ));
    WinSendDlgItemMsg( hwnd, IDD_INDEX, SPBM_SETCURRENTVALUE, MPFROMLONG( 65 ), 0 );

/*
    ulCP = WinQueryCp( pGlobal->hmq );
    switch ( ulCP ) {
        case 437:
        case 813:
        case 850:
        case 852:
        case 855:
        case 856:
        case 857:
        case 859:
        case 860:
        case 861:
        case 862:
        case 863:
        case 864:
        case 865:
        case 866:
        case 869:
            WinSetDlgItemText( hwnd, ID_PREV, "");
            WinSetDlgItemText( hwnd, ID_NEXT, "");
            break;
    }
*/

    CentreWindow( hwnd );
}


/* ------------------------------------------------------------------------- *
 * CentreWindow                                                              *
 *                                                                           *
 * Centres the given window on the screen.                                   *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   HWND hwnd: Handle of the window to be centred.                          *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void CentreWindow( HWND hwnd )
{
    LONG scr_width, scr_height;
    LONG x, y;
    SWP wp;

    scr_width = WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN );
    scr_height = WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN );

    if ( WinQueryWindowPos( hwnd, &wp )) {
        x = ( scr_width - wp.cx ) / 2;
        y = ( scr_height - wp.cy ) / 2;
        WinSetWindowPos( hwnd, HWND_TOP, x, y, wp.cx, wp.cy, SWP_MOVE | SWP_ACTIVATE );
    }

}

/* ------------------------------------------------------------------------- *
 * OpenFontFile                                                              *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   HWND hwnd:         Handle of the main program window.                   *
 *   PFIGLOBAL pGlobal: Pointer to global data.                              *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void OpenFontFile( HWND hwnd, PFIGLOBAL pGlobal )
{
    PSZ  pszFile;
    CHAR szFace[ FONTNAME_MAXZ ];
    CHAR szTitle[ TITLEBAR_MAXZ ];

    pszFile = strrchr( pGlobal->szFile, '\\');
    if ( pszFile && *(pszFile+1) )
        pszFile++;
    else
        pszFile = pGlobal->szFile;
    strncpy( szTitle, "Font Information: ", TITLEBAR_MAXZ );
    strncat( szTitle, pszFile, TITLEBAR_MAXZ );
    WinSetWindowText( hwnd, szTitle );
    strncpy( szFace, pGlobal->face->family_name, FONTNAME_MAXZ-1 );
    if ( pGlobal->face->style_name ) {
        strncat( szFace, " ", FONTNAME_MAXZ-1 );
        strncat( szFace, pGlobal->face->style_name, FONTNAME_MAXZ-1 );
    }
    WinSetDlgItemText( hwnd, IDD_FONTNAME, szFace );
    PopulateNames( hwnd, pGlobal );
    PopulateCMaps( hwnd, pGlobal );
    WinSendDlgItemMsg( hwnd, IDD_PREVIEW, UPW_SETFONT,
                       MPFROMP( pGlobal->face ), MPVOID );
    WinSendDlgItemMsg( hwnd, IDD_PREVIEW, UPW_SETGLYPH,
                       MPFROMLONG(0), MPFROMLONG(65) );     // 65 == 'A'
}


/* ------------------------------------------------------------------------- *
 * CloseFontFile                                                             *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   HWND hwnd:         Handle of the main program window.                   *
 *   PFIGLOBAL pGlobal: Pointer to global data.                              *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void CloseFontFile( HWND hwnd, PFIGLOBAL pGlobal )
{
    PFNRECORD pRec;
    PFCRECORD pCRec;

    FT_Done_Face( pGlobal->face );
    pGlobal->face = NULL;
    WinSendDlgItemMsg( hwnd, IDD_PREVIEW, UPW_SETFONT, 0L, 0L );

    pRec = (PFNRECORD) WinSendDlgItemMsg( hwnd, IDD_NAMES, CM_QUERYRECORD, NULL,
                                          MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER) );
    while ( pRec ) {
        if ( pRec->pszPlatform ) free( pRec->pszPlatform );
        if ( pRec->pszEncoding ) free( pRec->pszEncoding );
        if ( pRec->pszLanguage ) free( pRec->pszLanguage );
        if ( pRec->pszNameType ) free( pRec->pszNameType );
        if ( pRec->pszValue    ) free( pRec->pszValue    );
        pRec = (PFNRECORD) pRec->record.preccNextRecord;
    }
    WinSendDlgItemMsg( hwnd, IDD_NAMES, CM_REMOVERECORD, 0,
                       MPFROM2SHORT(0, CMA_FREE | CMA_INVALIDATE) );
    pCRec = (PFCRECORD) WinSendDlgItemMsg( hwnd, IDD_CMAPS, CM_QUERYRECORD, NULL,
                                           MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER) );
    while ( pCRec ) {
        if ( pCRec->pszPlatform ) free( pCRec->pszPlatform );
        if ( pCRec->pszEncoding ) free( pCRec->pszEncoding );
        pCRec = (PFCRECORD) pCRec->record.preccNextRecord;
    }
    WinSendDlgItemMsg( hwnd, IDD_CMAPS, CM_REMOVERECORD, 0,
                       MPFROM2SHORT(0, CMA_FREE | CMA_INVALIDATE) );
}


/* ------------------------------------------------------------------------- *
 * PopulateNames                                                             *
 *                                                                           *
 * Populate the names list for the current font.                             *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   HWND hwnd:         Handle of the main program window.                   *
 *   PFIGLOBAL pGlobal: Pointer to global data.                              *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void PopulateNames( HWND hwnd, PFIGLOBAL pGlobal )
{
    FT_UInt      names;
    FT_SfntName  name;
    HWND         hwndCnr;
    PFNRECORD    pRec, pFirst;
    RECORDINSERT ri;
    ULONG        cb,
                 i;

    names = FT_Get_Sfnt_Name_Count( pGlobal->face );

    if ( names ) {
        hwndCnr = WinWindowFromID( hwnd, IDD_NAMES );

        cb = sizeof(FNRECORD) - sizeof(MINIRECORDCORE);
        pRec = (PFNRECORD) WinSendMsg( hwndCnr, CM_ALLOCRECORD,
                                       MPFROMLONG( cb ),
                                       MPFROMLONG( names ));
        pFirst = pRec;
        cb = sizeof( MINIRECORDCORE );

        for ( i = 0; i < names; i++ ) {
            if (( FT_Get_Sfnt_Name( pGlobal->face, i, &name )) != 0 ) continue;

            pRec->number   = i;
            pRec->length   = name.string_len;

            pRec->pszPlatform = (PSZ) calloc( PLATFORM_MAXZ, 1 );
            pRec->pszEncoding = (PSZ) calloc( ENCODING_MAXZ, 1 );
            pRec->pszLanguage = (PSZ) calloc( LANGUAGE_MAXZ, 1 );
            pRec->pszNameType = (PSZ) calloc( NAMETYPE_MAXZ, 1 );

            PlatformAndEncoding( name.platform_id, pRec->pszPlatform, PLATFORM_MAXZ-1,
                                 name.encoding_id, pRec->pszEncoding, ENCODING_MAXZ-1 );
            sprintf( pRec->pszLanguage, "0x%X", name.language_id );
            NameRecordType( name.name_id, pRec->pszNameType, NAMETYPE_MAXZ-1 );

            if (( name.platform_id == 1 ) && ( name.encoding_id == 0 ) &&
                ( name.string_len < NAMETEXT_MAXZ ))
            {
                pRec->pszValue = (PSZ) calloc( name.string_len+1, 1 );
                if ( pRec->pszValue )
                    strncpy( pRec->pszValue, name.string, name.string_len );
                else
                    pRec->pszValue = "";
            }
            else if (( name.platform_id == 3 ) && ( name.encoding_id == 1 ) &&
                     ( pGlobal->uconv && ( name.string_len < (2 * NAMETEXT_MAXZ) )))
            {
                ConvertUnicodeName( pGlobal->uconv, name.string, name.string_len, &(pRec->pszValue) );
                if ( !pRec->pszValue ) pRec->pszValue = "";
            }
            else {
                pRec->pszValue = (PSZ) calloc( NAMETEXT_MAXZ, 1 );
                sprintf( pRec->pszValue, "(%u bytes)", name.string_len );
            }

            pRec->record.cb = cb;
            pRec->record.flRecordAttr = CRA_RECORDREADONLY;
            pRec->record.pszIcon = NULL;
            pRec->record.hptrIcon = NULLHANDLE;
            pRec = (PFNRECORD) pRec->record.preccNextRecord;
        }

        ri.cb                = sizeof( RECORDINSERT );
        ri.pRecordOrder      = (PRECORDCORE) CMA_END;
        ri.pRecordParent     = NULL;
        ri.zOrder            = (ULONG) CMA_TOP;
        ri.fInvalidateRecord = TRUE;
        ri.cRecordsInsert    = names;

        WinSendMsg( hwndCnr, CM_INSERTRECORD, MPFROMP( pFirst ), MPFROMP( &ri ));
    }
}


/* ------------------------------------------------------------------------- *
 * PopulateCMaps                                                             *
 *                                                                           *
 * Populate the cmaps list for the current font.                             *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   HWND hwnd:         Handle of the main program window.                   *
 *   PFIGLOBAL pGlobal: Pointer to global data.                              *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void PopulateCMaps( HWND hwnd, PFIGLOBAL pGlobal )
{
    FT_CharMap   charmap;
    HWND         hwndCnr;
    PFCRECORD    pRec, pFirst;
    RECORDINSERT ri;
    ULONG        cb,
                 i;

    if ( pGlobal->face->num_charmaps ) {
        hwndCnr = WinWindowFromID( hwnd, IDD_CMAPS );

        cb = sizeof(FCRECORD) - sizeof(MINIRECORDCORE);
        pRec = (PFCRECORD) WinSendMsg( hwndCnr, CM_ALLOCRECORD,
                                       MPFROMLONG( cb ),
                                       MPFROMLONG( pGlobal->face->num_charmaps ));
        pFirst = pRec;
        cb = sizeof( MINIRECORDCORE );

        for ( i = 0; i < pGlobal->face->num_charmaps; i++ ) {
            charmap = pGlobal->face->charmaps[i];

            pRec->number   = i;
            pRec->pszPlatform = (PSZ) calloc( PLATFORM_MAXZ, 1 );
            pRec->pszEncoding = (PSZ) calloc( ENCODING_MAXZ, 1 );
            PlatformAndEncoding( charmap->platform_id, pRec->pszPlatform, PLATFORM_MAXZ-1,
                                 charmap->encoding_id, pRec->pszEncoding, ENCODING_MAXZ-1 );

            pRec->record.cb = cb;
            pRec->record.flRecordAttr = CRA_RECORDREADONLY;
            pRec->record.pszIcon = NULL;
            pRec->record.hptrIcon = NULLHANDLE;
            pRec = (PFCRECORD) pRec->record.preccNextRecord;
        }

        ri.cb                = sizeof( RECORDINSERT );
        ri.pRecordOrder      = (PRECORDCORE) CMA_END;
        ri.pRecordParent     = NULL;
        ri.zOrder            = (ULONG) CMA_TOP;
        ri.fInvalidateRecord = TRUE;
        ri.cRecordsInsert    = pGlobal->face->num_charmaps;

        WinSendMsg( hwndCnr, CM_INSERTRECORD, MPFROMP( pFirst ), MPFROMP( &ri ));
    }
}


/* ------------------------------------------------------------------------- *
 * PlatformAndEncoding                                                       *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   ULONG pid   : Platform ID (I)                                           *
 *   PSZ   pszPID: Allocated buffer for platform string (O)                  *
 *   ULONG cbPID : Maximum length of platform string (I)                     *
 *   ULONG eid   : Encoding ID (I)                                           *
 *   PSZ   pszEID: Allocated buffer for encoding string (O)                  *
 *   ULONG cbEID : Maximum length of encoding string (I)                     *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void PlatformAndEncoding( ULONG pid, PSZ pszPID, ULONG cbPID, ULONG eid, PSZ pszEID, ULONG cbEID )
{

    switch ( pid ) {
        case 0:
            strncpy( pszPID, "Unicode", cbPID );
            switch ( eid ) {
                case 0:  strncpy( pszEID, "default", cbEID );       break;
                case 1:  strncpy( pszEID, "1.1", cbEID );           break;
                case 3:  strncpy( pszEID, "2+", cbEID );            break;
                case 4:  strncpy( pszEID, "3.1+ (UTF-32)", cbEID ); break;
                default: strncpy( pszEID, "unknown", cbEID );       break;
            }
            break;

        case 1:
            strncpy( pszPID, "Macintosh", cbPID );
            switch ( eid ) {
                case 0:  strncpy( pszEID, "Roman", cbEID );       break;
                case 1:  strncpy( pszEID, "Japanese", cbEID );    break;
                case 2:  strncpy( pszEID, "Chinese-T", cbEID );   break;
                case 3:  strncpy( pszEID, "Korean", cbEID );      break;
                case 8:  strncpy( pszEID, "Symbols", cbEID );     break;
                case 25: strncpy( pszEID, "Chinese-S", cbEID );   break;
                default: strncpy( pszEID, "unknown", cbEID );     break;
            }
            break;

        case 3:
            strncpy( pszPID, "Windows", cbPID );
            switch ( eid ) {
                case 0:  strncpy( pszEID, "Symbols", cbEID );          break;
                case 1:  strncpy( pszEID, "Unicode", cbEID );          break;
                case 2:  strncpy( pszEID, "Shift-JIS, Japan", cbEID ); break;
                case 3:  strncpy( pszEID, "GB2312, China", cbEID );    break;
                case 4:  strncpy( pszEID, "Big5, Taiwan", cbEID );     break;
                case 5:  strncpy( pszEID, "Wansung, Korea", cbEID );   break;
                case 6:  strncpy( pszEID, "Johab, Korea", cbEID );     break;
                case 10: strncpy( pszEID, "UCS-4", cbEID );            break;
                default: strncpy( pszEID, "unknown", cbEID );          break;
            }
            break;

        default:
            strncpy( pszPID, "unknown", cbPID );
            strncpy( pszEID, "unknown", cbEID );
            break;
    }

}


/* ------------------------------------------------------------------------- *
 * NameRecordType                                                            *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   ULONG id     : Name type ID (I)                                         *
 *   PSZ   pszText: Allocated buffer for name value string (O)               *
 *   ULONG cbText : Maximum length of name value string (I)                  *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void NameRecordType( ULONG id, PSZ pszText, ULONG cbText )
{
    switch ( id ) {
        case 0:  strncpy( pszText, "Copyright", cbText );                        break;
        case 1:  strncpy( pszText, "Family Name", cbText );                      break;
        case 2:  strncpy( pszText, "Style Name", cbText );                       break;
        case 3:  strncpy( pszText, "Unique ID", cbText );                        break;
        case 4:  strncpy( pszText, "Full Name", cbText );                        break;
        case 5:  strncpy( pszText, "Version", cbText );                          break;
        case 6:  strncpy( pszText, "PostScript Name", cbText );                  break;
        case 7:  strncpy( pszText, "Trademark", cbText );                        break;
        case 8:  strncpy( pszText, "Foundry Name", cbText );                     break;
        case 9:  strncpy( pszText, "Designer Name", cbText );                    break;
        case 10: strncpy( pszText, "Description", cbText );                      break;
        case 11: strncpy( pszText, "Foundry URL", cbText );                      break;
        case 12: strncpy( pszText, "Designer URL", cbText );                     break;
        case 13: strncpy( pszText, "License", cbText );                          break;
        case 14: strncpy( pszText, "License URL", cbText );                      break;
        case 16: strncpy( pszText, "Typographic Family Name", cbText );          break;
        case 17: strncpy( pszText, "Typographic Subfamily Name", cbText );       break;
        case 18: strncpy( pszText, "Compatible Full Name (Macintosh)", cbText ); break;
        case 19: strncpy( pszText, "Sample Text", cbText );                      break;
        case 20: strncpy( pszText, "CID Font Name", cbText );                    break;
        case 21: strncpy( pszText, "WWS Family Name", cbText );                  break;
        case 22: strncpy( pszText, "WWS Subfamily Name", cbText );               break;
        case 23: strncpy( pszText, "Light Background Palette", cbText );         break;
        case 24: strncpy( pszText, "Dark Background Palette", cbText );          break;
        default: strncpy( pszText, "unknown", cbText );                          break;
    }
}


/* ------------------------------------------------------------------------- *
 * ConvertUnicodeName                                                        *
 *                                                                           *
 * Convert a Unicode (UCS-2) name string to a codepage string. The output    *
 * buffer will be allocated by this function.                                *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *   UconvObject uconv      : Conversion object for the current codepage (I) *
 *   PCH         pchInput   : Input UCS-2 string to be converted (I)         *
 *   ULONG       length     : Length of input string in bytes (I)            *
 *   PSZ         *ppszOutput: Pointer to output buffer (O)                   *
 *                                                                           *
 * RETURNS: N/A                                                              *
 * ------------------------------------------------------------------------- */
void ConvertUnicodeName( UconvObject uconv, PCH pchInput, ULONG length, PSZ *ppszOutput )
{
    PSZ pszOutput,
        psz;
    UniChar *puInput,
            *pu;
    size_t stIn, stOut, stSub;

    stIn  = length / sizeof(UniChar);
    stOut = stIn * 4;
    stSub = 0;

    if (( puInput = (UniChar *) calloc( length+sizeof(UniChar), 1 )) == NULL )
        return;
    UniStrncpy( puInput, (UniChar *) pchInput, length / sizeof(UniChar) );
    pszOutput = (PSZ) calloc( stOut+1, 1 );
    pu  = puInput;
    psz = pszOutput;
    if ( psz && ( UniUconvFromUcs( uconv, &pu, &stIn, (PPVOID) &psz,
                                   &stOut, &stSub ) == ULS_SUCCESS ))
        *ppszOutput = pszOutput;
    else
        *ppszOutput = NULL;

    free( puInput );
}


/* ------------------------------------------------------------------------- *
 * RenderPreviewGlyph                                                        *
 *                                                                           *
 * Generate a displayable OS/2 bitmap out of the requested font glyph.       *
 *                                                                           *
 * ARGUMENTS:                                                                *
 *                                                                           *
 * RETURNS: HBITMAP                                                          *
 *   Handle of the created OS/2 bitmap                                       *
 * ------------------------------------------------------------------------- */
HBITMAP RenderPreviewGlyph( HPS hps, FT_Face face, LONG charmap, LONG glyph )
{
    USHORT             i;
    LONG               gindex;
    BITMAPINFOHEADER2  bmp;
    BITMAPINFO2       *pbmi;
    HBITMAP            hbm;
    UCHAR             *buf;
    FT_Error           error = 0;

    // Select the indicated index values and load the corresponding glyph
    error = FT_Set_Charmap( face, face->charmaps[charmap] );
    if ( error ) return NULLHANDLE;
    error = FT_Set_Char_Size( face, 0, 72*64, 120, 120 );
    if ( error ) return NULLHANDLE;
    gindex = FT_Get_Char_Index( face, glyph );
    if ( !gindex ) return NULLHANDLE;
    error = FT_Load_Glyph( face, gindex, FT_LOAD_DEFAULT );
    if ( error ) return NULLHANDLE;

    // Generate a FreeType-format glyph bitmap
    error = FT_Render_Glyph( face->glyph, FT_RENDER_MODE_NORMAL );
    if ( error ) return NULLHANDLE;

    // Create an OS/2 bitmap and convert the FreeType bitmap
    bmp.cbFix = sizeof(bmp);
    bmp.cx = NEAREST_LONGINT_BOUNDARY( face->glyph->bitmap.pitch );
    bmp.cy = face->glyph->bitmap.rows;
    bmp.cPlanes = 1;
    bmp.cBitCount = 8;
    pbmi = (PBITMAPINFO2) malloc( sizeof(BITMAPINFO2) + (255 * sizeof(RGB2)));
    if (!pbmi) return NULLHANDLE;
    memset( pbmi, 0, sizeof(BITMAPINFO2) + (255 * sizeof(RGB2)));
    pbmi->cbFix = sizeof(bmp);
    pbmi->cx = bmp.cx;
    pbmi->cy = bmp.cy;
    pbmi->cPlanes = bmp.cPlanes;
    pbmi->cBitCount = bmp.cBitCount;
    for ( i = 0; i < 256; i++ ) {
        pbmi->argbColor[i].bBlue  = 255-i;
        pbmi->argbColor[i].bGreen = 255-i;
        pbmi->argbColor[i].bRed   = 255-i;
    }
    buf = (PUCHAR) malloc( bmp.cx * bmp.cy );
    ConvertBitmap( face->glyph->bitmap.buffer, buf, bmp.cy, face->glyph->bitmap.pitch, bmp.cx );
    hbm = GpiCreateBitmap( hps, &bmp, CBM_INIT, buf, pbmi );
    free( buf );
    free( pbmi );

    return ( hbm );
}


/* ------------------------------------------------------------------------ *
 * ConvertBitmap                                                            *
 *                                                                          *
 * Copy a glyph bitmap from source to target buffer, converting it to an    *
 * OS/2 PM-format bitmap with the specified number of bytes per row (a.k.a. *
 * 'pitch').  The target buffer's pitch (t_pitch) must be equal to or       *
 * greater than that of the source buffer (s_pitch).                        *
 *                                                                          *
 * The target buffer must have been allocated previously.  It must have an  *
 * allocated length of at least (rows * t_pitch) bytes.                     *
 *                                                                          *
 * ------------------------------------------------------------------------ */
BOOL ConvertBitmap( PUCHAR pSource, PUCHAR pTarget, ULONG rows, ULONG s_pitch, ULONG t_pitch )
{
    ULONG row, col, ofs;

    if (( pSource && !pTarget ) || ( t_pitch < s_pitch ))     // sanity check
        return FALSE;
    ofs = 0;
    for ( row = 0; row < rows; row++ ) {
        for ( col = 0; col < s_pitch; col++ ) {
            pTarget[ ofs++ ] = pSource[ col + ((rows-row-1) * s_pitch) ];
        }
        for ( ; col < t_pitch; col++ ) pTarget[ ofs++ ] = 0;
    }
    return TRUE;
}


