#include <stddef.h>
#include <stdint.h>

/**
 * Enable flag for each OpenGL extension.  Different device drivers will
 * enable different extensions at runtime.
 */
struct gl_extensions
{
   uint32_t dummy;  /* don't remove this! */
   uint32_t dummy_true;  /* Set true by _mesa_init_extensions(). */
   uint32_t ANGLE_texture_compression_dxt;
   uint32_t ARB_ES2_compatibility;
   uint32_t ARB_ES3_compatibility;
   uint32_t ARB_ES3_1_compatibility;
   uint32_t ARB_ES3_2_compatibility;
   uint32_t ARB_arrays_of_arrays;
   uint32_t ARB_base_instance;
   uint32_t ARB_bindless_texture;
   uint32_t ARB_blend_func_extended;
   uint32_t ARB_buffer_storage;
   uint32_t ARB_clip_control;
   uint32_t ARB_color_buffer_float;
   uint32_t ARB_compatibility;
   uint32_t ARB_compute_shader;
   uint32_t ARB_compute_variable_group_size;
   uint32_t ARB_conditional_render_inverted;
   uint32_t ARB_conservative_depth;
   uint32_t ARB_copy_image;
   uint32_t ARB_cull_distance;
   uint32_t ARB_depth_buffer_float;
   uint32_t ARB_depth_clamp;
   uint32_t ARB_derivative_control;
   uint32_t ARB_draw_buffers_blend;
   uint32_t ARB_draw_elements_base_vertex;
   uint32_t ARB_draw_indirect;
   uint32_t ARB_draw_instanced;
   uint32_t ARB_fragment_coord_conventions;
   uint32_t ARB_fragment_layer_viewport;
   uint32_t ARB_fragment_program;
   uint32_t ARB_fragment_program_shadow;
   uint32_t ARB_fragment_shader;
   uint32_t ARB_framebuffer_no_attachments;
   uint32_t ARB_framebuffer_object;
   uint32_t ARB_fragment_shader_interlock;
   uint32_t ARB_enhanced_layouts;
   uint32_t ARB_explicit_attrib_location;
   uint32_t ARB_explicit_uniform_location;
   uint32_t ARB_gl_spirv;
   uint32_t ARB_gpu_shader5;
   uint32_t ARB_gpu_shader_fp64;
   uint32_t ARB_gpu_shader_int64;
   uint32_t ARB_half_float_vertex;
   uint32_t ARB_indirect_parameters;
   uint32_t ARB_instanced_arrays;
   uint32_t ARB_internalformat_query;
   uint32_t ARB_internalformat_query2;
   uint32_t ARB_map_buffer_range;
   uint32_t ARB_occlusion_query;
   uint32_t ARB_occlusion_query2;
   uint32_t ARB_pipeline_statistics_query;
   uint32_t ARB_polygon_offset_clamp;
   uint32_t ARB_post_depth_coverage;
   uint32_t ARB_query_buffer_object;
   uint32_t ARB_robust_buffer_access_behavior;
   uint32_t ARB_sample_locations;
   uint32_t ARB_sample_shading;
   uint32_t ARB_seamless_cube_map;
   uint32_t ARB_shader_atomic_counter_ops;
   uint32_t ARB_shader_atomic_counters;
   uint32_t ARB_shader_ballot;
   uint32_t ARB_shader_bit_encoding;
   uint32_t ARB_shader_clock;
   uint32_t ARB_shader_draw_parameters;
   uint32_t ARB_shader_group_vote;
   uint32_t ARB_shader_image_load_store;
   uint32_t ARB_shader_image_size;
   uint32_t ARB_shader_precision;
   uint32_t ARB_shader_stencil_export;
   uint32_t ARB_shader_storage_buffer_object;
   uint32_t ARB_shader_texture_image_samples;
   uint32_t ARB_shader_texture_lod;
   uint32_t ARB_shader_viewport_layer_array;
   uint32_t ARB_shading_language_packing;
   uint32_t ARB_shading_language_420pack;
   uint32_t ARB_shadow;
   uint32_t ARB_sparse_buffer;
   uint32_t ARB_sparse_texture;
   uint32_t ARB_sparse_texture2;
   uint32_t ARB_sparse_texture_clamp;
   uint32_t ARB_stencil_texturing;
   uint32_t ARB_spirv_extensions;
   uint32_t ARB_sync;
   uint32_t ARB_tessellation_shader;
   uint32_t ARB_texture_buffer_object;
   uint32_t ARB_texture_buffer_object_rgb32;
   uint32_t ARB_texture_buffer_range;
   uint32_t ARB_texture_compression_bptc;
   uint32_t ARB_texture_compression_rgtc;
   uint32_t ARB_texture_cube_map_array;
   uint32_t ARB_texture_filter_anisotropic;
   uint32_t ARB_texture_filter_minmax;
   uint32_t ARB_texture_float;
   uint32_t ARB_texture_gather;
   uint32_t ARB_texture_mirror_clamp_to_edge;
   uint32_t ARB_texture_multisample;
   uint32_t ARB_texture_non_power_of_two;
   uint32_t ARB_texture_stencil8;
   uint32_t ARB_texture_query_levels;
   uint32_t ARB_texture_query_lod;
   uint32_t ARB_texture_rg;
   uint32_t ARB_texture_rgb10_a2ui;
   uint32_t ARB_texture_view;
   uint32_t ARB_timer_query;
   uint32_t ARB_transform_feedback2;
   uint32_t ARB_transform_feedback3;
   uint32_t ARB_transform_feedback_instanced;
   uint32_t ARB_transform_feedback_overflow_query;
   uint32_t ARB_uniform_buffer_object;
   uint32_t ARB_vertex_attrib_64bit;
   uint32_t ARB_vertex_program;
   uint32_t ARB_vertex_shader;
   uint32_t ARB_vertex_type_10f_11f_11f_rev;
   uint32_t ARB_vertex_type_2_10_10_10_rev;
   uint32_t ARB_viewport_array;
   uint32_t EXT_blend_equation_separate;
   uint32_t EXT_color_buffer_float;
   uint32_t EXT_color_buffer_half_float;
   uint32_t EXT_demote_to_helper_invocation;
   uint32_t EXT_depth_bounds_test;
   uint32_t EXT_disjoint_timer_query;
   uint32_t EXT_draw_buffers2;
   uint32_t EXT_EGL_image_storage;
   uint32_t EXT_float_blend;
   uint32_t EXT_framebuffer_multisample;
   uint32_t EXT_framebuffer_multisample_blit_scaled;
   uint32_t EXT_framebuffer_sRGB;
   uint32_t EXT_gpu_program_parameters;
   uint32_t EXT_gpu_shader4;
   uint32_t EXT_memory_object;
   uint32_t EXT_memory_object_fd;
   uint32_t EXT_memory_object_win32;
   uint32_t EXT_multisampled_render_to_texture;
   uint32_t EXT_packed_float;
   uint32_t EXT_provoking_vertex;
   uint32_t EXT_render_snorm;
   uint32_t EXT_semaphore;
   uint32_t EXT_semaphore_fd;
   uint32_t EXT_semaphore_win32;
   uint32_t EXT_shader_image_load_formatted;
   uint32_t EXT_shader_image_load_store;
   uint32_t EXT_shader_integer_mix;
   uint32_t EXT_shader_samples_identical;
   uint32_t EXT_sRGB;
   uint32_t EXT_stencil_two_side;
   uint32_t EXT_texture_array;
   uint32_t EXT_texture_buffer_object;
   uint32_t EXT_texture_compression_latc;
   uint32_t EXT_texture_compression_s3tc;
   uint32_t EXT_texture_compression_s3tc_srgb;
   uint32_t EXT_texture_env_dot3;
   uint32_t EXT_texture_filter_anisotropic;
   uint32_t EXT_texture_filter_minmax;
   uint32_t EXT_texture_integer;
   uint32_t EXT_texture_mirror_clamp;
   uint32_t EXT_texture_norm16;
   uint32_t EXT_texture_shadow_lod;
   uint32_t EXT_texture_shared_exponent;
   uint32_t EXT_texture_snorm;
   uint32_t EXT_texture_sRGB;
   uint32_t EXT_texture_sRGB_R8;
   uint32_t EXT_texture_sRGB_RG8;
   uint32_t EXT_texture_sRGB_decode;
   uint32_t EXT_texture_swizzle;
   uint32_t EXT_texture_type_2_10_10_10_REV;
   uint32_t EXT_transform_feedback;
   uint32_t EXT_timer_query;
   uint32_t EXT_vertex_array_bgra;
   uint32_t EXT_window_rectangles;
   uint32_t OES_copy_image;
   uint32_t OES_primitive_bounding_box;
   uint32_t OES_sample_variables;
   uint32_t OES_standard_derivatives;
   uint32_t OES_texture_buffer;
   uint32_t OES_texture_cube_map_array;
   uint32_t OES_texture_view;
   uint32_t OES_viewport_array;
   /* vendor extensions */
   uint32_t AMD_compressed_ATC_texture;
   uint32_t AMD_framebuffer_multisample_advanced;
   uint32_t AMD_depth_clamp_separate;
   uint32_t AMD_performance_monitor;
   uint32_t AMD_pinned_memory;
   uint32_t AMD_seamless_cubemap_per_texture;
   uint32_t AMD_vertex_shader_layer;
   uint32_t AMD_vertex_shader_viewport_index;
   uint32_t ANDROID_extension_pack_es31a;
   uint32_t ARM_shader_framebuffer_fetch_depth_stencil;
   uint32_t ATI_meminfo;
   uint32_t ATI_texture_compression_3dc;
   uint32_t ATI_texture_mirror_once;
   uint32_t ATI_texture_env_combine3;
   uint32_t ATI_fragment_shader;
   uint32_t GREMEDY_string_marker;
   uint32_t INTEL_blackhole_render;
   uint32_t INTEL_conservative_rasterization;
   uint32_t INTEL_performance_query;
   uint32_t INTEL_shader_atomic_float_minmax;
   uint32_t INTEL_shader_integer_functions2;
   uint32_t KHR_blend_equation_advanced;
   uint32_t KHR_blend_equation_advanced_coherent;
   uint32_t KHR_robustness;
   uint32_t KHR_texture_compression_astc_hdr;
   uint32_t KHR_texture_compression_astc_ldr;
   uint32_t KHR_texture_compression_astc_sliced_3d;
   uint32_t MESA_framebuffer_flip_y;
   uint32_t MESA_texture_const_bandwidth;
   uint32_t MESA_pack_invert;
   uint32_t MESA_tile_raster_order;
   uint32_t EXT_shader_framebuffer_fetch;
   uint32_t EXT_shader_framebuffer_fetch_non_coherent;
   uint32_t MESA_shader_integer_functions;
   uint32_t MESA_window_pos;
   uint32_t MESA_ycbcr_texture;
   uint32_t NV_alpha_to_coverage_dither_control;
   uint32_t NV_compute_shader_derivatives;
   uint32_t NV_conditional_render;
   uint32_t NV_copy_depth_to_color;
   uint32_t NV_copy_image;
   uint32_t NV_fill_rectangle;
   uint32_t NV_fog_distance;
   uint32_t NV_primitive_restart;
   uint32_t NV_shader_atomic_float;
   uint32_t NV_shader_atomic_int64;
   uint32_t NV_texture_barrier;
   uint32_t NV_texture_env_combine4;
   uint32_t NV_texture_rectangle;
   uint32_t NV_vdpau_interop;
   uint32_t NV_conservative_raster;
   uint32_t NV_conservative_raster_dilate;
   uint32_t NV_conservative_raster_pre_snap_triangles;
   uint32_t NV_conservative_raster_pre_snap;
   uint32_t NV_viewport_array2;
   uint32_t NV_viewport_swizzle;
   uint32_t NVX_gpu_memory_info;
   uint32_t TDFX_texture_compression_FXT1;
   uint32_t OES_EGL_image;
   uint32_t OES_draw_texture;
   uint32_t OES_depth_texture_cube_map;
   uint32_t OES_EGL_image_external;
   uint32_t OES_texture_3D;
   uint32_t OES_texture_float;
   uint32_t OES_texture_float_linear;
   uint32_t OES_texture_half_float;
   uint32_t OES_texture_half_float_linear;
   uint32_t OES_compressed_ETC1_RGB8_texture;
   uint32_t OES_geometry_shader;
   uint32_t OES_texture_compression_astc;
   uint32_t extension_sentinel;
   /** The extension string */
   const uint8_t *String;
   /** Number of supported extensions */
   uint32_t Count;
   /**
    * The context version which extension helper functions compare against.
    * By default, the value is equal to ctx->Version. This changes to ~0
    * while meta is in progress.
    */
   uint8_t Version;
};

/**
 * Enum for the OpenGL APIs we know about and may support.
 *
 * NOTE: This must match the api_enum table in
 * src/mesa/main/get_hash_generator.py
 */
typedef enum
{
   API_OPENGL_COMPAT,      /* legacy / compatibility contexts */
   API_OPENGLES,
   API_OPENGLES2,
   API_OPENGL_CORE,
   API_OPENGL_LAST = API_OPENGL_CORE
} gl_api;

/**
 * \brief An element of the \c extension_table.
 */
struct mesa_extension {
   /** Name of extension, such as "GL_ARB_depth_clamp". */
   const char *name;

   /** Offset (in bytes) of the corresponding member in struct gl_extensions. */
   size_t offset;

   /** Minimum version the extension requires for the given API
    * (see gl_api defined in mtypes.h). The value is equal to:
    * 10 * major_version + minor_version
    */
   uint8_t version[API_OPENGL_LAST + 1];

   /** Year the extension was proposed or approved.  Used to sort the 
    * extension string chronologically. */
   uint16_t year;
};

extern const struct mesa_extension _mesa_extension_table[];


/* Generate enums for the functions below */
enum {
#define EXT(name_str, ...) MESA_EXTENSION_##name_str,
#include "extensions_table.h"
#undef EXT
MESA_EXTENSION_COUNT
};


