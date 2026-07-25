@echo off
setlocal enabledelayedexpansion

set MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe
set SLN=%~dp0vc17\pyczan_shmem.sln

echo ============================================
echo  Building pyczan_shmem (Debug)
echo ============================================
echo.

:: Build sequentially to respect static lib dependency
"%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /p:BuildInParallel=false

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] Build completed.
) else (
    echo.
    echo [FAILED] Build error.
    exit /b 1
)

endlocal
