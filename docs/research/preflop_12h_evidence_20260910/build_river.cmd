@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
cd /d "C:\Users\GoryNickel\Documents\GitHub\GTO-Solver\docs\research\preflop_12h_evidence_20260910"
cl /nologo /std:c++20 /EHsc /O2 /Ob2 /DNDEBUG /MD /W4 /permissive- /I"C:\Users\GoryNickel\Documents\GitHub\GTO-Solver\include" /I"C:\Users\GoryNickel\Documents\GitHub\GTO-Solver\out\build\codex-release-20260907\generated" river_certificate_probe.cpp /Fe:river_certificate_probe.exe /link "C:\Users\GoryNickel\Documents\GitHub\GTO-Solver\out\build\codex-release-20260907\libs\preflop\gtosd_preflop.lib" "C:\Users\GoryNickel\Documents\GitHub\GTO-Solver\out\build\codex-release-20260907\libs\tree\gtosd_tree.lib" "C:\Users\GoryNickel\Documents\GitHub\GTO-Solver\out\build\codex-release-20260907\libs\equity\gtosd_equity.lib" "C:\Users\GoryNickel\Documents\GitHub\GTO-Solver\out\build\codex-release-20260907\libs\core\gtosd_core.lib"
if errorlevel 1 exit /b 1
river_certificate_probe.exe > river_certificate_probe.stdout.log 2> river_certificate_probe.stderr.log
