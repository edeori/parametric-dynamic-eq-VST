$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
cmake -S . -B build/ci -A x64 -DPARADYNEQ_ENABLE_LICENSING=ON -DPARADYNEQ_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build build/ci --config Release --target ParaDynEQ_VST3 ParaDynEQ_Standalone ParaDynEQTests ParaDynEQMatchTests ParaDynEQMatchIntegrationTests ParaDynEQFingerprintTests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
ctest --test-dir build/ci -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Regression tests failed' }
python ci/verify-licensed.py build/ci "build/ci/Release/VST3/ParaDyn EQ.vst3/Contents/x86_64-win/ParaDyn EQ.vst3" "build/ci/Release/Standalone/ParaDyn EQ.exe"
if ($LASTEXITCODE -ne 0) { throw 'Licensed binary verification failed' }
$versionLine = Get-Content build/ci/CMakeCache.txt | Where-Object { $_ -match '^CMAKE_PROJECT_VERSION:STATIC=' }
$version = ($versionLine -split '=', 2)[1]
$compiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
if (!(Test-Path $compiler)) { throw 'Inno Setup 6 is required' }
New-Item -ItemType Directory -Force dist | Out-Null
& $compiler "/DAppVersion=$version" "/DRepoRoot=$PWD" "/O$PWD/dist" Installer/ParaDynEQ.iss
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed' }
