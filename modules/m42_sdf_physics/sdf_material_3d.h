/**************************************************************************/
/*  sdf_material_3d.h                                                     */
/**************************************************************************/
/*                   SDF Physics Module - Material System                */
/**************************************************************************/

#ifndef SDF_MATERIAL_3D_H
#define SDF_MATERIAL_3D_H

#include "scene/resources/material.h"
#include "scene/resources/texture.h"
#include "core/templates/self_list.h"

// Material class for SDF shapes with auto-generated shaders
// Provides BaseMaterial3D-like features with SDF raymarching
class SDFMaterial3D : public Material {
	GDCLASS(SDFMaterial3D, Material);

public:
	// === SDF Shape Types ===
	enum SDFType {
		SDF_TYPE_BOX,
		SDF_TYPE_SPHERE,
		SDF_TYPE_MAX
	};

	// === Reuse BaseMaterial3D enums for compatibility ===
	enum Feature {
		FEATURE_EMISSION,
		FEATURE_NORMAL_MAPPING,
		FEATURE_AMBIENT_OCCLUSION,
		FEATURE_HEIGHT_MAPPING,
		FEATURE_REFRACTION,
		FEATURE_MAX
	};

	enum BlendMode {
		BLEND_MODE_MIX,
		BLEND_MODE_ADD,
		BLEND_MODE_SUB,
		BLEND_MODE_MUL,
		BLEND_MODE_MAX
	};

	enum CullMode {
		CULL_BACK,
		CULL_FRONT,
		CULL_DISABLED,
		CULL_MAX
	};

	enum DepthDrawMode {
		DEPTH_DRAW_OPAQUE_ONLY,
		DEPTH_DRAW_ALWAYS,
		DEPTH_DRAW_DISABLED,
		DEPTH_DRAW_MAX
	};

	enum TextureParam {
		TEXTURE_ALBEDO,
		TEXTURE_METALLIC,
		TEXTURE_ROUGHNESS,
		TEXTURE_EMISSION,
		TEXTURE_NORMAL,
		TEXTURE_ORM, // Occlusion/Roughness/Metallic packed
		TEXTURE_MAX
	};

private:
	// === Shader Caching System ===
	union MaterialKey {
		struct {
			uint32_t sdf_type : 2;
			uint32_t blend_mode : 3;
			uint32_t cull_mode : 2;
			uint32_t depth_draw_mode : 2;
			uint32_t feature_mask : 6; // Which features are enabled
			uint32_t use_albedo_texture : 1;
			uint32_t use_orm_texture : 1;
			uint32_t use_normal_texture : 1;
			uint32_t use_emission_texture : 1;
			uint32_t invalid_key : 1;
		};
		uint32_t key = 0;

		MaterialKey() {
			memset(this, 0, sizeof(MaterialKey));
		}

		static uint32_t hash(const MaterialKey &p_key) {
			return hash_djb2_buffer((const uint8_t *)&p_key, sizeof(MaterialKey));
		}

		bool operator==(const MaterialKey &p_key) const {
			return memcmp(this, &p_key, sizeof(MaterialKey)) == 0;
		}

		bool operator<(const MaterialKey &p_key) const {
			return memcmp(this, &p_key, sizeof(MaterialKey)) < 0;
		}
	};

	struct ShaderData {
		RID shader;
		int users = 0;
	};

	static HashMap<MaterialKey, ShaderData, MaterialKey> shader_map;
	static Mutex shader_map_mutex;

	MaterialKey current_key;
	bool shader_dirty = true;

	// === Material RID Management ===
	mutable Mutex material_rid_mutex;
	RID material_rid;
	RID shader_rid;

	// === Deferred Update System ===
	static Mutex material_mutex;
	static SelfList<SDFMaterial3D>::List dirty_materials;
	SelfList<SDFMaterial3D> element;

	// === Standard Material Properties ===
	Color albedo = Color(1, 1, 1, 1);
	float specular = 0.5f;
	float metallic = 0.0f;
	float roughness = 0.5f;

	Color emission = Color(0, 0, 0, 0);
	float emission_energy = 1.0f;

	float normal_scale = 1.0f;
	float heightmap_scale = 0.05f;
	float refraction = 0.0f;
	float ao_light_affect = 0.0f;

	// === Texture References ===
	Ref<Texture2D> textures[TEXTURE_MAX];

	// === Feature Flags ===
	bool features[FEATURE_MAX] = {};

	// === Render Modes ===
	BlendMode blend_mode = BLEND_MODE_MIX;
	CullMode cull_mode = CULL_FRONT; // SDF raymarching requires front-face culling (default)
	DepthDrawMode depth_draw_mode = DEPTH_DRAW_ALWAYS;

	// === SDF-Specific Properties ===
	SDFType sdf_type = SDF_TYPE_BOX;
	Vector3 sdf_size = Vector3(2, 2, 2); // Box size
	float sdf_radius = 1.0f; // Sphere radius
	float sdf_roundness = 0.0f; // Box roundness (0.0-1.0)

	// === Shader Generation ===
	void _update_shader();
	void _queue_shader_change();
	MaterialKey _compute_key() const;
	String _generate_shader_code(const MaterialKey &p_key);
	String _generate_sdf_scene_code(const MaterialKey &p_key);
	String _generate_uv_code(const MaterialKey &p_key);

	// === Material RID Management ===
	void _check_material_rid();
	void _material_set_param(const StringName &p_name, const Variant &p_value);

	// === Shader Name Cache ===
	struct ShaderNames {
		StringName albedo;
		StringName specular;
		StringName metallic;
		StringName roughness;
		StringName emission;
		StringName emission_energy;
		StringName normal_scale;
		StringName heightmap_scale;
		StringName refraction;
		StringName ao_light_affect;
		StringName sdf_half_extents;
		StringName sdf_roundness;
		StringName sdf_radius;
		StringName texture_albedo;
		StringName texture_metallic;
		StringName texture_roughness;
		StringName texture_emission;
		StringName texture_normal;
		StringName texture_orm;
	};

	static ShaderNames *shader_names;

protected:
	static void _bind_methods();
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

	virtual bool _can_do_next_pass() const override { return true; }
	virtual bool _can_use_render_priority() const override { return true; }

public:
	// === Standard Material Setters/Getters ===
	void set_albedo(const Color &p_albedo);
	Color get_albedo() const;

	void set_specular(float p_specular);
	float get_specular() const;

	void set_metallic(float p_metallic);
	float get_metallic() const;

	void set_roughness(float p_roughness);
	float get_roughness() const;

	void set_emission(const Color &p_emission);
	Color get_emission() const;

	void set_emission_energy(float p_energy);
	float get_emission_energy() const;

	void set_normal_scale(float p_normal_scale);
	float get_normal_scale() const;

	void set_heightmap_scale(float p_heightmap_scale);
	float get_heightmap_scale() const;

	void set_refraction(float p_refraction);
	float get_refraction() const;

	void set_ao_light_affect(float p_ao_light_affect);
	float get_ao_light_affect() const;

	// === Texture Management ===
	void set_texture(TextureParam p_param, const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture(TextureParam p_param) const;

	// === Feature Flags ===
	void set_feature(Feature p_feature, bool p_enable);
	bool get_feature(Feature p_feature) const;

	// === Render Modes ===
	void set_blend_mode(BlendMode p_mode);
	BlendMode get_blend_mode() const;

	void set_cull_mode(CullMode p_mode);
	CullMode get_cull_mode() const;

	void set_depth_draw_mode(DepthDrawMode p_mode);
	DepthDrawMode get_depth_draw_mode() const;

	// === SDF-Specific Properties ===
	void set_sdf_type(SDFType p_type);
	SDFType get_sdf_type() const;

	void set_sdf_size(const Vector3 &p_size);
	Vector3 get_sdf_size() const;

	void set_sdf_radius(float p_radius);
	float get_sdf_radius() const;

	void set_sdf_roundness(float p_roundness);
	float get_sdf_roundness() const;

	// === Material Overrides ===
	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	// === Static Update Management ===
	static void flush_changes();
	static void finish_shaders();
	static void init_shaders();

	SDFMaterial3D();
	virtual ~SDFMaterial3D();
};

VARIANT_ENUM_CAST(SDFMaterial3D::SDFType)
VARIANT_ENUM_CAST(SDFMaterial3D::Feature)
VARIANT_ENUM_CAST(SDFMaterial3D::BlendMode)
VARIANT_ENUM_CAST(SDFMaterial3D::CullMode)
VARIANT_ENUM_CAST(SDFMaterial3D::DepthDrawMode)
VARIANT_ENUM_CAST(SDFMaterial3D::TextureParam)

#endif // SDF_MATERIAL_3D_H
