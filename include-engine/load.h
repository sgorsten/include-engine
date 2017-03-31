#ifndef LOAD_H
#define LOAD_H

#include "data-types.h"

image load_image(const char * filename);
std::vector<mesh> load_meshes_from_fbx(const char * filename);

#endif
