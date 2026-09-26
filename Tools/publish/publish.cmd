@echo off
rem Package the client, zip it and put it on Cloudflare R2 for testers (B-17 L2, first slice).
rem   publish.cmd                        package, then publish the next version (0.1.N)
rem   publish.cmd --notes "What changed" add notes for testers
rem   publish.cmd --skip-package         publish the build already in Valhalla2\Saved\Perf\pkg
rem   publish.cmd --dry-run              do everything except the upload
rem Local copies go to ..\Valhalla-Builds next to the repo folder (outside the repository), or
rem VALHALLA_BUILDS_DIR in secrets.local.env. Needs the R2_* lines there and boto3 (see README.md).
setlocal
set "PY=python"
where py >nul 2>&1 && set "PY=py -3"
%PY% "%~dp0publish_zip.py" %*
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
