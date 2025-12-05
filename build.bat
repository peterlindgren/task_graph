@echo off

if not defined VCINSTALLDIR call "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"

pushd "%~dp0"
if not exist build mkdir build
pushd build
call cl.exe /nologo /EHsc /MT /Zi %* ..\main.cpp ..\task_graph.cpp
popd
popd
