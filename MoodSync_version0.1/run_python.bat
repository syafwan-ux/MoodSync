@echo off
echo =====================================================
echo   MoodSync v0.1 - Run Python Backend
echo   Database : SQLite (moodsync.db)
echo   IPC      : WebUI Data Binding
echo =====================================================

REM Cek Python
where python >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Python tidak ditemukan. Install dari https://python.org
    pause
    exit /b 1
)

echo [CHECK] Python versi:
python --version

REM Cek / install webui2
echo [CHECK] Memastikan webui2 terinstall...
python -c "import webui" 2>nul
if %errorlevel% neq 0 (
    echo [INSTALL] Menginstall webui2...
    pip install webui2
)

echo.
echo [START] Menjalankan MoodSync...
echo         Buka browser/window akan muncul otomatis.
echo         Tekan Ctrl+C untuk keluar.
echo.

cd /d "%~dp0"
python main.py

pause
