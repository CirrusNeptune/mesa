#include <stdio.h>
#include "vc4/vc4_context.h"
#include "main/consts_exts.h"
#include "main/shader_types.h"
#include "standalone.h"
#include "state_tracker/st_glsl_to_ir.h"
#include "state_tracker/st_program.h"

struct st_config_options;
#include "state_tracker/st_extensions.h"

static int
vc4_simulator_get_param_ioctl(int fd, struct drm_vc4_get_param *args)
{
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
                    (long long)args->param);
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
                                     struct drm_vc4_create_shader_bo *args)
{
    if (shader_table_capacity == 0) {
        shader_table_capacity = 32;
        shader_table = realloc(shader_table, shader_table_capacity * sizeof(struct shader_bo));
    } else if (shader_table_num == shader_table_capacity) {
        shader_table_capacity *= 2;
        shader_table = realloc(shader_table, shader_table_capacity * sizeof(struct shader_bo));
    }

    struct shader_bo *bo = &shader_table[shader_table_num];
    bo->size = args->size;
    bo->data = malloc(args->size);
    memcpy(bo->data, (void *)(uintptr_t)args->data, args->size);

    args->handle = shader_table_num++;

    return 0;
}

int drmIoctl(int fd, unsigned long request, void *arg)
{
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
            fprintf(stderr, "Unknown ioctl 0x%08x\n", (int)request);
            abort();
    }
}

int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
    assert(0);
    return 1;
}

int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
    assert(0);
    return 1;
}

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
    assert(0);
    return 1;
}

int drmSyncobjDestroy(int fd, uint32_t handle)
{
    assert(0);
    return 1;
}

int drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
    switch (capability)
    {
        case DRM_CAP_SYNCOBJ:
            value = 0;
            return 0;
        default:
            assert(0);
            return 1;
    }
}

int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd)
{
    assert(0);
    return 1;
}

int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
    assert(0);
    return 1;
}

struct renderonly_scanout *
renderonly_create_gpu_import_for_resource(struct pipe_resource *rsc,
                                          struct renderonly *ro,
                                          struct winsys_handle *out_handle)
{
    assert(0);
    return NULL;
}

void
renderonly_scanout_destroy(struct renderonly_scanout *scanout,
                           struct renderonly *ro)
{
    assert(0);
}

int main(int argc, char** argv) {
    struct vc4_context *vc4;

    vc4 = rzalloc(NULL, struct vc4_context);
    if (!vc4)
        return 1;
    struct vc4_screen *screen = (struct vc4_screen *)vc4_screen_create(-1, NULL, NULL);
    vc4->screen = screen;

    struct pipe_context *pctx = &vc4->base;

    vc4_state_init(pctx);
    vc4_program_init(pctx);
    vc4_job_init(vc4);

    static struct gl_context local_ctx = {0};
    struct standalone_options standalone_opts = {
            .glsl_version = 430,
            .do_link = 1,
    };
    char* files[] = {"/Users/cirrus/Desktop/test.vert",
                     "/Users/cirrus/Desktop/test.frag"};
    struct gl_shader_program *shader_program = standalone_compile_shader(&standalone_opts, 2, files, &local_ctx);

    struct gl_extensions extensions = {0};
    st_init_limits(&screen->base, &local_ctx.Const, &extensions, API_OPENGL_CORE);

    static struct st_context local_st_ctx = {0};
    local_st_ctx.pipe = pctx;
    local_st_ctx.screen = &screen->base;
    local_st_ctx.ctx = &local_ctx;
    local_ctx.st = &local_st_ctx;
    struct gl_pipeline_object pipeline_object = {
            .Flags = GLSL_DUMP
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
        struct vc4_rasterizer_state* ras_state_obj = pctx->create_rasterizer_state(pctx, &ras_state);
        pctx->bind_rasterizer_state(pctx, ras_state_obj);
    }

    {
        struct pipe_blend_state blend_state = {0};
        struct pipe_blend_state* blend_state_obj = pctx->create_blend_state(pctx, &blend_state);
        pctx->bind_blend_state(pctx, blend_state_obj);
    }

    {
        struct pipe_depth_stencil_alpha_state zsa_state = {0};
        struct vc4_depth_stencil_alpha_state* zsa_state_obj = pctx->create_depth_stencil_alpha_state(pctx, &zsa_state);
        pctx->bind_depth_stencil_alpha_state(pctx, zsa_state_obj);
    }

    {
        struct pipe_vertex_element elements[1] = {
                {
                    .src_offset = 0,
                    .vertex_buffer_index = 0,
                    .src_format = PIPE_FORMAT_R32G32B32A32_FLOAT,
                    .instance_divisor = 0,
                }
        };
        struct vc4_vertex_stateobj *vtx_state = pctx->create_vertex_elements_state(pctx, 1, elements);
        pctx->bind_vertex_elements_state(pctx, vtx_state);
    }

    vc4_get_job_for_fbo(vc4);
    vc4_update_compiled_shaders(vc4, PIPE_PRIM_TRIANGLES);

    return 0;
}
