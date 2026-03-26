/**************************************************************************/
/*  sdf_box_mesh_instance_3d.cpp                                          */
/**************************************************************************/
/*                   SDF Physics Module - Box Shape                       */
/**************************************************************************/

#include "sdf_mesh_instance_3d_box.h"
#include "servers/rendering_server.h"

SDFBoxMeshInstance3D::SDFBoxMeshInstance3D() {
}

// ============================================================================
// Property Binding
// ============================================================================

void SDFBoxMeshInstance3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &SDFBoxMeshInstance3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &SDFBoxMeshInstance3D::get_size);

	ClassDB::bind_method(D_METHOD("set_roundness", "roundness"), &SDFBoxMeshInstance3D::set_roundness);
	ClassDB::bind_method(D_METHOD("get_roundness"), &SDFBoxMeshInstance3D::get_roundness);

	ADD_GROUP("SDF Box", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roundness", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_roundness", "get_roundness");
}

// ============================================================================
// Proxy Mesh Generation
// ============================================================================

void SDFBoxMeshInstance3D::_generate_proxy_mesh() {
	// Calculate expansion for conservative rasterization (matching Godot BoxMesh pattern)
	// Generate mesh at actual size (like BoxMesh does), not unit size with scaling
	Vector3 half_extents = size * 0.5;
	float r = roundness * MIN(half_extents.x, MIN(half_extents.y, half_extents.z));
	Vector3 expanded = half_extents + Vector3(r, r, r) * 1.1; // 10% safety margin

	// Generate box vertices (8 corners)
	PackedVector3Array vertices;
	vertices.resize(8);

	vertices.write[0] = Vector3(-expanded.x, -expanded.y, -expanded.z);
	vertices.write[1] = Vector3(expanded.x, -expanded.y, -expanded.z);
	vertices.write[2] = Vector3(expanded.x, expanded.y, -expanded.z);
	vertices.write[3] = Vector3(-expanded.x, expanded.y, -expanded.z);
	vertices.write[4] = Vector3(-expanded.x, -expanded.y, expanded.z);
	vertices.write[5] = Vector3(expanded.x, -expanded.y, expanded.z);
	vertices.write[6] = Vector3(expanded.x, expanded.y, expanded.z);
	vertices.write[7] = Vector3(-expanded.x, expanded.y, expanded.z);

	// Generate indices (36 indices for 12 triangles, 6 faces)
	PackedInt32Array indices;
	indices.resize(36);

	// Front face (+Z)
	indices.write[0] = 4; indices.write[1] = 5; indices.write[2] = 6;
	indices.write[3] = 4; indices.write[4] = 6; indices.write[5] = 7;

	// Back face (-Z)
	indices.write[6] = 1; indices.write[7] = 0; indices.write[8] = 3;
	indices.write[9] = 1; indices.write[10] = 3; indices.write[11] = 2;

	// Right face (+X)
	indices.write[12] = 5; indices.write[13] = 1; indices.write[14] = 2;
	indices.write[15] = 5; indices.write[16] = 2; indices.write[17] = 6;

	// Left face (-X)
	indices.write[18] = 0; indices.write[19] = 4; indices.write[20] = 7;
	indices.write[21] = 0; indices.write[22] = 7; indices.write[23] = 3;

	// Top face (+Y)
	indices.write[24] = 3; indices.write[25] = 7; indices.write[26] = 6;
	indices.write[27] = 3; indices.write[28] = 6; indices.write[29] = 2;

	// Bottom face (-Y)
	indices.write[30] = 0; indices.write[31] = 1; indices.write[32] = 5;
	indices.write[33] = 0; indices.write[34] = 5; indices.write[35] = 4;

	// Create mesh arrays
	Array arrays;
	arrays.resize(RenderingServer::ARRAY_MAX);
	arrays[RenderingServer::ARRAY_VERTEX] = vertices;
	arrays[RenderingServer::ARRAY_INDEX] = indices;

	// Upload to RenderingServer
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->mesh_clear(mesh_rid);
	rs->mesh_add_surface_from_arrays(
		mesh_rid,
		RenderingServer::PRIMITIVE_TRIANGLES,
		arrays
	);

	// Apply material if it exists
	if (sdf_material.is_valid()) {
		rs->mesh_surface_set_material(mesh_rid, 0, sdf_material->get_rid());
	}
}

// ============================================================================
// Shader Integration
// ============================================================================

String SDFBoxMeshInstance3D::_get_shader_defines() {
	// Shape-specific shader code
	String defines = "#define SDF_SHAPE_BOX\n";
	defines += "\n// Shape parameters\n";
	defines += "uniform vec3 shape_half_extents = vec3(1.0);\n";
	defines += "uniform float shape_roundness = 0.0;\n";
	defines += "\n";

	// Box-specific SDF and intersection functions
	defines += R"(
// SDF scene function for box
float sdf_scene(vec3 p) {
	return sdf_box(p, shape_half_extents, shape_roundness);
}

// Normal computation for box
vec3 compute_sdf_normal(vec3 p) {
	return sdf_box_normal(p, shape_half_extents, shape_roundness);
}

// Bounding volume intersection for box
bool intersect_shape_bounds(vec3 ro, vec3 rd, out float t_near, out float t_far) {
	// Use slightly expanded bounds for conservative marching
	return intersect_box(ro, rd, shape_half_extents * 1.1, t_near, t_far);
}

// Sphere tracing through SDF scene
// Now defined AFTER sdf_scene() to avoid forward declaration issues
bool raymarch_sdf(vec3 ray_origin, vec3 ray_dir, out vec3 hit_pos, out float total_t) {
	const int MAX_STEPS = 64;
	const float MIN_DIST = 0.001;
	const float MAX_DIST = 100.0;

	total_t = 0.0;

	for (int i = 0; i < MAX_STEPS; i++) {
		hit_pos = ray_origin + ray_dir * total_t;

		// Query SDF at current position
		float dist = sdf_scene(hit_pos);

		// Hit detection
		if (dist < MIN_DIST) {
			return true;
		}

		// Step forward by SDF distance (sphere tracing)
		total_t += dist;

		// Exceeded maximum distance
		if (total_t > MAX_DIST) {
			break;
		}
	}

	return false; // Miss
}
)";

	return defines;
}

void SDFBoxMeshInstance3D::_update_shader_parameters() {
	if (sdf_material.is_null()) {
		return;
	}

	// Calculate absolute rounding radius (matching mesh generation)
	Vector3 half_extents = size * 0.5;
	float r = roundness * MIN(half_extents.x, MIN(half_extents.y, half_extents.z));

	// Update shape parameters (in local mesh space, matching generated mesh size)
	sdf_material->set_shader_parameter("shape_half_extents", half_extents);
	sdf_material->set_shader_parameter("shape_roundness", r);

	// Update material properties
	sdf_material->set_shader_parameter("albedo", albedo);
	sdf_material->set_shader_parameter("metallic", metallic);
	sdf_material->set_shader_parameter("roughness", roughness);
}

// ============================================================================
// Physics Shape Creation
// ============================================================================

Ref<Shape3D> SDFBoxMeshInstance3D::_create_physics_shape() {
	// Create SDF box shape if available
	Ref<SDFBoxShape3D> shape;
	shape.instantiate();
	shape->set_size(size);
	shape->set_roundness(roundness);
	return shape;
}

// ============================================================================
// Property Accessors
// ============================================================================

void SDFBoxMeshInstance3D::set_size(const Vector3 &p_size) {
	// Clamp to positive values
	Vector3 clamped_size = Vector3(
		MAX(p_size.x, 0.001f),
		MAX(p_size.y, 0.001f),
		MAX(p_size.z, 0.001f)
	);

	if (size == clamped_size) {
		return;
	}

	size = clamped_size;

	// Regenerate mesh at new size (matching Godot BoxMesh convention)
	_invalidate_mesh();
	_invalidate_material();

	// Update collision shape if enabled
	if (get_create_collision()) {
		call_deferred("_update_collision_shape");
	}

	update_gizmos();
}

Vector3 SDFBoxMeshInstance3D::get_size() const {
	return size;
}

void SDFBoxMeshInstance3D::set_roundness(float p_roundness) {
	p_roundness = CLAMP(p_roundness, 0.0f, 1.0f);

	if (Math::is_equal_approx(roundness, p_roundness)) {
		return;
	}

	roundness = p_roundness;

	// Roundness affects mesh expansion, so regenerate mesh
	_invalidate_mesh();
	_invalidate_material();

	// Update collision shape if enabled
	if (get_create_collision()) {
		call_deferred("_update_collision_shape");
	}
}

float SDFBoxMeshInstance3D::get_roundness() const {
	return roundness;
}

// ============================================================================
// AABB
// ============================================================================

AABB SDFBoxMeshInstance3D::get_aabb() const {
	// Return bounding box for the expanded mesh
	Vector3 half_extents = size * 0.5;
	float r = roundness * MIN(half_extents.x, MIN(half_extents.y, half_extents.z));
	Vector3 expanded = half_extents + Vector3(r, r, r) * 1.1;

	return AABB(-expanded, expanded * 2.0);
}
