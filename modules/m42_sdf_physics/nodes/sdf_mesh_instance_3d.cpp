/**************************************************************************/
/*  sdf_mesh_instance_3d.cpp                                              */
/**************************************************************************/
/*                   SDF Physics Module - Base Class                      */
/**************************************************************************/

#include "sdf_mesh_instance_3d.h"
#include "core/io/resource_loader.h"
#include "servers/rendering_server.h"

// Generated shader header
#include "modules/m42_sdf_physics/shaders/sdf_raymarch_inc.glsl.gen.h"

// ============================================================================
// Constructor / Destructor
// ============================================================================

SDFMeshInstance3D::SDFMeshInstance3D() {
	// Create mesh RID for RenderingServer
	mesh_rid = RenderingServer::get_singleton()->mesh_create();
	set_base(mesh_rid); // Register with GeometryInstance3D
}

SDFMeshInstance3D::~SDFMeshInstance3D() {
	// Clean up mesh RID
	if (mesh_rid.is_valid()) {
		RenderingServer::get_singleton()->free(mesh_rid);
	}

	// Collision shape child is automatically freed by Godot
}

// ============================================================================
// Property Binding
// ============================================================================

void SDFMeshInstance3D::_bind_methods() {
	// Visual properties
	ClassDB::bind_method(D_METHOD("set_albedo", "albedo"), &SDFMeshInstance3D::set_albedo);
	ClassDB::bind_method(D_METHOD("get_albedo"), &SDFMeshInstance3D::get_albedo);

	ClassDB::bind_method(D_METHOD("set_metallic", "metallic"), &SDFMeshInstance3D::set_metallic);
	ClassDB::bind_method(D_METHOD("get_metallic"), &SDFMeshInstance3D::get_metallic);

	ClassDB::bind_method(D_METHOD("set_roughness", "roughness"), &SDFMeshInstance3D::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &SDFMeshInstance3D::get_roughness);

	// Collision
	ClassDB::bind_method(D_METHOD("set_create_collision", "enable"), &SDFMeshInstance3D::set_create_collision);
	ClassDB::bind_method(D_METHOD("get_create_collision"), &SDFMeshInstance3D::get_create_collision);

	// Internal methods (needed for call_deferred)
	ClassDB::bind_method(D_METHOD("_update_mesh_if_dirty"), &SDFMeshInstance3D::_update_mesh_if_dirty);
	ClassDB::bind_method(D_METHOD("_update_material_if_dirty"), &SDFMeshInstance3D::_update_material_if_dirty);
	ClassDB::bind_method(D_METHOD("_update_collision_shape"), &SDFMeshInstance3D::_update_collision_shape);

	// Properties exposed to editor
	ADD_GROUP("SDF Appearance", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "albedo"), "set_albedo", "get_albedo");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "metallic", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_metallic", "get_metallic");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_roughness", "get_roughness");

	ADD_GROUP("SDF Physics", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "create_collision"), "set_create_collision", "get_create_collision");
}

// ============================================================================
// Notifications (Lifecycle)
// ============================================================================

void SDFMeshInstance3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			// Generate mesh and material when entering scene tree
			_update_mesh_if_dirty();
			_update_material_if_dirty();
		} break;

		case NOTIFICATION_TRANSFORM_CHANGED: {
			// Sync transform with collision shape if it exists
			if (collision_shape) {
				collision_shape->set_global_transform(get_global_transform());
			}
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			// Sync visibility with collision shape
			if (collision_shape) {
				collision_shape->set_visible(is_visible());
			}
		} break;
	}
}

// ============================================================================
// Mesh Management
// ============================================================================

void SDFMeshInstance3D::_update_mesh_if_dirty() {
	if (!mesh_dirty) {
		return;
	}

	// Subclass generates proxy mesh geometry
	_generate_proxy_mesh();

	mesh_dirty = false;
}

void SDFMeshInstance3D::_invalidate_mesh() {
	mesh_dirty = true;

	// Regenerate mesh next frame if in tree
	if (is_inside_tree()) {
		call_deferred("_update_mesh_if_dirty");
	}
}

// ============================================================================
// Material / Shader Management
// ============================================================================

void SDFMeshInstance3D::_update_material_if_dirty() {
	if (!material_dirty) {
		return;
	}

	// Create material if needed
	if (sdf_material.is_null()) {
		sdf_material.instantiate();

		// Create shader
		Ref<Shader> shader;
		shader.instantiate();

		// Generate shader code
		String shader_code = _generate_sdf_shader_code();
		shader->set_code(shader_code);

		sdf_material->set_shader(shader);

		// Apply material to mesh
		RenderingServer *rs = RenderingServer::get_singleton();
		rs->mesh_surface_set_material(mesh_rid, 0, sdf_material->get_rid());
	}

	// Update shader parameters (uniforms)
	_update_shader_parameters();

	material_dirty = false;
}

void SDFMeshInstance3D::_invalidate_material() {
	material_dirty = true;

	// Update material next frame if in tree
	if (is_inside_tree()) {
		call_deferred("_update_material_if_dirty");
	}
}

// ============================================================================
// Shader Code Generation
// ============================================================================

String SDFMeshInstance3D::_generate_sdf_shader_code() {
	// Build complete shader using generated header
	String full_shader = "shader_type spatial;\n";
	// depth_prepass_alpha: Treat as opaque material with alpha test (for proper shadows)
	full_shader += "render_mode cull_front, depth_draw_always, depth_prepass_alpha, specular_schlick_ggx;\n\n";

	// Get shape-specific defines (uniforms only, not functions yet)
	String shape_defines = _get_shader_defines();

	// Extract just the uniform declarations (before the functions)
	int func_start = shape_defines.find("float sdf_scene");
	if (func_start < 0) {
		func_start = shape_defines.find("bool intersect_shape_bounds");
	}

	String uniforms_only = (func_start > 0) ? shape_defines.substr(0, func_start) : "";
	String shape_functions = (func_start > 0) ? shape_defines.substr(func_start) : shape_defines;

	// Add uniforms first
	full_shader += uniforms_only;
	full_shader += "// Material properties\n";
	full_shader += "uniform vec4 albedo : source_color = vec4(1.0);\n";
	full_shader += "uniform float metallic : hint_range(0.0, 1.0) = 0.0;\n";
	full_shader += "uniform float roughness : hint_range(0.0, 1.0) = 0.5;\n\n";
	full_shader += "// Depth texture for manual depth testing (required with cull_front)\n";
	full_shader += "uniform sampler2D DEPTH_TEXTURE : hint_depth_texture, filter_linear_mipmap;\n\n";

	// Append SDF library from generated header (BEFORE shape functions!)
	full_shader += String::utf8((const char *)sdf_raymarch_inc_shader_glsl);
	full_shader += "\n\n";

	// NOW add shape-specific wrapper functions (that call the library functions)
	full_shader += shape_functions;
	full_shader += "\n\n";

	// Fragment shader template
	full_shader += R"(
void fragment() {
	// Both main and shadow passes need raymarching for rounded shadows!
	// Transform to object space using view-space origin (works for camera AND light)
	mat4 world_to_local = inverse(MODEL_MATRIX);

	// Ray direction: from camera/light (origin in view space) to fragment
	vec3 ray_dir_view = normalize(VERTEX); // VERTEX is fragment pos in view space
	vec3 ray_dir_world = (INV_VIEW_MATRIX * vec4(ray_dir_view, 0.0)).xyz;
	vec3 ray_dir_local = (world_to_local * vec4(ray_dir_world, 0.0)).xyz;

	// Ray origin: camera/light is at origin in view space
	vec3 cam_pos_world = (INV_VIEW_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 cam_pos_local = (world_to_local * vec4(cam_pos_world, 1.0)).xyz;

	// Constrained raymarch using shape bounds
	float t_near, t_far;
	if (!intersect_shape_bounds(cam_pos_local, ray_dir_local, t_near, t_far)) {
		discard; // No intersection with bounding volume
	}

	// Start march from near intersection
	vec3 march_start = cam_pos_local + ray_dir_local * max(t_near, 0.0);

	// Sphere trace through SDF
	vec3 hit_pos;
	float total_t;
	if (!raymarch_sdf(march_start, ray_dir_local, hit_pos, total_t)) {
		discard; // Miss
	}

	// World-space hit position (transform applies scaling)
	vec3 world_hit = (MODEL_MATRIX * vec4(hit_pos, 1.0)).xyz;

	// Compute depth (REQUIRED for both main and shadow passes)
	vec4 clip_pos = PROJECTION_MATRIX * VIEW_MATRIX * vec4(world_hit, 1.0);
	float sdf_depth = clip_pos.z / clip_pos.w; // Godot already maps to [0,1] with reversed-Z

	// Manual depth test ONLY in main pass (shadow pass has its own depth testing)
	// In main pass: discard if SDF surface is behind scene geometry
	if (!IN_SHADOW_PASS) {
		float scene_depth = texture(DEPTH_TEXTURE, SCREEN_UV).r;
		// Note: Godot uses reversed-Z (near=1.0, far=0.0), so larger values are closer
		if (sdf_depth < scene_depth) {
			discard;
		}
	}

	// Output SDF depth
	DEPTH = sdf_depth;

	// Main pass: Output normals and material properties
	if (!IN_SHADOW_PASS) {
		// TRUE analytical normal from SDF gradient (not finite differences!)
		vec3 local_normal = compute_sdf_normal(hit_pos);
		NORMAL = normalize((MODEL_MATRIX * vec4(local_normal, 0.0)).xyz);

		// Output PBR properties
		ALBEDO = albedo.rgb;
		METALLIC = metallic;
		ROUGHNESS = roughness;
	}
}
)";

	return full_shader;
}

// ============================================================================
// Collision Shape Management
// ============================================================================

void SDFMeshInstance3D::_update_collision_shape() {
	if (create_collision) {
		if (collision_shape == nullptr) {
			// Create child collision shape node
			collision_shape = memnew(CollisionShape3D);
			add_child(collision_shape, false, INTERNAL_MODE_BACK);
			collision_shape->set_owner(this);
		}

		// Create/update physics shape from subclass
		Ref<Shape3D> new_shape = _create_physics_shape();
		collision_shape->set_shape(new_shape);
	} else {
		// Remove collision shape if disabled
		if (collision_shape != nullptr) {
			collision_shape->queue_free();
			collision_shape = nullptr;
		}
	}
}

// ============================================================================
// Property Accessors
// ============================================================================

void SDFMeshInstance3D::set_albedo(const Color &p_albedo) {
	if (albedo == p_albedo) {
		return;
	}

	albedo = p_albedo;
	_invalidate_material();
}

Color SDFMeshInstance3D::get_albedo() const {
	return albedo;
}

void SDFMeshInstance3D::set_metallic(float p_metallic) {
	p_metallic = CLAMP(p_metallic, 0.0f, 1.0f);
	if (Math::is_equal_approx(metallic, p_metallic)) {
		return;
	}

	metallic = p_metallic;
	_invalidate_material();
}

float SDFMeshInstance3D::get_metallic() const {
	return metallic;
}

void SDFMeshInstance3D::set_roughness(float p_roughness) {
	p_roughness = CLAMP(p_roughness, 0.0f, 1.0f);
	if (Math::is_equal_approx(roughness, p_roughness)) {
		return;
	}

	roughness = p_roughness;
	_invalidate_material();
}

float SDFMeshInstance3D::get_roughness() const {
	return roughness;
}

void SDFMeshInstance3D::set_create_collision(bool p_enable) {
	if (create_collision == p_enable) {
		return;
	}

	create_collision = p_enable;
	call_deferred("_update_collision_shape");
}

bool SDFMeshInstance3D::get_create_collision() const {
	return create_collision;
}
