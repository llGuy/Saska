@echo off

set CC=cl

set DB=devenv

set CFLAGS=-Zi /EHsc /std:c++latest /DEBUG:FULL

set DEF=/DGLM_ENABLE_EXPERIMENTAL /DUNITY_BUILD /DSTB_IMAGE_IMPLEMENTATION /D_MBCS

set INC=/I C:/dependencies/ /I C:/dependencies/xml_parser

set BIN=Converter.exe

set SRC=converter.cpp

set LIBS=user32.lib User32.lib Gdi32.lib Shell32.lib kernel32.lib gdi32.lib msvcrt.lib msvcmrt.lib

If "%1" == "compile" goto :compile
If "%1" == "debug" goto :debug
If "%1" == "clean" goto :clean
If "%1" == "run" goto :run
If "%1" == "help" goto :help

:compile
%CC% %CFLAGS% %DEF% %INC% /Fe%BIN% %SRC% %LIBS%
goto :eof

:debug
%DB% %BIN%
goto :eof

:clean
rm *.exe *.obj *.ilk *.pdb
goto :eof

:run
%BIN%
goto :eof

:help
echo To build application, enter into command line: build.bat compile
echo To debug application, enter into command line: build.bat debug
echo To run application, enter into command line: build.bat run
echo To clean application, enter into command line: build.bat clean

:eof
