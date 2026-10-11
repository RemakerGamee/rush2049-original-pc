@echo off
cd /d "%~dp0"
python tools\release\setup_original.py
if errorlevel 1 pause
