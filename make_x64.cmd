@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cd /d "%~dp0"


echo.
echo ===================================================
echo # INSTRUMENTING
echo ===================================================
echo.

msbuild.exe /t:Rebuild "%~dp0\MPatch.sln" /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGInstrument
if %ERRORLEVEL% NEQ 0 goto:BuildError


echo.
echo ===================================================
echo # PROFILING
echo ===================================================

"%~dp0\bin\x64\Release\MPatchCLI.exe" -c "%~dp0\etc\opusenc.new.exe" "%~dp0\etc\opusenc.old.exe" "%TMP%\~%RANDOM%%RANDOM%.patch"
if %ERRORLEVEL% NEQ 0 goto:BuildError


echo.
echo ===================================================
echo # BUILDING (PGO)
echo ===================================================
echo.

msbuild.exe /t:LibLinkOnly "%~dp0\MPatch.sln" /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGOptimize /p:LinkTimeCodeGeneration=PGOptimization /p:ProfileGuidedDatabase=""%~dp0\bin\x64\Release\MPatchCLI.pgd"
if %ERRORLEVEL% NEQ 0 goto:BuildError

echo.
echo Completed.
pause
goto:eof


:BuildError
echo.
echo Whoops, something went wrong !!!
pause
