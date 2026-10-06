@echo off
call "%~dp0Run.cmd" TestDeterministic %*
exit /b %errorlevel%
