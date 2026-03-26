/**************************************************************************/
/*  sdf_sphere_mesh_instance_3d.cpp                                       */
/**************************************************************************/
/*                   SDF Physics Module - Sphere Shape                    */
/**************************************************************************/

#include "sdf_mesh_instance_3d_sphere.h"
#include "core/math/math_defs.h"
#include "servers/rendering_server.h"

SDFSphereMeshInstance3D::SDFSphereMeshInstance3D() {
}

// ============================================================================
// Property Binding
// ============================================================================

void SDFSphereMeshInstance3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &SDFSphereMeshInstance3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &SDFSphereMeshInstance3D::get_radius);

	ADD_GROUP("SDF Sphere", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.001,100,0.001,or_greater,suffix:m"), "set_radius", "get_radius");
}

// ============================================================================
// Proxy Mesh Generation
// ============================================================================

void SDFSphereMeshInstance3D::_generate_proxy_mesh() {
	// Generate sphere at actual radius (matching Godot SphereMesh convention)
	// Apply 10% expansion for conservative rasterization
	float expanded_radius = radius * 1.1f;

	// Generate UV sphere (simple tessellation)
	const int rings = 16;
	const int radial_segments = 32;

	PackedVector3Array vertices;
	PackedInt32Array indices;

	// Generate vertices
	for (int i = 0; i <= rings; i++) {
		float v = (float)i / rings;
		float phi = v * Math::PI;

		for (int j = 0; j <= radial_segments; j++) {
			float u = (float)j / radial_segments;
			float theta = u * Math::TAU;

			float x = Math::sin(phi) * Math::cos(theta);
			float y = Math::cos(phi);
			float z = Math::sin(phi) * Math::sin(theta);

			vertices.push_back(Vector3(x, y, z) * expanded_radius);
		}
	}

	// Generate indices
	for (int i = 0; i < rings; i++) {
		for (int j = 0; j < radial_segments; j++) {
			int current = i * (radial_segments + 1) + j;
			int next = current + radial_segments + 1;

			// First triangle
			indices.push_back(current);
			indices.push_back(next);
			indices.push_back(current + 1);

			// Second triangle
			indices.push_back(current + 1);
			indices.push_back(next);
			indices.push_back(next + 1);
		}
	}

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

String SDFSphereMeshInstance3D::_get_shader_defines() {
	// Shape-specific shader code
	String defines = "#define SDF_SHAPE_SPHERE\n";
	defines += "\n// Shape parameters\n";
	defines += "uniform float shape_radius = 1.0;\n";
	defines += "\n";

	// Sphere-specific SDF and intersection functions
	defines += R"(
// SDF scene function for sphere
float sdf_scene(vec3 p) {
	return sdf_sphere(p, shape_radius);
}

// Normal computation for sphere
vec3 compute_sdf_normal(vec3 p) {
	return sdf_sphere_normal(p);
}

// Bounding volume intersection for sphere
bool intersect_shape_bounds(vec3 ro, vec3 rd, out float t_near, out float t_far) {
	// Use slightly expanded bounds for conservative marching
	return intersect_sphere(ro, rd, shape_radius * 1.1, t_near, t_far);
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

void SDFSphereMeshInstance3D::_update_shader_parameters() {
	if (sdf_material.is_null()) {
		return;
	}

	// Update shape parameters (matching mesh generation)
	sdf_material->set_shader_parameter("shape_radius", radius);

	// Update material properties
	sdf_material->set_shader_parameter("albedo", albedo);
	sdf_material->set_shader_parameter("metallic", metallic);
	sdf_material->set_shader_parameter("roughness", roughness);
}

// ============================================================================
// Physics Shape Creation
// ============================================================================

Ref<Shape3D> SDFSphereMeshInstance3D::_create_physics_shape() {
	// Create SDF sphere shape if available
	Ref<SDFSphereShape3D> shape;
	shape.instantiate();
	shape->set_radius(radius);
	return shape;
}

// ============================================================================
// Property Accessors
// ============================================================================

void SDFSphereMeshInstance3D::set_radius(float p_radius) {
	p_radius = MAX(p_radius, 0.001f); // Clamp to positive

	if (Math::is_equal_approx(radius, p_radius)) {
		return;
	}

	radius = p_radius;

	// Regenerate mesh at new radius (matching Godot SphereMesh convention)
	_invalidate_mesh();
	_invalidate_material();

	// Update collision shape if enabled
	if (get_create_collision()) {
		call_deferred("_update_collision_shape");
	}

	update_gizmos();
}

float SDFSphereMeshInstance3D::get_radius() const {
	return radius;
}

// ============================================================================
// AABB
// ============================================================================

AABB SDFSphereMeshInstance3D::get_aabb() const {
	// Return bounding box for the expanded sphere
	float expanded_radius = radius * 1.1;
	Vector3 extents = Vector3(expanded_radius, expanded_radius, expanded_radius);

	return AABB(-extents, extents * 2.0);
}
