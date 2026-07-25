@echo off
setlocal enabledelayedexpansion

set BUILD_DIR=%~dp0build\bin\Release

if not exist "%BUILD_DIR%\test_shmem.exe" (
    echo [ERROR] test_shmem.exe not found. Run build_release.bat first.
    exit /b 1
)

echo ============================================
echo  Running test_shmem.exe
echo ============================================

"%BUILD_DIR%\test_shmem.exe"

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] All tests passed.
) else (
    echo.
    echo [FAILED] Some tests failed.
    exit /b 1
)

endlocal
