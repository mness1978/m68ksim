    
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "cpu.h"
#include <stdint.h> // Include for uint16_t

// Forward declare CPU to avoid circular dependency if needed later
// struct CPU; // Already included via cpu.h

// Define a function pointer type for our instruction handlers
typedef void (*InstructionHandler)(CPU* cpu, uint16_t opcode);

// Structure to map an opcode pattern to a handler function
typedef struct {
    uint16_t mask;    // Bitmask to apply to the opcode
    uint16_t value;   // Value to compare against after masking
    InstructionHandler handler; // Function to handle this opcode
} OpcodeMapping;

void execute_program(CPU* cpu);

#endif // EXECUTOR_H

  
