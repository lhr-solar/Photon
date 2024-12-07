#!/bin/bash
rm -rf ./build
mkdir ./build && cd ./build
cmake -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=ON ..
cmake --build .
