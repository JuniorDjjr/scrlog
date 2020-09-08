g++ ordered_commands.cpp -o_.exe
if %ERRORLEVEL% == 0 (
move _.exe vc/_.exe
cd vc
_.exe
) 
pause>nul