@echo off
python "%~dp0play.py"
set "rush_exit=%errorlevel%"
if not "%rush_exit%"=="0" pause
exit /b %rush_exit%
