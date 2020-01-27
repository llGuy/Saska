@echo off

If "%1" == "clean" goto clean

:build
pushd ..\build
msbuild saska.sln
popd

goto eof

:clean
pushd ..\build
msbuild saska.sln -t:Clean
popd

goto eof

:eof
