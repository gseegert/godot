// SDF Raymarch Library for M42 SDF Physics Module
// This file provides SDF primitive functions, ray intersection, and analytical normals
// for GPU-based raymarched rendering of SDF shapes.
//
// Note: The raymarch_sdf() function is defined by shape-specific code after sdf_scene()
// to avoid forward declaration issues.

// ============================================================================
// SDF PRIMITIVE FUNCTIONS (Inigo Quilez)
// ============================================================================

// Rounded box SDF
// p: point to evaluate (in local space, origin at center)
// b: half-extents of the box (before rounding)
// r: rounding radius
float sdf_box(vec3 p, vec3 b, float r) {
	vec3 q = abs(p) - (b - vec3(r));
	return length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

// Sphere SDF
// p: point to evaluate (in local space, origin at center)
// r: sphere radius
float sdf_sphere(vec3 p, float r) {
	return length(p) - r;
}

// ============================================================================
// ANALYTICAL NORMAL COMPUTATION
// ============================================================================
// These match the physics implementation exactly (GodotSDFBoxShape3D, GodotSDFSphereShape3D)
// CRITICAL: These are analytical derivatives, NOT numerical approximations!

// Box SDF gradient - EXACT derivative
// Matches GodotSDFBoxShape3D::_sdf_distance_gradient() from godot_shape_3d.h:538
vec3 sdf_box_normal(vec3 p, vec3 b, float r) {
	vec3 shrunk_extents = b - vec3(r);
	vec3 w = abs(p) - shrunk_extents;
	vec3 s = sign(p);
	float g = max(w.x, max(w.y, w.z));
	vec3 q = max(w, vec3(0.0));
	float l = length(q);

	// Analytical gradient (matches physics code exactly)
	// Safe division: only divide if both g > 0 and l > 0
	// Multiply by sign to restore direction (q uses abs(p) which strips sign)
	return (g > 0.0 && l > 0.0) ? (s * q / l) : s;
}

// Sphere SDF gradient - EXACT derivative (normalized position vector)
// Matches GodotSDFSphereShape3D::_sdf_distance_gradient() from godot_shape_3d.h:587
vec3 sdf_sphere_normal(vec3 p) {
	float l = length(p);
	// Arbitrary fallback if at exact center
	return (l > 0.0001) ? (p / l) : vec3(0.0, 1.0, 0.0);
}

// ============================================================================
// RAY-SHAPE INTERSECTION (for constrained raymarching)
// ============================================================================

// AABB ray intersection
// ro: ray origin (in local space)
// rd: ray direction (in local space, normalized)
// extents: box half-extents
// Returns: true if intersects, outputs t_near and t_far
bool intersect_box(vec3 ro, vec3 rd, vec3 extents, out float t_near, out float t_far) {
	vec3 inv_dir = 1.0 / rd;
	vec3 tmin = (-extents - ro) * inv_dir;
	vec3 tmax = (extents - ro) * inv_dir;
	vec3 t1 = min(tmin, tmax);
	vec3 t2 = max(tmin, tmax);
	t_near = max(max(t1.x, t1.y), t1.z);
	t_far = min(min(t2.x, t2.y), t2.z);
	return t_far > max(t_near, 0.0);
}

// Sphere ray intersection
// ro: ray origin (in local space)
// rd: ray direction (in local space, normalized)
// radius: sphere radius
// Returns: true if intersects, outputs t_near and t_far
bool intersect_sphere(vec3 ro, vec3 rd, float radius, out float t_near, out float t_far) {
	float a = dot(rd, rd);
	float b = 2.0 * dot(ro, rd);
	float c = dot(ro, ro) - radius * radius;
	float discriminant = b * b - 4.0 * a * c;

	if (discriminant < 0.0) {
		return false; // No intersection
	}

	float sqrt_disc = sqrt(discriminant);
	t_near = (-b - sqrt_disc) / (2.0 * a);
	t_far = (-b + sqrt_disc) / (2.0 * a);
	return true;
}

// ============================================================================
// END OF LIBRARY
// ============================================================================
// Shape-specific code should define:
// - float sdf_scene(vec3 p) - The SDF function for the shape
// - vec3 compute_sdf_normal(vec3 p) - The analytical normal function
// - bool intersect_shape_bounds(...) - Ray-shape intersection for bounds
// - bool raymarch_sdf(...) - Sphere tracing function (calls sdf_scene)
// ============================================================================
