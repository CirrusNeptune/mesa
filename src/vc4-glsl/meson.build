vc4_glsl = executable(
  'vc4-glsl',
  ['vc4-glsl.c', 'vc4-standalone.cpp', 'vc4-standalone_scaffolding.cpp',
  '../compiler/glsl/ir_builder_print_visitor.cpp',
  ir_expression_operation_h],
  dependencies : [dep_libdrm],
  include_directories : [
    inc_util, inc_gallium, inc_gallium_drivers, inc_gallium_aux,
    inc_nir, inc_broadcom, inc_glsl, inc_mesa, inc_mapi
  ],
  link_with : [
    libgallium,
    libmesa,
    libglapi_static,
    libvc4,
    libbroadcom_v3d,
    libbroadcom_cle
  ],
  c_args : [no_override_init_args, c_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : true,
)
