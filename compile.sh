#!/bin/bash

#rm -rf build
#rm imgui.ini
#./vert.sh
rm log.txt
mkdir build
cd build
mkdir generated 

# Force wayland build
#cmake .. -DUSE_WAYLAND_WSI=ON

# Force release build
#cmake -DCMAKE_BUILD_TYPE=Release ..

# default
cmake ..

bear -- make -j
cp bin/core ../bin/
cp bin/core ~/.local/bin/photon
cd ..
./bin/core
