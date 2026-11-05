@echo off
chcp 65001 >nul
echo === StyleMix: сборка VST3 ===
cmake -B build -A x64
if errorlevel 1 goto fail
cmake --build build --config Release --target StyleMix_VST3
if errorlevel 1 goto fail
echo.
echo Готово! Плагин лежит здесь:
echo %~dp0build\StyleMix_artefacts\Release\VST3\StyleMix.vst3
echo Скопируй папку StyleMix.vst3 в C:\Program Files\Common Files\VST3
pause
exit /b 0
:fail
echo Ошибка сборки. Проверь, что установлены Visual Studio Build Tools, CMake и Git.
pause
exit /b 1
