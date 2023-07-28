#ifndef VC4_DISASM_H
#define VC4_DISASM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void
vc4_glsl_qpu_disasm(FILE *out, const uint64_t *instructions, int num_instructions);

#ifdef __cplusplus
}
#endif

#endif /* VC4_DISASM_H */
