#include <stdio.h>
#include "vc4/vc4_context.h"
#include "main/consts_exts.h"
#include "main/shader_types.h"
#include "standalone.h"
#include "state_tracker/st_glsl_to_ir.h"
#include "state_tracker/st_program.h"
#include "ir_vertex_attribute_visitor.h"
#include "vc4-disasm.h"

struct st_config_options;

#include "state_tracker/st_extensions.h"
#include "main/shared.h"
#include "main/shaderapi.h"

static int
vc4_simulator_get_param_ioctl(int fd, struct drm_vc4_get_param *args) {
   switch (args->param) {
      case DRM_VC4_PARAM_SUPPORTS_BRANCHES:
      case DRM_VC4_PARAM_SUPPORTS_ETC1:
      case DRM_VC4_PARAM_SUPPORTS_THREADED_FS:
      case DRM_VC4_PARAM_SUPPORTS_FIXED_RCL_ORDER:
         args->value = true;
         return 0;

      case DRM_VC4_PARAM_SUPPORTS_MADVISE:
      case DRM_VC4_PARAM_SUPPORTS_PERFMON:
         errno = -EINVAL;
         return -1;

      case DRM_VC4_PARAM_V3D_IDENT0:
         args->value = 0x02000000;
         return 0;

      case DRM_VC4_PARAM_V3D_IDENT1:
         args->value = 0x00000001;
         return 0;

      default:
         fprintf(stderr, "Unknown DRM_IOCTL_VC4_GET_PARAM(%lld)\n",
                 (long long) args->param);
         abort();
   };
}

struct shader_bo {
    __u32 size;
    void *data;
};

static __u32 shader_table_capacity = 0;
static __u32 shader_table_num = 0;
static struct shader_bo *shader_table = NULL;

static int
vc4_simulator_create_shader_bo_ioctl(int fd,
                                     struct drm_vc4_create_shader_bo *args) {
   if (shader_table_capacity == 0) {
      shader_table_capacity = 32;
      shader_table = realloc(shader_table,
                             shader_table_capacity *
                             sizeof(struct shader_bo));
   } else if (shader_table_num == shader_table_capacity) {
      shader_table_capacity *= 2;
      shader_table = realloc(shader_table,
                             shader_table_capacity *
                             sizeof(struct shader_bo));
   }

   struct shader_bo *bo = &shader_table[shader_table_num];
   bo->size = args->size;
   bo->data = malloc(args->size);
   memcpy(bo->data, (void *) (uintptr_t) args->data, args->size);

   args->handle = shader_table_num++;

   return 0;
}

int drmIoctl(int fd, unsigned long request, void *arg) {
   switch (request) {
      case DRM_IOCTL_VC4_GET_PARAM:
         return vc4_simulator_get_param_ioctl(fd, arg);
      case DRM_IOCTL_VC4_GET_TILING:
      case DRM_IOCTL_VC4_SET_TILING:
         /* Disable these for now, since the sharing with i965 requires
          * linear buffers.
          */
         errno = -EINVAL;
         return -1;
      case DRM_IOCTL_VC4_CREATE_SHADER_BO:
         return vc4_simulator_create_shader_bo_ioctl(fd, arg);
      default:
         fprintf(stderr, "Unknown ioctl 0x%08x\n", (int) request);
         abort();
   }
}

int
drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd) {
   assert(0);
   return 1;
}

int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) {
   assert(0);
   return 1;
}

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle) {
   assert(0);
   return 1;
}

int drmSyncobjDestroy(int fd, uint32_t handle) {
   assert(0);
   return 1;
}

int drmGetCap(int fd, uint64_t capability, uint64_t *value) {
   switch (capability) {
      case DRM_CAP_SYNCOBJ:
         value = 0;
         return 0;
      default:
         assert(0);
         return 1;
   }
}

int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd) {
   assert(0);
   return 1;
}

int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd) {
   assert(0);
   return 1;
}

struct renderonly_scanout *
renderonly_create_gpu_import_for_resource(struct pipe_resource *rsc,
                                          struct renderonly *ro,
                                          struct winsys_handle *out_handle) {
   assert(0);
   return NULL;
}

void
renderonly_scanout_destroy(struct renderonly_scanout *scanout,
                           struct renderonly *ro) {
   assert(0);
}

static void output_uniform_parameter(FILE *out,
                                     const struct gl_program_parameter_list *parameters,
                                     uint32_t index) {
   unsigned last_uniform_storage_index = 0xffffffff;
   unsigned base_p = 0xffffffff;
   for (unsigned p = 0; p < parameters->NumParameters; ++p) {
      const struct gl_program_parameter *parameter = &parameters->Parameters[p];
      if (parameter->UniformStorageIndex != last_uniform_storage_index) {
         last_uniform_storage_index = parameter->UniformStorageIndex;
         base_p = p;
      }
      if (parameter->ValueOffset <= index &&
          index < parameter->ValueOffset + parameter->Size) {
         switch (parameter->Type) {
            case PROGRAM_STATE_VAR:
               return;
            case PROGRAM_UNIFORM:
               switch (parameter->DataType) {
                  case GL_FLOAT_MAT4:
                  case GL_FLOAT_MAT3:
                  case GL_FLOAT_MAT2:
                     fprintf(out,
                             "            ShaderUniform::Constant(qpu::transmute_f32(%s.col(%u)[%u])),\n",
                             parameter->Name, p - base_p,
                             index - parameter->ValueOffset);
                     return;
                  case GL_FLOAT_VEC4:
                  case GL_FLOAT_VEC3:
                  case GL_FLOAT_VEC2:
                     fprintf(out,
                             "            ShaderUniform::Constant(qpu::transmute_f32(%s[%u])),\n",
                             parameter->Name,
                             index - parameter->ValueOffset);
                     return;
                  case GL_FLOAT:
                     fprintf(out,
                             "            ShaderUniform::Constant(qpu::transmute_f32(%s)),\n",
                             parameter->Name);
                     return;
                  case GL_INT_VEC4:
                  case GL_INT_VEC3:
                  case GL_INT_VEC2:
                  case GL_UNSIGNED_INT_VEC4:
                  case GL_UNSIGNED_INT_VEC3:
                  case GL_UNSIGNED_INT_VEC2:
                     fprintf(out,
                             "            ShaderUniform::Constant(%s[%u]),\n",
                             parameter->Name,
                             index - parameter->ValueOffset);
                     return;
                  case GL_INT:
                  case GL_UNSIGNED_INT:
                     fprintf(out,
                             "            ShaderUniform::Constant(%s as _),\n",
                             parameter->Name);
                     return;
                  default:
                     break;
               }
               break;
            default:
               break;
         }
         break;
      }
   }
   assert(0);
}

static void output_uniforms(FILE *out, enum pipe_shader_type shader_type,
                            const struct vc4_shader_uniform_info *uniforms,
                            const struct gl_program_parameter_list *parameters,
                            const struct gl_shader_program_data *prog_data) {
   for (unsigned i = 0; i < uniforms->count; ++i) {
      const enum quniform_contents contents = uniforms->contents[i];
      const uint32_t data = uniforms->data[i];

      static const char *quniform_names[] = {
         [QUNIFORM_VIEWPORT_X_SCALE] = "qpu::transmute_f32(encoder.vp_x_scale())",
         [QUNIFORM_VIEWPORT_Y_SCALE] = "qpu::transmute_f32(encoder.vp_y_scale())",
         [QUNIFORM_VIEWPORT_Z_OFFSET] = "qpu::transmute_f32(encoder.vp_z_offset())",
         [QUNIFORM_VIEWPORT_Z_SCALE] = "qpu::transmute_f32(encoder.vp_z_scale())",
         [QUNIFORM_TEXTURE_CONFIG_P0] = "tex_p0",
         [QUNIFORM_TEXTURE_CONFIG_P1] = "tex_p1",
         [QUNIFORM_TEXTURE_CONFIG_P2] = "tex_p2",
         [QUNIFORM_TEXTURE_FIRST_LEVEL] = "tex_first_level",
      };

      switch (contents) {
         case QUNIFORM_CONSTANT:
            fprintf(out, "            ShaderUniform::Constant(0x%08X),\n", data);
            break;
         case QUNIFORM_UNIFORM:
            output_uniform_parameter(out, parameters, data);
            break;
         case QUNIFORM_TEXTURE_CONFIG_P0:
            for (unsigned r = 0; r < prog_data->NumProgramResourceList; ++r) {
               const struct gl_program_resource *ProgramResourceList = &prog_data->ProgramResourceList[r];
               if (ProgramResourceList->Type == GL_UNIFORM) {
                  const struct gl_uniform_storage *uni_storage = ProgramResourceList->Data;
                  if (glsl_get_base_type(uni_storage->type) == GLSL_TYPE_SAMPLER &&
                     uni_storage->opaque[shader_type].active &&
                     uni_storage->opaque[shader_type].index == data) {
                     fprintf(out,
                             "            ShaderUniform::Texture(%s),\n", uni_storage->name.string);
                     break;
                  }
               }
            }
            break;
         case QUNIFORM_TEXTURE_CONFIG_P1:
         case QUNIFORM_TEXTURE_CONFIG_P2:
         case QUNIFORM_TEXTURE_FIRST_LEVEL:
            break;
         default:
            if (contents < ARRAY_SIZE(quniform_names) &&
                quniform_names[contents]) {
               fprintf(out, "            ShaderUniform::Constant(%s),\n",
                       quniform_names[contents]);
            } else {
               fprintf(out, "            ShaderUniform::Constant(??? %d),\n",
                       contents);
            }
            break;
      }
   }
}

static void output_uniform_args(FILE *out,
                                const struct gl_program_parameter_list *parameters_tup[2],
                                const struct gl_shader_program_data *prog_data) {
   uint32_t visited_uniforms = 0;
   for (unsigned i = 0; i < 2; ++i) {
      const struct gl_program_parameter_list *parameters = parameters_tup[i];
      for (unsigned p = 0; p < parameters->NumParameters; ++p) {
         const struct gl_program_parameter *parameter = &parameters->Parameters[p];
         if (parameter->Type != PROGRAM_UNIFORM)
            continue;
         const uint32_t this_uniform_mask = 1 << parameter->UniformStorageIndex;
         if (!(visited_uniforms & this_uniform_mask)) {
            visited_uniforms |= this_uniform_mask;
            switch (parameter->DataType) {
               case GL_FLOAT_MAT4:
                  fprintf(out, ", %s: &glam::Mat4", parameter->Name);
                  break;
               case GL_FLOAT_MAT3:
                  fprintf(out, ", %s: &glam::Mat3", parameter->Name);
                  break;
               case GL_FLOAT_MAT2:
                  fprintf(out, ", %s: &glam::Mat2", parameter->Name);
                  break;
               case GL_FLOAT_VEC4:
                  fprintf(out, ", %s: &glam::Vec4", parameter->Name);
                  break;
               case GL_FLOAT_VEC3:
                  fprintf(out, ", %s: &glam::Vec3", parameter->Name);
                  break;
               case GL_FLOAT_VEC2:
                  fprintf(out, ", %s: &glam::Vec2", parameter->Name);
                  break;
               case GL_FLOAT:
                  fprintf(out, ", %s: f32", parameter->Name);
                  break;
               case GL_INT_VEC4:
                  fprintf(out, ", %s: &glam::IVec4", parameter->Name);
                  break;
               case GL_INT_VEC3:
                  fprintf(out, ", %s: &glam::IVec3", parameter->Name);
                  break;
               case GL_INT_VEC2:
                  fprintf(out, ", %s: &glam::IVec2", parameter->Name);
                  break;
               case GL_INT:
                  fprintf(out, ", %s: i32", parameter->Name);
                  break;
               case GL_UNSIGNED_INT_VEC4:
                  fprintf(out, ", %s: &glam::UVec4", parameter->Name);
                  break;
               case GL_UNSIGNED_INT_VEC3:
                  fprintf(out, ", %s: &glam::UVec3", parameter->Name);
                  break;
               case GL_UNSIGNED_INT_VEC2:
                  fprintf(out, ", %s: &glam::UVec2", parameter->Name);
                  break;
               case GL_UNSIGNED_INT:
                  fprintf(out, ", %s: u32", parameter->Name);
                  break;
               default:
                  break;
            }
         }
      }
   }

   for (unsigned p = 0; p < prog_data->NumProgramResourceList; ++p) {
      const struct gl_program_resource *ProgramResourceList = &prog_data->ProgramResourceList[p];
      if (ProgramResourceList->Type == GL_UNIFORM) {
         const struct gl_uniform_storage *uni_storage = ProgramResourceList->Data;
         if (glsl_get_base_type(uni_storage->type) == GLSL_TYPE_SAMPLER) {
            fprintf(out, ", %s: &TextureUniform", uni_storage->name.string);
         }
      }
   }
}

static void output_compiled_shader(FILE *out, const char *name,
                                   const struct vc4_compiled_shader *cshader) {
   assert(cshader->bo->handle < shader_table_num);
   const struct shader_bo *shader = &shader_table[cshader->bo->handle];
   fprintf(out, "const %s_ASM_CODE: [u64; %u] = qpu! {\n", name,
           (unsigned) shader->size / 8);
   vc4_glsl_qpu_disasm(out, shader->data, (int) shader->size / 8);
   fprintf(out,
           "};\npub static %s_ASM: ShaderNode = ShaderNode::new(&%s_ASM_CODE);\n\n",
           name, name);
}

int main(int argc, char **argv) {
   if (argc < 4) {
      fprintf(stderr, "Usage: %s <file>.vert <file>.frag <output>.rs\n", argv[0]);
      return 1;
   }

   char* vert_path = argv[1];
   char* frag_path = argv[2];
   char* rs_path = argv[3];

   struct vc4_context *vc4;

   vc4 = rzalloc(NULL, struct vc4_context);
   if (!vc4)
      return 1;
   struct vc4_screen *screen = (struct vc4_screen *) vc4_screen_create(-1,
                                                                       NULL,
                                                                       NULL);
   vc4->screen = screen;

   struct pipe_context *pctx = &vc4->base;

   vc4_state_init(pctx);
   vc4_program_init(pctx);
   vc4_job_init(vc4);

   static struct gl_shared_state shared = {0};
   static struct gl_context local_ctx = {.Shared = &shared};
   _mesa_init_shader_includes(&shared);
   struct standalone_options standalone_opts = {
      .glsl_version = 430,
      .do_link = 1,
      //.dump_ast = 1,
      //.dump_builder = 1,
   };
   char *files[] = {vert_path, frag_path};
   struct gl_shader_program *shader_program = standalone_compile_shader(
      &standalone_opts, 2, files, &local_ctx);

   for (unsigned i = 0; i < shader_program->NumShaders; ++i) {
      struct gl_shader *shader = shader_program->Shaders[i];
      if (!shader->CompileStatus) {
         fwrite(shader->InfoLog, 1, strlen(shader->InfoLog), stderr);
      }
   }

   if (!shader_program->data->LinkStatus) {
      if (strlen(shader_program->data->InfoLog))
         fprintf(stderr, "Unable to link shaders: %s\n", shader_program->data->InfoLog);
      return 1;
   }

   struct pipe_vertex_element vertex_elements[16] = {0};
   unsigned vertex_element_sizes[16] = {0};
   unsigned num_vertex_elements = extract_vertex_attributes_from_ir(
      vertex_elements, vertex_element_sizes,
      shader_program->_LinkedShaders[MESA_SHADER_VERTEX]->ir);

   struct gl_extensions extensions = {0};
   st_init_limits(&screen->base, &local_ctx.Const, &extensions,
                  API_OPENGL_CORE);

   static struct st_context local_st_ctx = {0};
   local_st_ctx.pipe = pctx;
   local_st_ctx.screen = &screen->base;
   local_st_ctx.ctx = &local_ctx;
   local_ctx.st = &local_st_ctx;
   struct gl_pipeline_object pipeline_object = {
      .Flags = 0
   };
   local_ctx._Shader = &pipeline_object;

   st_link_shader(&local_ctx, shader_program);

   {
      struct st_variant *variant = &shader_program->_LinkedShaders[MESA_SHADER_VERTEX]->Program->variants[0];
      struct vc4_uncompiled_shader *shader = variant->driver_shader;
      pctx->bind_vs_state(pctx, shader);
   }

   {
      struct st_variant *variant = &shader_program->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program->variants[0];
      struct vc4_uncompiled_shader *shader = variant->driver_shader;
      pctx->bind_fs_state(pctx, shader);
   }

   {
      struct pipe_rasterizer_state ras_state = {
         .clip_plane_enable = 0,
      };
      struct vc4_rasterizer_state *ras_state_obj = pctx->create_rasterizer_state(
         pctx, &ras_state);
      pctx->bind_rasterizer_state(pctx, ras_state_obj);
   }

   {
      struct pipe_blend_state blend_state = {0};
      blend_state.rt[0].blend_enable = 1;
      blend_state.rt[0].rgb_func = PIPE_BLEND_ADD;
      blend_state.rt[0].rgb_src_factor = PIPE_BLENDFACTOR_ONE;
      blend_state.rt[0].rgb_dst_factor = PIPE_BLENDFACTOR_ONE;
      blend_state.rt[0].alpha_func = PIPE_BLEND_ADD;
      blend_state.rt[0].alpha_src_factor = PIPE_BLENDFACTOR_ONE;
      blend_state.rt[0].alpha_dst_factor = PIPE_BLENDFACTOR_ONE;
      blend_state.rt[0].colormask = PIPE_MASK_RGBA;
      struct pipe_blend_state *blend_state_obj = pctx->create_blend_state(
         pctx,
         &blend_state);
      pctx->bind_blend_state(pctx, blend_state_obj);
   }

   {
      struct pipe_depth_stencil_alpha_state zsa_state = {0};
      struct vc4_depth_stencil_alpha_state *zsa_state_obj = pctx->create_depth_stencil_alpha_state(
         pctx, &zsa_state);
      pctx->bind_depth_stencil_alpha_state(pctx, zsa_state_obj);
   }

   {
      struct vc4_vertex_stateobj *vtx_state = pctx->create_vertex_elements_state(
         pctx, num_vertex_elements, vertex_elements);
      pctx->bind_vertex_elements_state(pctx, vtx_state);
   }

   struct pipe_resource cbuf0_res = {0};
   cbuf0_res.format = PIPE_FORMAT_B8G8R8A8_UNORM;
   struct pipe_surface cbuf0 = {0};
   cbuf0.reference.count = 1;
   cbuf0.texture = &cbuf0_res;
   cbuf0.format = cbuf0_res.format;
   vc4->framebuffer.cbufs[0] = &cbuf0;

   if ((shader_program->_LinkedShaders[MESA_SHADER_VERTEX]->Program->info.outputs_written & VARYING_BIT_POS) == 0) {
      fprintf(stderr, "%s does not write to gl_Position\n", files[0]);
      return 1;
   }

   vc4_get_job_for_fbo(vc4);
   vc4_update_compiled_shaders(vc4, PIPE_PRIM_TRIANGLES);

   FILE *fout = fopen(rs_path, "w");

   fprintf(fout, "#![allow(unused_imports, nonstandard_style)]\n"
                 "use super::ShaderNode;\n"
                 "use rpi_drm::{Buffer, CommandEncoder, ShaderAttribute, ShaderUniform, TextureUniform};\n"
                 "use vc4_drm::cl::AttributeRecord;\n"
                 "use vc4_drm::{glam, qpu};\n\n");

   output_compiled_shader(fout, "CS", vc4->prog.cs);
   output_compiled_shader(fout, "VS", vc4->prog.vs);
   output_compiled_shader(fout, "FS", vc4->prog.fs);

   fprintf(fout, "pub fn bind(encoder: &mut CommandEncoder");
   if (num_vertex_elements)
      fprintf(fout, ", cs_vbo: &Buffer, vs_vbo: &Buffer");
   const struct gl_program_parameter_list *parameters_tup[2] = {
      shader_program->_LinkedShaders[MESA_SHADER_VERTEX]->Program->Parameters,
      shader_program->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program->Parameters
   };
   output_uniform_args(fout, parameters_tup, shader_program->data);
   fprintf(fout, ") {\n"
                 "    encoder.bind_shader(\n"
                 "        %s,\n"
                 "        %d,\n"
                 "        *FS_ASM.handle.get().unwrap(),\n"
                 "        *VS_ASM.handle.get().unwrap(),\n"
                 "        *CS_ASM.handle.get().unwrap(),\n"
                 "        &[\n",
                 vc4->prog.fs->fs_threaded ? "false" : "true",
           vc4->prog.fs->num_inputs);

   if (num_vertex_elements) {
      uint64_t inputs_read = shader_program->_LinkedShaders[MESA_SHADER_VERTEX]->Program->info.inputs_read;
      inputs_read &= 0x7FFF8000;

      unsigned vs_vpm_stride = 0, cs_vpm_stride = 0;
      {
         const struct pipe_vertex_element *element = &vertex_elements[
            num_vertex_elements - 1];
         const unsigned element_size = vertex_element_sizes[
            num_vertex_elements -
            1];
         vs_vpm_stride = element->src_offset + element_size;
      }
      unsigned i = 0;
      u_foreach_bit64 (b, inputs_read) {
         const uint64_t location = b - VERT_ATTRIB_GENERIC0;
         assert(location < num_vertex_elements);
         const struct pipe_vertex_element *element = &vertex_elements[location];
         const unsigned element_size = vertex_element_sizes[location];
         if (i < vc4->prog.cs->vattrs_live)
            cs_vpm_stride = element->src_offset + element_size;
         ++i;
      }
      i = 0;
      u_foreach_bit64 (b, inputs_read) {
         const uint64_t location = b - VERT_ATTRIB_GENERIC0;
         assert(location < num_vertex_elements);
         const struct pipe_vertex_element *element = &vertex_elements[location];
         const unsigned element_size = vertex_element_sizes[location];
         if (i < vc4->prog.cs->vattrs_live) {
            fprintf(fout, "            ShaderAttribute {\n"
                          "                buffer: cs_vbo,\n"
                          "                record: AttributeRecord {\n"
                          "                    address: %u,\n"
                          "                    number_of_bytes_minus_1: %u,\n"
                          "                    stride: %u,\n"
                          "                    vertex_shader_vpm_offset: 0,\n"
                          "                    coordinate_shader_vpm_offset: %u,\n"
                          "                },\n"
                          "                vs: false,\n"
                          "                cs: true,\n"
                          "            },\n", element->src_offset,
                    element_size - 1,
                    cs_vpm_stride, vc4->prog.cs->vattr_offsets[i]);
         }
         if (i < vc4->prog.vs->vattrs_live) {
            fprintf(fout, "            ShaderAttribute {\n"
                          "                buffer: vs_vbo,\n"
                          "                record: AttributeRecord {\n"
                          "                    address: %u,\n"
                          "                    number_of_bytes_minus_1: %u,\n"
                          "                    stride: %u,\n"
                          "                    vertex_shader_vpm_offset: %u,\n"
                          "                    coordinate_shader_vpm_offset: 0,\n"
                          "                },\n"
                          "                vs: true,\n"
                          "                cs: false,\n"
                          "            },\n", element->src_offset,
                    element_size - 1,
                    vs_vpm_stride, vc4->prog.vs->vattr_offsets[i]);
         }
         ++i;
      }
   }

   fprintf(fout, "        ],\n"
                 "        &[\n");
   output_uniforms(fout, MESA_SHADER_FRAGMENT, &vc4->prog.fs->uniforms,
                   shader_program->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program->Parameters,
                   shader_program->data);
   fprintf(fout, "        ],\n"
                 "        &[\n");
   output_uniforms(fout, MESA_SHADER_VERTEX, &vc4->prog.vs->uniforms,
                   shader_program->_LinkedShaders[MESA_SHADER_VERTEX]->Program->Parameters,
                   shader_program->data);
   fprintf(fout, "        ],\n"
                 "        &[\n");
   output_uniforms(fout, MESA_SHADER_VERTEX, &vc4->prog.cs->uniforms,
                   shader_program->_LinkedShaders[MESA_SHADER_VERTEX]->Program->Parameters,
                   shader_program->data);
   fprintf(fout, "        ],\n"
                 "    );\n"
                 "}\n");

   fclose(fout);
   return 0;
}
