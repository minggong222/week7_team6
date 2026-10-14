@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0RoundObjDecimals.ps1" %*
set "result=%errorlevel%"
echo.
if not "%result%"=="0" echo OBJ conversion failed. See the messages above.
pause
exit /b %result%
