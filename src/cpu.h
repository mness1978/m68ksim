#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#define NUM_DATA_REGISTERS 8
#define NUM_ADDRESS_REGISTERS 8

// Status Register bits
#define SR_T1 15 // Trace mode
#define SR_S  13 // Supervisor/User state
#define SR_M  12 // Master/Interrupt state
#define SR_I2 10 // Interrupt mask
#define SR_I1 9
#define SR_I0 8
#define SR_X  4  // Extend
#define SR_N  3  // Negative
#define SR_Z  2  // Zero
#define SR_V  1  // Overflow
#define SR_C  0  // Carry

typedef struct {
    uint32_t d[NUM_DATA_REGISTERS];
    uint32_t a[NUM_ADDRESS_REGISTERS];
    uint32_t pc; // Program Counter
    uint16_t sr; // Status Register
} CPU;

void cpu_init(CPU* cpu);
void cpu_pulse_reset(CPU* cpu); // Simulates a hardware reset
void cpu_dump_registers(CPU* cpu);

#endif // CPU_H
