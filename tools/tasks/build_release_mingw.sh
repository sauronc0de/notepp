cmake --preset Release_mingw
cmake --build --preset Release_mingw

rm -rf dist/Notepp
mkdir -p dist/Notepp

cp build/Release_mingw/Notepp.exe dist/Notepp/
cp -r assets dist/Notepp/
cp -r data dist/Notepp/

./tools/tasks/copy_mingw_dlls.sh dist/Notepp/Notepp.exe