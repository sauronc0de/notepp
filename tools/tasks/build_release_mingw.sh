cmake --preset Release_mingw
cmake --build --preset Release_mingw

rm -rf dist/Notepp
mkdir -p dist/Notepp

cp build/Release_mingw/Notepp.exe dist/Notepp/
cp -r assets dist/Notepp/
cp -r data dist/Notepp/

# Copy required DLLs to the dist/Notepp folder
./tools/tasks/copy_mingw_dlls.sh

# Create installer using Inno Setup Compiler (ISCC)
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "build/Release_mingw/notepp.iss"