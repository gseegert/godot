/**************************************************************************/
/*  sdf_material_3d.cpp                                                   */
/**************************************************************************/
/*                   SDF Physics Module - Material System                */
/**************************************************************************/

#include "sdf_material_3d.h"
#include "servers/rendering_server.h"
#include "core/version.h"

// Generated shader header
#include "shaders/sdf_raymarch_inc.glsl.gen.h"

// ============================================================================
// Static Members
// ============================================================================

HashMap<SDFMaterial3D::MaterialKey, SDFMaterial3D::ShaderData, SDFMaterial3D::MaterialKey> SDFMaterial3D::shader_map;
Mutex SDFMaterial3D::shader_map_mutex;
Mutex SDFMaterial3D::material_mutex;
SelfList<SDFMaterial3D>::List SDFMaterial3D::dirty_materials;
SDFMaterial3D::ShaderNames *SDFMaterial3D::shader_names = nullptr;

// ============================================================================
// Initialization / Cleanup
// ============================================================================

void SDFMaterial3D::init_shaders() {
	shader_names = memnew(ShaderNames);

	shader_names->albedo = "albedo";
	shader_names->specular = "specular";
	shader_names->metallic = "metallic";
	shader_names->roughness = "roughness";
	shader_names->emission = "emission";
	shader_names->emission_energy = "emission_energy";
	shader_names->normal_scale = "normal_scale";
	shader_names->heightmap_scale = "heightmap_scale";
	shader_names->refraction = "refraction";
	shader_names->ao_light_affect = "ao_light_affect";

	shader_names->sdf_half_extents = "sdf_half_extents";
	shader_names->sdf_roundness = "sdf_roundness";
	shader_names->sdf_radius = "sdf_radius";

	shader_names->texture_albedo = "texture_albedo";
	shader_names->texture_metallic = "texture_metallic";
	shader_names->texture_roughness = "texture_roughness";
	shader_names->texture_emission = "texture_emission";
	shader_names->texture_normal = "texture_normal";
	shader_names->texture_orm = "texture_orm";
}

void SDFMaterial3D::finish_shaders() {
	if (shader_names) {
		memdelete(shader_names);
		shader_names = nullptr;
	}

	// Free all cached shaders
	MutexLock lock(shader_map_mutex);
	for (const KeyValue<MaterialKey, ShaderData> &E : shader_map) {
		RenderingServer::get_singleton()->free(E.value.shader);
	}
	shader_map.clear();
}

void SDFMaterial3D::flush_changes() {
	// Process dirty materials in batch
	MutexLock lock(material_mutex);

	while (dirty_materials.first()) {
		SelfList<SDFMaterial3D> *E = dirty_materials.first();
		dirty_materials.remove(E);
		E->self()->_update_shader();
	}
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

SDFMaterial3D::SDFMaterial3D() :
		element(this) {
	// Initialize with default material key
	current_key.invalid_key = 1; // Force initial shader generation

	// Mark for shader update
	_queue_shader_change();
}

SDFMaterial3D::~SDFMaterial3D() {
	// Remove from dirty list if present
	MutexLock lock(material_mutex);
	if (element.in_list()) {
		dirty_materials.remove(&element);
	}

	// Release shader
	MutexLock shader_lock(shader_map_mutex);
	if (shader_map.has(current_key)) {
		ShaderData *shader_data = shader_map.getptr(current_key);
		if (shader_data) {
			shader_data->users--;
			if (shader_data->users == 0) {
				RenderingServer::get_singleton()->free(shader_data->shader);
				shader_map.erase(current_key);
			}
		}
	}

	// Free material RID
	MutexLock material_lock(material_rid_mutex);
	if (material_rid.is_valid()) {
		RenderingServer::get_singleton()->free(material_rid);
	}
}

// ============================================================================
// Material Key Computation
// ============================================================================

SDFMaterial3D::MaterialKey SDFMaterial3D::_compute_key() const {
	MaterialKey mk;

	mk.sdf_type = sdf_type;
	mk.blend_mode = blend_mode;
	mk.cull_mode = cull_mode;
	mk.depth_draw_mode = depth_draw_mode;

	// Pack feature flags
	mk.feature_mask = 0;
	for (int i = 0; i < FEATURE_MAX; i++) {
		if (features[i]) {
			mk.feature_mask |= (1 << i);
		}
	}

	// Texture usage flags
	mk.use_albedo_texture = textures[TEXTURE_ALBEDO].is_valid() ? 1 : 0;
	mk.use_orm_texture = textures[TEXTURE_ORM].is_valid() ? 1 : 0;
	mk.use_normal_texture = textures[TEXTURE_NORMAL].is_valid() ? 1 : 0;
	mk.use_emission_texture = textures[TEXTURE_EMISSION].is_valid() ? 1 : 0;

	mk.invalid_key = 0;

	return mk;
}

// ============================================================================
// Deferred Update Management
// ============================================================================

void SDFMaterial3D::_queue_shader_change() {
	MutexLock lock(material_mutex);
	if (!element.in_list()) {
		dirty_materials.add(&element);
	}
}

void SDFMaterial3D::_check_material_rid() {
	MutexLock lock(material_rid_mutex);

	if (!material_rid.is_valid()) {
		material_rid = RenderingServer::get_singleton()->material_create();

		if (shader_rid.is_valid()) {
			RenderingServer::get_singleton()->material_set_shader(material_rid, shader_rid);
		}

		// Set next pass if exists
		Ref<Material> next = get_next_pass();
		RID next_pass_rid;
		if (next.is_valid()) {
			next_pass_rid = next->get_rid();
		}
		RenderingServer::get_singleton()->material_set_next_pass(material_rid, next_pass_rid);

		// Set render priority
		RenderingServer::get_singleton()->material_set_render_priority(material_rid, get_render_priority());
	}
}

void SDFMaterial3D::_material_set_param(const StringName &p_name, const Variant &p_value) {
	_check_material_rid();
	RenderingServer::get_singleton()->material_set_param(material_rid, p_name, p_value);
}

// ============================================================================
// Shader Generation - SDF Scene Code
// ============================================================================

String SDFMaterial3D::_generate_sdf_scene_code(const MaterialKey &p_key) {
	String code;

	switch (p_key.sdf_type) {
		case SDF_TYPE_BOX:
			code += R"(
// SDF scene function for box
float sdf_scene(vec3 p) {
	return sdf_box(p, sdf_half_extents, sdf_roundness);
}

// Normal computation for box
vec3 compute_sdf_normal(vec3 p) {
	return sdf_box_normal(p, sdf_half_extents, sdf_roundness);
}

// Bounding volume intersection for box
bool intersect_shape_bounds(vec3 ro, vec3 rd, out float t_near, out float t_far) {
	return intersect_box(ro, rd, sdf_half_extents * 1.1, t_near, t_far);
}
)";
			break;

		case SDF_TYPE_SPHERE:
			code += R"(
// SDF scene function for sphere
float sdf_scene(vec3 p) {
	return sdf_sphere(p, sdf_radius);
}

// Normal computation for sphere
vec3 compute_sdf_normal(vec3 p) {
	return sdf_sphere_normal(p);
}

// Bounding volume intersection for sphere
bool intersect_shape_bounds(vec3 ro, vec3 rd, out float t_near, out float t_far) {
	return intersect_sphere(ro, rd, sdf_radius * 1.1, t_near, t_far);
}
)";
			break;

		default:
			break;
	}

	// Raymarch function (common to all types)
	code += R"(
// Sphere tracing through SDF scene
bool raymarch_sdf(vec3 ray_origin, vec3 ray_dir, out vec3 hit_pos, out float total_t) {
	const int MAX_STEPS = 64;
	const float MIN_DIST = 0.001;
	const float MAX_DIST = 100.0;

	total_t = 0.0;

	for (int i = 0; i < MAX_STEPS; i++) {
		hit_pos = ray_origin + ray_dir * total_t;

		float dist = sdf_scene(hit_pos);

		if (dist < MIN_DIST) {
			return true;
		}

		total_t += dist;

		if (total_t > MAX_DIST) {
			break;
		}
	}

	return false;
}
)";

	return code;
}

// ============================================================================
// Shader Generation - UV Code
// ============================================================================

String SDFMaterial3D::_generate_uv_code(const MaterialKey &p_key) {
	String code;

	switch (p_key.sdf_type) {
		case SDF_TYPE_BOX:
			// Box UVs: triplanar-style based on dominant normal axis
			code += R"(
vec2 compute_uv(vec3 pos, vec3 normal) {
	vec3 abs_normal = abs(normal);
	vec2 uv;

	if (abs_normal.x > abs_normal.y && abs_normal.x > abs_normal.z) {
		// X-axis dominant
		uv = pos.yz;
	} else if (abs_normal.y > abs_normal.z) {
		// Y-axis dominant
		uv = pos.xz;
	} else {
		// Z-axis dominant
		uv = pos.xy;
	}

	// Normalize to 0-1 range based on box size
	uv = (uv / (sdf_half_extents.xy * 2.0)) + vec2(0.5);
	return uv;
}
)";
			break;

		case SDF_TYPE_SPHERE:
			// Sphere UVs: spherical coordinates
			code += R"(
vec2 compute_uv(vec3 pos, vec3 normal) {
	vec3 n = normalize(pos);
	float u = 0.5 + atan(n.z, n.x) / (2.0 * 3.14159265359);
	float v = 0.5 - asin(n.y) / 3.14159265359;
	return vec2(u, v);
}
)";
			break;

		default:
			code += "vec2 compute_uv(vec3 pos, vec3 normal) { return vec2(0.0); }\n";
			break;
	}

	return code;
}

// ============================================================================
// Shader Generation - Main Code
// ============================================================================

String SDFMaterial3D::_generate_shader_code(const MaterialKey &p_key) {
	String code = vformat("// Auto-generated SDF shader - Godot %s\n\n", VERSION_FULL_CONFIG);

	code += "shader_type spatial;\n";
	code += "render_mode ";

	// Cull mode
	switch (p_key.cull_mode) {
		case CULL_BACK:
			code += "cull_back";
			break;
		case CULL_FRONT:
			code += "cull_front";
			break;
		case CULL_DISABLED:
			code += "cull_disabled";
			break;
	}
	code += ", ";

	// Blend mode
	switch (p_key.blend_mode) {
		case BLEND_MODE_MIX:
			code += "blend_mix";
			break;
		case BLEND_MODE_ADD:
			code += "blend_add";
			break;
		case BLEND_MODE_SUB:
			code += "blend_sub";
			break;
		case BLEND_MODE_MUL:
			code += "blend_mul";
			break;
	}
	code += ", ";

	// Depth mode
	switch (p_key.depth_draw_mode) {
		case DEPTH_DRAW_ALWAYS:
			code += "depth_draw_always";
			break;
		case DEPTH_DRAW_OPAQUE_ONLY:
			code += "depth_draw_opaque";
			break;
		case DEPTH_DRAW_DISABLED:
			code += "depth_draw_never";
			break;
	}

	code += ", depth_prepass_alpha, specular_schlick_ggx;\n\n";

	// NOTE: depth_prepass_alpha enables alpha-tested geometry to participate in depth passes.
	// We use IN_SHADOW_PASS to ensure full SDF raymarching executes in shadow passes,
	// guaranteeing shadows match the actual SDF surface (not the proxy mesh).

	// === Uniforms ===
	code += "// Standard material properties\n";
	code += "uniform vec4 albedo : source_color = vec4(1.0);\n";
	code += "uniform float specular : hint_range(0.0, 1.0) = 0.5;\n";
	code += "uniform float metallic : hint_range(0.0, 1.0) = 0.0;\n";
	code += "uniform float roughness : hint_range(0.0, 1.0) = 0.5;\n";

	if (p_key.feature_mask & (1 << FEATURE_EMISSION)) {
		code += "\n// Emission\n";
		code += "uniform vec4 emission : source_color = vec4(0.0, 0.0, 0.0, 1.0);\n";
		code += "uniform float emission_energy = 1.0;\n";
	}

	if (p_key.feature_mask & (1 << FEATURE_NORMAL_MAPPING)) {
		code += "\n// Normal mapping\n";
		code += "uniform float normal_scale : hint_range(-16.0, 16.0) = 1.0;\n";
	}

	if (p_key.feature_mask & (1 << FEATURE_REFRACTION)) {
		code += "\n// Refraction\n";
		code += "uniform float refraction : hint_range(-1.0, 1.0) = 0.0;\n";
	}

	if (p_key.feature_mask & (1 << FEATURE_AMBIENT_OCCLUSION)) {
		code += "\n// Ambient occlusion\n";
		code += "uniform float ao_light_affect = 0.0;\n";
	}

	// Textures
	if (p_key.use_albedo_texture) {
		code += "\n// Albedo texture\n";
		code += "uniform sampler2D texture_albedo : source_color, filter_linear_mipmap, repeat_enable;\n";
	}

	if (p_key.use_orm_texture) {
		code += "\n// ORM texture (Occlusion/Roughness/Metallic)\n";
		code += "uniform sampler2D texture_orm : hint_default_white, filter_linear_mipmap, repeat_enable;\n";
	}

	if (p_key.use_normal_texture) {
		code += "\n// Normal map texture\n";
		code += "uniform sampler2D texture_normal : hint_normal, filter_linear_mipmap, repeat_enable;\n";
	}

	if (p_key.use_emission_texture) {
		code += "\n// Emission texture\n";
		code += "uniform sampler2D texture_emission : source_color, filter_linear_mipmap, repeat_enable;\n";
	}

	// Depth texture for manual depth testing
	code += "\n// Depth texture for manual depth testing\n";
	code += "uniform sampler2D DEPTH_TEXTURE : hint_depth_texture, filter_linear_mipmap;\n";

	// SDF-specific uniforms
	code += "\n// SDF shape parameters\n";
	switch (p_key.sdf_type) {
		case SDF_TYPE_BOX:
			code += "uniform vec3 sdf_half_extents = vec3(1.0);\n";
			code += "uniform float sdf_roundness = 0.0;\n";
			break;
		case SDF_TYPE_SPHERE:
			code += "uniform float sdf_radius = 1.0;\n";
			break;
	}

	code += "\n";

	// === Include SDF Library ===
	code += "// SDF raymarching library\n";
	code += String::utf8((const char *)sdf_raymarch_inc_shader_glsl);
	code += "\n\n";

	// === Shape-Specific Functions ===
	code += "// Shape-specific SDF functions\n";
	code += _generate_sdf_scene_code(p_key);
	code += "\n";

	// === UV Generation ===
	code += "// Procedural UV generation\n";
	code += _generate_uv_code(p_key);
	code += "\n";

	// === Fragment Shader ===
	code += R"(
void fragment() {
	// === SDF Raymarching ===
	mat4 world_to_local = inverse(MODEL_MATRIX);

	// Ray direction from camera/light to fragment
	vec3 ray_dir_view = normalize(VERTEX);
	vec3 ray_dir_world = (INV_VIEW_MATRIX * vec4(ray_dir_view, 0.0)).xyz;
	vec3 ray_dir_local = (world_to_local * vec4(ray_dir_world, 0.0)).xyz;

	// Camera/light position (origin in view space)
	vec3 cam_pos_world = (INV_VIEW_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 cam_pos_local = (world_to_local * vec4(cam_pos_world, 1.0)).xyz;

	// Intersect bounding volume
	float t_near, t_far;
	if (!intersect_shape_bounds(cam_pos_local, ray_dir_local, t_near, t_far)) {
		discard;
	}

	// Raymarch from near intersection
	vec3 march_start = cam_pos_local + ray_dir_local * max(t_near, 0.0);
	vec3 hit_pos;
	float total_t;
	if (!raymarch_sdf(march_start, ray_dir_local, hit_pos, total_t)) {
		discard;
	}

	// World-space hit position
	vec3 world_hit = (MODEL_MATRIX * vec4(hit_pos, 1.0)).xyz;

	// Compute depth
	vec4 clip_pos = PROJECTION_MATRIX * VIEW_MATRIX * vec4(world_hit, 1.0);
	float sdf_depth = clip_pos.z / clip_pos.w;

	// Manual depth test (only in main pass)
	if (!IN_SHADOW_PASS) {  // Main pass (not shadow pass)
		float scene_depth = texture(DEPTH_TEXTURE, SCREEN_UV).r;
		if (sdf_depth < scene_depth) {
			discard;
		}
	}

	DEPTH = sdf_depth;

	// === Compute normals and material properties ===
	vec3 local_normal = compute_sdf_normal(hit_pos);
	vec3 world_normal = normalize((MODEL_MATRIX * vec4(local_normal, 0.0)).xyz);

	// Generate UVs
	vec2 base_uv = compute_uv(hit_pos, local_normal);

	// === PBR Material Properties ===
)";

	// Albedo
	if (p_key.use_albedo_texture) {
		code += "\tALBEDO = texture(texture_albedo, base_uv).rgb * albedo.rgb;\n";
	} else {
		code += "\tALBEDO = albedo.rgb;\n";
	}

	// ORM or individual metallic/roughness
	if (p_key.use_orm_texture) {
		code += "\tvec3 orm = texture(texture_orm, base_uv).rgb;\n";
		if (p_key.feature_mask & (1 << FEATURE_AMBIENT_OCCLUSION)) {
			code += "\tAO = orm.r;\n";
			code += "\tAO_LIGHT_AFFECT = ao_light_affect;\n";
		}
		code += "\tROUGHNESS = orm.g * roughness;\n";
		code += "\tMETALLIC = orm.b * metallic;\n";
	} else {
		code += "\tROUGHNESS = roughness;\n";
		code += "\tMETALLIC = metallic;\n";
	}

	code += "\tSPECULAR = specular;\n";

	// Normal mapping
	if (p_key.use_normal_texture) {
		code += "\n\t// Normal mapping\n";
		code += "\tvec3 normal_map = texture(texture_normal, base_uv).rgb * 2.0 - 1.0;\n";
		code += "\tnormal_map.xy *= normal_scale;\n";
		code += "\t// Build TBN from world normal\n";
		code += "\tvec3 Q1 = dFdx(world_hit);\n";
		code += "\tvec3 Q2 = dFdy(world_hit);\n";
		code += "\tvec2 st1 = dFdx(base_uv);\n";
		code += "\tvec2 st2 = dFdy(base_uv);\n";
		code += "\tvec3 T = normalize(Q1 * st2.t - Q2 * st1.t);\n";
		code += "\tvec3 B = -normalize(cross(world_normal, T));\n";
		code += "\tmat3 TBN = mat3(T, B, world_normal);\n";
		code += "\tNORMAL = normalize(TBN * normal_map);\n";
	} else {
		code += "\tNORMAL = world_normal;\n";
	}

	// Emission
	if (p_key.feature_mask & (1 << FEATURE_EMISSION)) {
		if (p_key.use_emission_texture) {
			code += "\n\t// Emission with texture\n";
			code += "\tEMISSION = texture(texture_emission, base_uv).rgb * emission.rgb * emission_energy;\n";
		} else {
			code += "\n\t// Emission\n";
			code += "\tEMISSION = emission.rgb * emission_energy;\n";
		}
	}

	// Refraction
	if (p_key.feature_mask & (1 << FEATURE_REFRACTION)) {
		code += "\n\t// Refraction\n";
		code += "\tREFRACTION = refraction;\n";
	}

	code += "}\n";

	return code;
}

// ============================================================================
// Shader Update
// ============================================================================

void SDFMaterial3D::_update_shader() {
	MaterialKey mk = _compute_key();

	if (mk == current_key && !shader_dirty) {
		return; // No change needed
	}

	// Release old shader
	{
		MutexLock lock(shader_map_mutex);
		if (shader_map.has(current_key)) {
			ShaderData *shader_data = shader_map.getptr(current_key);
			if (shader_data) {
				shader_data->users--;
				if (shader_data->users == 0) {
					RenderingServer::get_singleton()->free(shader_data->shader);
					shader_map.erase(current_key);
				}
			}
		}
	}

	current_key = mk;
	shader_dirty = false;

	// Check cache for new shader
	{
		MutexLock lock(shader_map_mutex);
		if (shader_map.has(mk)) {
			ShaderData *shader_data = shader_map.getptr(mk);
			shader_data->users++;
			shader_rid = shader_data->shader;

			// Update material's shader
			_check_material_rid();
			RenderingServer::get_singleton()->material_set_shader(material_rid, shader_rid);

			// Update all parameters
			_material_set_param(shader_names->albedo, albedo);
			_material_set_param(shader_names->specular, specular);
			_material_set_param(shader_names->metallic, metallic);
			_material_set_param(shader_names->roughness, roughness);

			if (features[FEATURE_EMISSION]) {
				_material_set_param(shader_names->emission, emission);
				_material_set_param(shader_names->emission_energy, emission_energy);
			}

			if (features[FEATURE_NORMAL_MAPPING]) {
				_material_set_param(shader_names->normal_scale, normal_scale);
			}

			// SDF parameters
			switch (sdf_type) {
				case SDF_TYPE_BOX: {
					Vector3 half_extents = sdf_size * 0.5;
					float r = sdf_roundness * MIN(half_extents.x, MIN(half_extents.y, half_extents.z));
					_material_set_param(shader_names->sdf_half_extents, half_extents);
					_material_set_param(shader_names->sdf_roundness, r);
				} break;
				case SDF_TYPE_SPHERE:
					_material_set_param(shader_names->sdf_radius, sdf_radius);
					break;
			}

			return; // Shader found in cache
		}
	}

	// Generate new shader code
	String code = _generate_shader_code(mk);

	// Create shader (outside mutex to avoid deadlock)
	RID new_shader = RenderingServer::get_singleton()->shader_create();
	RenderingServer::get_singleton()->shader_set_code(new_shader, code);

	// Insert into cache
	{
		MutexLock lock(shader_map_mutex);
		ShaderData shader_data;
		shader_data.shader = new_shader;
		shader_data.users = 1;
		shader_map[mk] = shader_data;
		shader_rid = new_shader;
	}

	// Update material's shader
	_check_material_rid();
	RenderingServer::get_singleton()->material_set_shader(material_rid, shader_rid);

	// Set all shader parameters
	_material_set_param(shader_names->albedo, albedo);
	_material_set_param(shader_names->specular, specular);
	_material_set_param(shader_names->metallic, metallic);
	_material_set_param(shader_names->roughness, roughness);

	if (features[FEATURE_EMISSION]) {
		_material_set_param(shader_names->emission, emission);
		_material_set_param(shader_names->emission_energy, emission_energy);
	}

	if (features[FEATURE_NORMAL_MAPPING]) {
		_material_set_param(shader_names->normal_scale, normal_scale);
	}

	if (features[FEATURE_REFRACTION]) {
		_material_set_param(shader_names->refraction, refraction);
	}

	if (features[FEATURE_AMBIENT_OCCLUSION]) {
		_material_set_param(shader_names->ao_light_affect, ao_light_affect);
	}

	// SDF parameters
	switch (sdf_type) {
		case SDF_TYPE_BOX: {
			Vector3 half_extents = sdf_size * 0.5;
			float r = sdf_roundness * MIN(half_extents.x, MIN(half_extents.y, half_extents.z));
			_material_set_param(shader_names->sdf_half_extents, half_extents);
			_material_set_param(shader_names->sdf_roundness, r);
		} break;
		case SDF_TYPE_SPHERE:
			_material_set_param(shader_names->sdf_radius, sdf_radius);
			break;
	}

	// Textures
	if (textures[TEXTURE_ALBEDO].is_valid()) {
		_material_set_param(shader_names->texture_albedo, textures[TEXTURE_ALBEDO]);
	}
	if (textures[TEXTURE_ORM].is_valid()) {
		_material_set_param(shader_names->texture_orm, textures[TEXTURE_ORM]);
	}
	if (textures[TEXTURE_NORMAL].is_valid()) {
		_material_set_param(shader_names->texture_normal, textures[TEXTURE_NORMAL]);
	}
	if (textures[TEXTURE_EMISSION].is_valid()) {
		_material_set_param(shader_names->texture_emission, textures[TEXTURE_EMISSION]);
	}
}

// ============================================================================
// Material Overrides
// ============================================================================

Shader::Mode SDFMaterial3D::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID SDFMaterial3D::get_shader_rid() const {
	return shader_rid;
}

RID SDFMaterial3D::get_rid() const {
	const_cast<SDFMaterial3D *>(this)->_check_material_rid();
	return material_rid;
}

// ============================================================================
// Property Setters/Getters
// ============================================================================

void SDFMaterial3D::set_albedo(const Color &p_albedo) {
	albedo = p_albedo;
	_material_set_param(shader_names->albedo, albedo);
}

Color SDFMaterial3D::get_albedo() const {
	return albedo;
}

void SDFMaterial3D::set_specular(float p_specular) {
	specular = CLAMP(p_specular, 0.0f, 1.0f);
	_material_set_param(shader_names->specular, specular);
}

float SDFMaterial3D::get_specular() const {
	return specular;
}

void SDFMaterial3D::set_metallic(float p_metallic) {
	metallic = CLAMP(p_metallic, 0.0f, 1.0f);
	_material_set_param(shader_names->metallic, metallic);
}

float SDFMaterial3D::get_metallic() const {
	return metallic;
}

void SDFMaterial3D::set_roughness(float p_roughness) {
	roughness = CLAMP(p_roughness, 0.0f, 1.0f);
	_material_set_param(shader_names->roughness, roughness);
}

float SDFMaterial3D::get_roughness() const {
	return roughness;
}

void SDFMaterial3D::set_emission(const Color &p_emission) {
	emission = p_emission;
	if (features[FEATURE_EMISSION]) {
		_material_set_param(shader_names->emission, emission);
	} else {
		set_feature(FEATURE_EMISSION, true);
	}
}

Color SDFMaterial3D::get_emission() const {
	return emission;
}

void SDFMaterial3D::set_emission_energy(float p_energy) {
	emission_energy = p_energy;
	if (features[FEATURE_EMISSION]) {
		_material_set_param(shader_names->emission_energy, emission_energy);
	}
}

float SDFMaterial3D::get_emission_energy() const {
	return emission_energy;
}

void SDFMaterial3D::set_normal_scale(float p_normal_scale) {
	normal_scale = p_normal_scale;
	if (features[FEATURE_NORMAL_MAPPING]) {
		_material_set_param(shader_names->normal_scale, normal_scale);
	}
}

float SDFMaterial3D::get_normal_scale() const {
	return normal_scale;
}

void SDFMaterial3D::set_heightmap_scale(float p_heightmap_scale) {
	heightmap_scale = p_heightmap_scale;
}

float SDFMaterial3D::get_heightmap_scale() const {
	return heightmap_scale;
}

void SDFMaterial3D::set_refraction(float p_refraction) {
	refraction = CLAMP(p_refraction, -1.0f, 1.0f);
	if (features[FEATURE_REFRACTION]) {
		_material_set_param(shader_names->refraction, refraction);
	}
}

float SDFMaterial3D::get_refraction() const {
	return refraction;
}

void SDFMaterial3D::set_ao_light_affect(float p_ao_light_affect) {
	ao_light_affect = p_ao_light_affect;
	if (features[FEATURE_AMBIENT_OCCLUSION]) {
		_material_set_param(shader_names->ao_light_affect, ao_light_affect);
	}
}

float SDFMaterial3D::get_ao_light_affect() const {
	return ao_light_affect;
}

// === Textures ===

void SDFMaterial3D::set_texture(TextureParam p_param, const Ref<Texture2D> &p_texture) {
	ERR_FAIL_INDEX(p_param, TEXTURE_MAX);

	textures[p_param] = p_texture;

	// Texture changes require shader regeneration
	_queue_shader_change();
}

Ref<Texture2D> SDFMaterial3D::get_texture(TextureParam p_param) const {
	ERR_FAIL_INDEX_V(p_param, TEXTURE_MAX, Ref<Texture2D>());
	return textures[p_param];
}

// === Features ===

void SDFMaterial3D::set_feature(Feature p_feature, bool p_enable) {
	ERR_FAIL_INDEX(p_feature, FEATURE_MAX);

	if (features[p_feature] == p_enable) {
		return;
	}

	features[p_feature] = p_enable;
	_queue_shader_change();
}

bool SDFMaterial3D::get_feature(Feature p_feature) const {
	ERR_FAIL_INDEX_V(p_feature, FEATURE_MAX, false);
	return features[p_feature];
}

// === Render Modes ===

void SDFMaterial3D::set_blend_mode(BlendMode p_mode) {
	if (blend_mode == p_mode) {
		return;
	}

	blend_mode = p_mode;
	_queue_shader_change();
}

SDFMaterial3D::BlendMode SDFMaterial3D::get_blend_mode() const {
	return blend_mode;
}

void SDFMaterial3D::set_cull_mode(CullMode p_mode) {
	if (cull_mode == p_mode) {
		return;
	}

	cull_mode = p_mode;
	_queue_shader_change();
}

SDFMaterial3D::CullMode SDFMaterial3D::get_cull_mode() const {
	return cull_mode;
}

void SDFMaterial3D::set_depth_draw_mode(DepthDrawMode p_mode) {
	if (depth_draw_mode == p_mode) {
		return;
	}

	depth_draw_mode = p_mode;
	_queue_shader_change();
}

SDFMaterial3D::DepthDrawMode SDFMaterial3D::get_depth_draw_mode() const {
	return depth_draw_mode;
}

// === SDF Properties ===

void SDFMaterial3D::set_sdf_type(SDFType p_type) {
	if (sdf_type == p_type) {
		return;
	}

	sdf_type = p_type;
	_queue_shader_change();
	notify_property_list_changed();
}

SDFMaterial3D::SDFType SDFMaterial3D::get_sdf_type() const {
	return sdf_type;
}

void SDFMaterial3D::set_sdf_size(const Vector3 &p_size) {
	Vector3 clamped = Vector3(
			MAX(p_size.x, 0.001f),
			MAX(p_size.y, 0.001f),
			MAX(p_size.z, 0.001f));

	if (sdf_size == clamped) {
		return;
	}

	sdf_size = clamped;

	// Update shader parameter immediately
	if (sdf_type == SDF_TYPE_BOX) {
		Vector3 half_extents = sdf_size * 0.5;
		_material_set_param(shader_names->sdf_half_extents, half_extents);

		// Recalculate roundness radius
		float r = sdf_roundness * MIN(half_extents.x, MIN(half_extents.y, half_extents.z));
		_material_set_param(shader_names->sdf_roundness, r);
	}
}

Vector3 SDFMaterial3D::get_sdf_size() const {
	return sdf_size;
}

void SDFMaterial3D::set_sdf_radius(float p_radius) {
	p_radius = MAX(p_radius, 0.001f);

	if (Math::is_equal_approx(sdf_radius, p_radius)) {
		return;
	}

	sdf_radius = p_radius;

	// Update shader parameter immediately
	if (sdf_type == SDF_TYPE_SPHERE) {
		_material_set_param(shader_names->sdf_radius, sdf_radius);
	}
}

float SDFMaterial3D::get_sdf_radius() const {
	return sdf_radius;
}

void SDFMaterial3D::set_sdf_roundness(float p_roundness) {
	p_roundness = CLAMP(p_roundness, 0.0f, 1.0f);

	if (Math::is_equal_approx(sdf_roundness, p_roundness)) {
		return;
	}

	sdf_roundness = p_roundness;

	// Update shader parameter immediately
	if (sdf_type == SDF_TYPE_BOX) {
		Vector3 half_extents = sdf_size * 0.5;
		float r = sdf_roundness * MIN(half_extents.x, MIN(half_extents.y, half_extents.z));
		_material_set_param(shader_names->sdf_roundness, r);
	}
}

float SDFMaterial3D::get_sdf_roundness() const {
	return sdf_roundness;
}

// ============================================================================
// Property List (for editor)
// ============================================================================

bool SDFMaterial3D::_set(const StringName &p_name, const Variant &p_value) {
	String name_str = String(p_name);

	// Handle SDF-specific properties
	if (name_str == "sdf_size") {
		set_sdf_size(p_value);
		return true;
	} else if (name_str == "sdf_radius") {
		set_sdf_radius(p_value);
		return true;
	} else if (name_str == "sdf_roundness") {
		set_sdf_roundness(p_value);
		return true;
	}

	// Handle dynamic texture properties
	if (name_str.begins_with("texture_")) {
		if (name_str == "texture_albedo") {
			set_texture(TEXTURE_ALBEDO, p_value);
			return true;
		} else if (name_str == "texture_normal") {
			set_texture(TEXTURE_NORMAL, p_value);
			return true;
		} else if (name_str == "texture_orm") {
			set_texture(TEXTURE_ORM, p_value);
			return true;
		} else if (name_str == "texture_emission") {
			set_texture(TEXTURE_EMISSION, p_value);
			return true;
		}
	}

	return false;
}

bool SDFMaterial3D::_get(const StringName &p_name, Variant &r_ret) const {
	String name_str = String(p_name);

	// Handle SDF-specific properties
	if (name_str == "sdf_size") {
		r_ret = get_sdf_size();
		return true;
	} else if (name_str == "sdf_radius") {
		r_ret = get_sdf_radius();
		return true;
	} else if (name_str == "sdf_roundness") {
		r_ret = get_sdf_roundness();
		return true;
	}

	// Handle textures
	if (name_str.begins_with("texture_")) {
		if (name_str == "texture_albedo") {
			r_ret = get_texture(TEXTURE_ALBEDO);
			return true;
		} else if (name_str == "texture_normal") {
			r_ret = get_texture(TEXTURE_NORMAL);
			return true;
		} else if (name_str == "texture_orm") {
			r_ret = get_texture(TEXTURE_ORM);
			return true;
		} else if (name_str == "texture_emission") {
			r_ret = get_texture(TEXTURE_EMISSION);
			return true;
		}
	}

	return false;
}

void SDFMaterial3D::_get_property_list(List<PropertyInfo> *p_list) const {
	// Conditionally show properties based on SDF type
	// All SDF properties are always present, just hidden based on type
	uint32_t box_usage = (sdf_type == SDF_TYPE_BOX) ? PROPERTY_USAGE_DEFAULT : PROPERTY_USAGE_STORAGE;
	uint32_t sphere_usage = (sdf_type == SDF_TYPE_SPHERE) ? PROPERTY_USAGE_DEFAULT : PROPERTY_USAGE_STORAGE;

	p_list->push_back(PropertyInfo(Variant::VECTOR3, "sdf_size", PROPERTY_HINT_NONE, "suffix:m", box_usage));
	p_list->push_back(PropertyInfo(Variant::FLOAT, "sdf_roundness", PROPERTY_HINT_RANGE, "0.0,1.0,0.01", box_usage));
	p_list->push_back(PropertyInfo(Variant::FLOAT, "sdf_radius", PROPERTY_HINT_RANGE, "0.001,100.0,0.001,or_greater,suffix:m", sphere_usage));
}

// ============================================================================
// Property Binding
// ============================================================================

void SDFMaterial3D::_bind_methods() {
	// Standard material properties
	ClassDB::bind_method(D_METHOD("set_albedo", "albedo"), &SDFMaterial3D::set_albedo);
	ClassDB::bind_method(D_METHOD("get_albedo"), &SDFMaterial3D::get_albedo);

	ClassDB::bind_method(D_METHOD("set_specular", "specular"), &SDFMaterial3D::set_specular);
	ClassDB::bind_method(D_METHOD("get_specular"), &SDFMaterial3D::get_specular);

	ClassDB::bind_method(D_METHOD("set_metallic", "metallic"), &SDFMaterial3D::set_metallic);
	ClassDB::bind_method(D_METHOD("get_metallic"), &SDFMaterial3D::get_metallic);

	ClassDB::bind_method(D_METHOD("set_roughness", "roughness"), &SDFMaterial3D::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &SDFMaterial3D::get_roughness);

	ClassDB::bind_method(D_METHOD("set_emission", "emission"), &SDFMaterial3D::set_emission);
	ClassDB::bind_method(D_METHOD("get_emission"), &SDFMaterial3D::get_emission);

	ClassDB::bind_method(D_METHOD("set_emission_energy", "energy"), &SDFMaterial3D::set_emission_energy);
	ClassDB::bind_method(D_METHOD("get_emission_energy"), &SDFMaterial3D::get_emission_energy);

	ClassDB::bind_method(D_METHOD("set_normal_scale", "normal_scale"), &SDFMaterial3D::set_normal_scale);
	ClassDB::bind_method(D_METHOD("get_normal_scale"), &SDFMaterial3D::get_normal_scale);

	ClassDB::bind_method(D_METHOD("set_refraction", "refraction"), &SDFMaterial3D::set_refraction);
	ClassDB::bind_method(D_METHOD("get_refraction"), &SDFMaterial3D::get_refraction);

	ClassDB::bind_method(D_METHOD("set_ao_light_affect", "amount"), &SDFMaterial3D::set_ao_light_affect);
	ClassDB::bind_method(D_METHOD("get_ao_light_affect"), &SDFMaterial3D::get_ao_light_affect);

	// Textures
	ClassDB::bind_method(D_METHOD("set_texture", "param", "texture"), &SDFMaterial3D::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture", "param"), &SDFMaterial3D::get_texture);

	// Features
	ClassDB::bind_method(D_METHOD("set_feature", "feature", "enable"), &SDFMaterial3D::set_feature);
	ClassDB::bind_method(D_METHOD("get_feature", "feature"), &SDFMaterial3D::get_feature);

	// Render modes
	ClassDB::bind_method(D_METHOD("set_blend_mode", "mode"), &SDFMaterial3D::set_blend_mode);
	ClassDB::bind_method(D_METHOD("get_blend_mode"), &SDFMaterial3D::get_blend_mode);

	ClassDB::bind_method(D_METHOD("set_cull_mode", "mode"), &SDFMaterial3D::set_cull_mode);
	ClassDB::bind_method(D_METHOD("get_cull_mode"), &SDFMaterial3D::get_cull_mode);

	ClassDB::bind_method(D_METHOD("set_depth_draw_mode", "mode"), &SDFMaterial3D::set_depth_draw_mode);
	ClassDB::bind_method(D_METHOD("get_depth_draw_mode"), &SDFMaterial3D::get_depth_draw_mode);

	// SDF properties
	ClassDB::bind_method(D_METHOD("set_sdf_type", "type"), &SDFMaterial3D::set_sdf_type);
	ClassDB::bind_method(D_METHOD("get_sdf_type"), &SDFMaterial3D::get_sdf_type);

	ClassDB::bind_method(D_METHOD("set_sdf_size", "size"), &SDFMaterial3D::set_sdf_size);
	ClassDB::bind_method(D_METHOD("get_sdf_size"), &SDFMaterial3D::get_sdf_size);

	ClassDB::bind_method(D_METHOD("set_sdf_radius", "radius"), &SDFMaterial3D::set_sdf_radius);
	ClassDB::bind_method(D_METHOD("get_sdf_radius"), &SDFMaterial3D::get_sdf_radius);

	ClassDB::bind_method(D_METHOD("set_sdf_roundness", "roundness"), &SDFMaterial3D::set_sdf_roundness);
	ClassDB::bind_method(D_METHOD("get_sdf_roundness"), &SDFMaterial3D::get_sdf_roundness);

	// Properties
	ADD_GROUP("SDF Shape", "sdf_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sdf_type", PROPERTY_HINT_ENUM, "Box,Sphere"), "set_sdf_type", "get_sdf_type");

	ADD_GROUP("Albedo", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "albedo", PROPERTY_HINT_COLOR_NO_ALPHA), "set_albedo", "get_albedo");

	ADD_GROUP("Metallic", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "metallic", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_metallic", "get_metallic");

	ADD_GROUP("Roughness", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_roughness", "get_roughness");

	ADD_GROUP("Specular", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "specular", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_specular", "get_specular");

	ADD_GROUP("Emission", "emission_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emission_enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "emission", PROPERTY_HINT_COLOR_NO_ALPHA), "set_emission", "get_emission");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "emission_energy", PROPERTY_HINT_RANGE, "0.0,16.0,0.01,or_greater"), "set_emission_energy", "get_emission_energy");

	ADD_GROUP("Render Modes", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_mode", PROPERTY_HINT_ENUM, "Mix,Add,Sub,Mul"), "set_blend_mode", "get_blend_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cull_mode", PROPERTY_HINT_ENUM, "Back,Front,Disabled"), "set_cull_mode", "get_cull_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "depth_draw_mode", PROPERTY_HINT_ENUM, "Opaque Only,Always,Disabled"), "set_depth_draw_mode", "get_depth_draw_mode");

	// Enums
	BIND_ENUM_CONSTANT(SDF_TYPE_BOX);
	BIND_ENUM_CONSTANT(SDF_TYPE_SPHERE);
	BIND_ENUM_CONSTANT(SDF_TYPE_MAX);

	BIND_ENUM_CONSTANT(FEATURE_EMISSION);
	BIND_ENUM_CONSTANT(FEATURE_NORMAL_MAPPING);
	BIND_ENUM_CONSTANT(FEATURE_AMBIENT_OCCLUSION);
	BIND_ENUM_CONSTANT(FEATURE_HEIGHT_MAPPING);
	BIND_ENUM_CONSTANT(FEATURE_REFRACTION);
	BIND_ENUM_CONSTANT(FEATURE_MAX);

	BIND_ENUM_CONSTANT(BLEND_MODE_MIX);
	BIND_ENUM_CONSTANT(BLEND_MODE_ADD);
	BIND_ENUM_CONSTANT(BLEND_MODE_SUB);
	BIND_ENUM_CONSTANT(BLEND_MODE_MUL);
	BIND_ENUM_CONSTANT(BLEND_MODE_MAX);

	BIND_ENUM_CONSTANT(CULL_BACK);
	BIND_ENUM_CONSTANT(CULL_FRONT);
	BIND_ENUM_CONSTANT(CULL_DISABLED);
	BIND_ENUM_CONSTANT(CULL_MAX);

	BIND_ENUM_CONSTANT(DEPTH_DRAW_OPAQUE_ONLY);
	BIND_ENUM_CONSTANT(DEPTH_DRAW_ALWAYS);
	BIND_ENUM_CONSTANT(DEPTH_DRAW_DISABLED);
	BIND_ENUM_CONSTANT(DEPTH_DRAW_MAX);

	BIND_ENUM_CONSTANT(TEXTURE_ALBEDO);
	BIND_ENUM_CONSTANT(TEXTURE_METALLIC);
	BIND_ENUM_CONSTANT(TEXTURE_ROUGHNESS);
	BIND_ENUM_CONSTANT(TEXTURE_EMISSION);
	BIND_ENUM_CONSTANT(TEXTURE_NORMAL);
	BIND_ENUM_CONSTANT(TEXTURE_ORM);
	BIND_ENUM_CONSTANT(TEXTURE_MAX);
}
