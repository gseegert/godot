/**************************************************************************/
/*  sdf_sphere_mesh_instance_3d.h                                         */
/**************************************************************************/
/*                   SDF Physics Module - Sphere Shape                    */
/**************************************************************************/

#ifndef SDF_SPHERE_MESH_INSTANCE_3D_H
#define SDF_SPHERE_MESH_INSTANCE_3D_H

#include "sdf_mesh_instance_3d.h"
#include "scene/resources/3d/sdf_sphere_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"

// GPU-raymarched sphere with optional collision
class SDFSphereMeshInstance3D : public SDFMeshInstance3D {
	GDCLASS(SDFSphereMeshInstance3D, SDFMeshInstance3D);

private:
	float radius = 1.0f; // Sphere radius

protected:
	static void _bind_methods();

	// SDFMeshInstance3D overrides
	virtual void _generate_proxy_mesh() override;
	virtual void _update_shader_parameters() override;
	virtual Ref<Shape3D> _create_physics_shape() override;
	virtual String _get_shader_defines() override;

public:
	void set_radius(float p_radius);
	float get_radius() const;

	virtual AABB get_aabb() const override;

	SDFSphereMeshInstance3D();
	virtual ~SDFSphereMeshInstance3D() {}
};

#endif // SDF_SPHERE_MESH_INSTANCE_3D_H
