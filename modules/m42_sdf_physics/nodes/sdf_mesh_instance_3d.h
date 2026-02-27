/**************************************************************************/
/*  sdf_mesh_instance_3d.h                                                */
/**************************************************************************/
/*                   SDF Physics Module - Base Class                      */
/**************************************************************************/

#ifndef SDF_MESH_INSTANCE_3D_H
#define SDF_MESH_INSTANCE_3D_H

#include "scene/3d/visual_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/material.h"

// Base class for GPU-raymarched SDF shape rendering
// Provides automatic mesh generation, shader management, and optional collision
class SDFMeshInstance3D : public GeometryInstance3D {
	GDCLASS(SDFMeshInstance3D, GeometryInstance3D);

protected:
	// === Core Rendering ===
	RID mesh_rid;                          // RenderingServer mesh handle
	Ref<ShaderMaterial> sdf_material;      // Auto-generated raymarch shader
	bool mesh_dirty = true;                // Needs mesh regeneration
	bool material_dirty = true;            // Needs shader parameter update

	// === Optional Collision ===
	bool create_collision = false;         // User toggle for collision shape
	CollisionShape3D *collision_shape = nullptr; // Child collision node

	// === Visual Properties ===
	Color albedo = Color(1, 1, 1, 1);
	float metallic = 0.0f;
	float roughness = 0.5f;

	// === Pure Virtual Methods (implemented by subclasses) ===
	virtual void _generate_proxy_mesh() = 0;      // Create expanded mesh geometry
	virtual void _update_shader_parameters() = 0; // Update shader uniforms
	virtual Ref<Shape3D> _create_physics_shape() = 0; // Create collision shape
	virtual String _get_shader_defines() = 0;     // Get shader preprocessor defines

	// === Internal Update Methods ===
	void _update_mesh_if_dirty();
	void _update_material_if_dirty();
	void _update_collision_shape();
	void _invalidate_mesh();
	void _invalidate_material();

	// === Shader Generation ===
	String _generate_sdf_shader_code();           // Generate complete shader

	// === Godot Overrides ===
	static void _bind_methods();
	void _notification(int p_what);

public:
	// === Common Property Accessors ===
	void set_albedo(const Color &p_albedo);
	Color get_albedo() const;

	void set_metallic(float p_metallic);
	float get_metallic() const;

	void set_roughness(float p_roughness);
	float get_roughness() const;

	void set_create_collision(bool p_enable);
	bool get_create_collision() const;

	// === GeometryInstance3D Overrides ===
	virtual AABB get_aabb() const override = 0;

	SDFMeshInstance3D();
	virtual ~SDFMeshInstance3D();
};

#endif // SDF_MESH_INSTANCE_3D_H
