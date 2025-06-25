#!/bin/bash

#rm -rf build
#rm imgui.ini
#./vert.sh
rm log.txt
mkdir build
cd build
mkdir generated 
cmake ..
#cmake -DCMAKE_BUILD_TYPE=Release ..
bear -- make -j
cp bin/core ../bin/
cp bin/core ~/.local/bin/photon
cd ..
./bin/core
