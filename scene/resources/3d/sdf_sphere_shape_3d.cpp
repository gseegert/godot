/**************************************************************************/
/*  box_shape_3d.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "sdf_sphere_shape_3d.h"

#if defined(MODULE_M42_SDF_PHYSICS_ENABLED)

#include "scene/resources/3d/primitive_meshes.h"


void SDFSphereShape3D::_update_shape() {
	SphereShape3D::_update_shape();
}

void SDFSphereShape3D::_bind_methods() {
	SphereShape3D::_bind_methods();
}

SDFSphereShape3D::SDFSphereShape3D(RID p_shape) :
		SphereShape3D(p_shape) {

	// @TODO(MODULE_M42_SDF_PHYSICS_ENABLED) : Initialize SDF shape here

}

SDFSphereShape3D::SDFSphereShape3D(PhysicsServer3D::ShapeType p_shape_type) :
		SDFSphereShape3D(PhysicsServer3D::get_singleton()->shape_create(p_shape_type)) {
}

SDFSphereShape3D::SDFSphereShape3D() :
		SDFSphereShape3D(PhysicsServer3D::SHAPE_SDF_SPHERE) {
}

#endif // MODULE_M42_SDF_PHYSICS_ENABLED
