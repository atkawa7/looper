# looper
Looper - A minimalistic console music player

NB: Tested on Windows and Ubuntu

- On windows
```
git clone https://github.com/atkawa7/looper
cd looper
vcpkg.exe install mpg123 libvorbis opusfile opus libogg libflac
mkdir build
cd build
cmake -G "Visual Studio 15 2017" -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x86-windows -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
looper.exe  test.mp3
```

- On Ubuntu
```
git clone https://github.com/atkawa7/looper
cd looper
sudo apt-get install libflac-dev libasound2-dev libvorbis-dev libogg-dev libmpg123-dev libopus-dev libopusfile-dev
mkdir build
cd build
cmake ..
cmake --build . --config Release
./looper  test.mp3
```
