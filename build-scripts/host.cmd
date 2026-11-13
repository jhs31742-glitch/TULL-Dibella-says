@echo off
call "G:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cl /nologo /std:c++latest /EHsc /utf-8 /Fe:OwnedCostTest.exe ..\src\OwnedCostTest.cpp || exit /b 1
OwnedCostTest.exe || exit /b 1
cl /nologo /std:c++latest /EHsc /utf-8 /Fe:HudSessionTest.exe ..\src\HudSessionTest.cpp || exit /b 1
HudSessionTest.exe || exit /b 1
cl /nologo /std:c++latest /EHsc /utf-8 /Fe:OfferingTest.exe ..\src\OfferingTest.cpp || exit /b 1
OfferingTest.exe || exit /b 1
cl /nologo /std:c++latest /EHsc /utf-8 /Fe:CR2Test.exe ..\src\CR2Test.cpp || exit /b 1
CR2Test.exe || exit /b 1
cl /nologo /std:c++latest /EHsc /utf-8 /Fe:HostTests.exe ..\src\tests.cpp ..\src\Phase1.cpp ..\src\SexLabStats.cpp || exit /b 1
HostTests.exe || exit /b 1
cl /nologo /std:c++latest /EHsc /utf-8 /Fe:HitPolicyTest.exe ..\src\HitPolicyTest.cpp ..\src\Phase1.cpp || exit /b 1
HitPolicyTest.exe || exit /b 1
cl /nologo /std:c++latest /EHsc /utf-8 /Fe:EquipmentClaimsTest.exe ..\src\EquipmentClaimsTest.cpp || exit /b 1
EquipmentClaimsTest.exe || exit /b 1
cl /nologo /std:c++latest /EHsc /utf-8 /Fe:EquipmentLookupTest.exe ..\src\EquipmentLookupTest.cpp || exit /b 1
EquipmentLookupTest.exe
