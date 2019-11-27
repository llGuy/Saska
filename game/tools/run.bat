@echo off

set DB=devenv

set CLIENT_BIN=..\binaries\Saska.exe
set SERVER_BIN=..\binaries\Server.exe

If "%1" == "debug_single_client_server" goto debug_single_client_server
If "%1" == "debug_multiple_clients_server" goto debug_multiple_clients_server
If "%1" == "run_multiple_clients_server" goto run_multiple_clients_server

:debug_single_client_server
%DB% %CLIENT_BIN%
%DB% %SERVER_BIN%
goto eof

:debug_multiple_clients_server
%DB% %CLIENT_BIN%
%DB% %SERVER_BIN%
%DB %%CLIENT_BIN%
goto eof

:run_multiple_clients_server
%CLIENT_BIN%
%SERVER_BIN%
%CLIENT_BIN%
goto eof

:eof

