#!/bin/bash

#glslangValidator -V custom_model.vert -o custom_model.vert.spv
#glslangValidator -V custom_model.frag -o custom_model.frag.spv

#glslangValidator -V particle_system.vert -o particle_system.vert.spv
#glslangValidator -V particle_system.frag -o particle_system.frag.spv

glslangValidator -V particles.vert -o particles.vert.spv
glslangValidator -V particles.frag -o particles.frag.spv
