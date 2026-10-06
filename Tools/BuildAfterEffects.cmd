@echo off
call "%~dp0Run.cmd" BuildAfterEffects %*
exit /b %errorlevel%
