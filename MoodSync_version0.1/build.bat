@echo off
echo =====================================================
echo   MoodSync v0.1 - Build C++ Backend
echo   Compiler: g++ (MinGW/MSVC required)
echo =====================================================

REM Cek apakah g++ tersedia
where g++ >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNING] g++ tidak ditemukan di PATH.
    echo.
    echo Pilihan:
    echo   1. Install MinGW-w64: https://github.com/niXman/mingw-builds-binaries/releases
    echo   2. Gunakan run_python.bat untuk menjalankan backend Python (tanpa compile)
    echo.
    echo Coba cari g++ di lokasi umum...
    if exist "C:\msys64\mingw64\bin\g++.exe" set GPP=C:\msys64\mingw64\bin\g++.exe
    if exist "C:\mingw64\bin\g++.exe"        set GPP=C:\mingw64\bin\g++.exe
    if exist "C:\TDM-GCC-64\bin\g++.exe"    set GPP=C:\TDM-GCC-64\bin\g++.exe
) else (
    set GPP=g++
)

if not defined GPP (
    echo [ERROR] g++ tidak ditemukan. Gunakan run_python.bat sebagai alternatif.
    pause
    exit /b 1
)

echo [BUILD] Menggunakan compiler: %GPP%
echo [BUILD] Mengkompilasi main.cpp + sqlite3.c...

%GPP% main.cpp sqlite3.c ^
    -o moodsync.exe ^
    -I"include" ^
    -L"." ^
    -lwebui-2-static ^
    -lws2_32 ^
    -lole32 ^
    -luuid ^
    -static ^
    -std=c++17 ^
    -O2

if %errorlevel% equ 0 (
    echo.
    echo [SUCCESS] Build berhasil! File: moodsync.exe
    echo [RUN] Menjalankan aplikasi...
    moodsync.exe
) else (
    echo.
    echo [ERROR] Build gagal. Periksa error di atas.
    echo Alternatif: gunakan run_python.bat
    pause
)
