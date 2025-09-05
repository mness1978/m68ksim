#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <stdint.h>

// A structure to map a memory address back to the source file
typedef struct {
    uint32_t address;
    int line_number;
    char* instruction_text;
} SourceMapping;

void disassembler_add_mapping(uint32_t address, int line_number, const char* text);
SourceMapping* disassembler_get_mapping(uint32_t address);
void disassembler_cleanup();

#endif // DISASSEMBLER_H
