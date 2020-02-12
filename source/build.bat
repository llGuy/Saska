@echo off

If "%1" == "clean" goto clean

:build
pushd ..\build
msbuild /nologo saska.sln
popd

goto eof

:clean
pushd ..\build
msbuild /nologo saska.sln -t:Clean
popd

goto eof

:eof
