#ifndef OPERAND_PARSER_TYPES_H
#define OPERAND_PARSER_TYPES_H

#include <stddef.h>

typedef struct {
    const char *input;
    size_t position;
} pcc_string_input_t;

#endif // OPERAND_PARSER_TYPES_H
