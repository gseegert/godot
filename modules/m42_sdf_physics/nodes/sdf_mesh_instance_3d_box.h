/**************************************************************************/
/*  sdf_box_mesh_instance_3d.h                                            */
/**************************************************************************/
/*                   SDF Physics Module - Box Shape                       */
/**************************************************************************/

#ifndef SDF_BOX_MESH_INSTANCE_3D_H
#define SDF_BOX_MESH_INSTANCE_3D_H

#include "sdf_mesh_instance_3d.h"
#include "scene/resources/3d/sdf_box_shape_3d.h"
#include "scene/resources/3d/box_shape_3d.h"

// GPU-raymarched rounded box with optional collision
class SDFBoxMeshInstance3D : public SDFMeshInstance3D {
	GDCLASS(SDFBoxMeshInstance3D, SDFMeshInstance3D);

private:
	Vector3 size = Vector3(2, 2, 2); // Box dimensions
	float roundness = 0.0f;           // Rounding factor (0.0-1.0)

protected:
	static void _bind_methods();

	// SDFMeshInstance3D overrides
	virtual void _generate_proxy_mesh() override;
	virtual void _update_shader_parameters() override;
	virtual Ref<Shape3D> _create_physics_shape() override;
	virtual String _get_shader_defines() override;

public:
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;

	void set_roundness(float p_roundness);
	float get_roundness() const;

	virtual AABB get_aabb() const override;

	SDFBoxMeshInstance3D();
	virtual ~SDFBoxMeshInstance3D() {}
};

#endif // SDF_BOX_MESH_INSTANCE_3D_H
