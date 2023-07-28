#include "ir.h"
#include "ir_vertex_attribute_visitor.h"
#include "glsl_parser_extras.h"
#include "main/macros.h"
#include "util/hash_table.h"

class ir_vertex_attributes_visitor : public ir_hierarchical_visitor {
public:
    ir_vertex_attributes_visitor(struct pipe_vertex_element elements[16]);

    virtual ir_visitor_status visit(class ir_variable *);

    const struct glsl_type *types[16] = {};

private:

    struct pipe_vertex_element *elements;
};

unsigned
extract_vertex_attributes_from_ir(struct pipe_vertex_element elements[16],
                                  unsigned vertex_element_sizes[16],
                                  struct exec_list *instructions) {
   ir_vertex_attributes_visitor v(elements);
   v.run(instructions);

   unsigned num_elements = 0;
   uint16_t offset = 0;
   for (int i = 0; i < 16; ++i) {
      if (const struct glsl_type *type = v.types[i]) {
         struct pipe_vertex_element *element = &elements[i];
         element->src_offset = offset;
         vertex_element_sizes[i] = type->vector_elements * 4;
         offset += vertex_element_sizes[i];
         num_elements = i + 1;
      }
   }

   return num_elements;
}

ir_vertex_attributes_visitor::ir_vertex_attributes_visitor(
   struct pipe_vertex_element elements[16])
   : elements(elements) {
}

ir_visitor_status
ir_vertex_attributes_visitor::visit(ir_variable *ir) {
   if (ir->data.mode == ir_var_shader_in) {
      unsigned int location =
         (unsigned int) ir->data.location - VERT_ATTRIB_GENERIC0;
      assert(location < 16u);
      assert(ir->type->is_32bit());
      assert(!ir->type->is_matrix());
      assert(ir->type->vector_elements != 0);

      enum pipe_format format = PIPE_FORMAT_NONE;
      if (ir->type->is_float()) {
         format = (enum pipe_format) ((int) PIPE_FORMAT_R32_FLOAT +
                                      ir->type->vector_elements - 1);
      } else if (ir->type->base_type == GLSL_TYPE_INT) {
         format = (enum pipe_format) ((int) PIPE_FORMAT_R32_SINT +
                                      ir->type->vector_elements - 1);
      } else if (ir->type->base_type == GLSL_TYPE_UINT) {
         format = (enum pipe_format) ((int) PIPE_FORMAT_R32_UINT +
                                      ir->type->vector_elements - 1);
      } else {
         assert(0);
      }

      struct pipe_vertex_element *element = &elements[location];
      element->src_offset = 0;
      element->vertex_buffer_index = 0;
      element->dual_slot = false;
      element->src_format = format;
      element->instance_divisor = 0;

      types[location] = ir->type;
   }

   return visit_continue;
}
