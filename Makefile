# IBM C/C++ (VisualAge) Makefile for DBCSMAP, English version.
#
# To compile other languages, change 'NLV' (below) to the appropriate country
# code.  That NLV directory must exist and must have the correct language files.
#

CC     = icc.exe
RC     = rc.exe
LINK   = ilink.exe
IPFC   = ipfc.exe
CFLAGS = /Gm /Ss /Q+ /Wuse      # /Wrea /Wuni
RFLAGS = -x -n
LFLAGS = /NOE /PMTYPE:PM /NOLOGO /MAP
NAME   = fontinfo
OBJS   = $(NAME).obj
LIBS   = libconv.lib libuls.lib freetype.lib

DEBUG=1

!ifndef NLV
    NLV = 001
!endif

!ifdef DEBUG
    CFLAGS   = $(CFLAGS) /Ti /Tm
    LFLAGS   = $(LFLAGS) /DEBUG
!endif

all         : $(NAME).exe

$(NAME).exe : $(OBJS) $(NAME).h resource.h $(NAME).res
                $(LINK) $(LFLAGS) $(OBJS) $(LIBS)
                $(RC) $(RFLAGS) $(NAME).res $@

$(NAME).res : $(NAME).rc $(NAME).dlg resource.h
                $(RC) -r -n $(NAME).rc $@

clean       :
                del $(OBJS) $(NAME).res $(NAME).map $(NAME).exe >NUL

