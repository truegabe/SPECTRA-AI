@echo off
setlocal

:: SPECTRA build script for Windows
:: Requires GCC (MinGW recommended) on PATH

where gcc >nul 2>&1
if errorlevel 1 (
    echo [ERROR] gcc not found on PATH.
    echo Install MinGW or another GCC distribution and add it to PATH.
    exit /b 1
)

echo [SPECTRA] Building spectra.exe...

gcc ^
  src/ast.c ^
  src/lexer.c ^
  src/parser.c ^
  src/value.c ^
  src/env.c ^
  src/interpreter.c ^
  src/builtins.c ^
  src/modules.c ^
  src/repl.c ^
  src/simulator.c ^
  src/main.c ^
  runtime/specton.c ^
  runtime/tensor.c ^
  runtime/memory.c ^
  runtime/neural.c ^
  -Isrc -Iruntime ^
  -o spectra.exe ^
  -lm -O2 -std=c99 2>&1

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo [OK] Built spectra.exe
for %%F in (spectra.exe) do echo [OK] Size: %%~zF bytes

if "%1"=="test" goto :test
goto :end

:test
echo.
echo [SPECTRA] Running runtime tests...
if not exist build mkdir build
gcc -std=c99 -O2 -Wall -Iruntime ^
  tests/test_runtime.c ^
  runtime/specton.c ^
  runtime/tensor.c ^
  runtime/memory.c ^
  -lm -o build/test_runtime.exe 2>&1
if errorlevel 1 (
    echo [ERROR] Test build failed.
    exit /b 1
)
build\test_runtime.exe

:end
endlocal
