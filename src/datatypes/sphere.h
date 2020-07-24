//
//  sphere.h
//  C-ray
//
//  Created by Valtteri Koskivuori on 28/02/2015.
//  Copyright © 2015-2020 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include "vector.h"
#include "material.h"

//Sphere
struct sphere {
	float radius;
	struct material material;
};

//New sphere with given params
struct sphere newSphere(float radius, struct material material);

struct sphere newLightSphere(float radius, struct color color, float intensity);

struct sphere defaultSphere(void);

//Calculates intersection between a light ray and a sphere
bool rayIntersectsWithSphere(const struct lightRay *ray, const struct sphere *sphere, struct hitRecord *isect);
