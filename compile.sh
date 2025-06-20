#!/bin/bash

#rm -rf build
#rm imgui.ini
#./vert.sh
mkdir build
cd build
mkdir generated 
cmake ..
#cmake -DCMAKE_BUILD_TYPE=Release ..
bear -- make -j
cp bin/core ../bin/
cd ..
./bin/core
