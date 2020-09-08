g++ a.cpp -oa.exe
if %ERRORLEVEL% == 0 (
copy a.exe vc/a.exe
copy a.exe 3/a.exe
copy a.exe sa/a.exe
a.exe
) 
pause>nul