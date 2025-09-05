#include "cpu.h"
#include <stdio.h>
#include <string.h>

void cpu_init(CPU* cpu) {
    memset(cpu, 0, sizeof(CPU));
}

void cpu_pulse_reset(CPU* cpu) {
    // Based on 68000 datasheet, a reset does the following:
    // 1. Sets Supervisor state, disables tracing.
    // 2. Sets interrupt mask to level 7.
    // 3. Loads SSP from 0x000000.
    // 4. Loads PC from 0x000004.
    // For our simulator, we'll simplify this for now.
    cpu_init(cpu);
    cpu->sr = (1 << SR_S) | (7 << SR_I0); // Supervisor mode, interrupt level 7
}

void cpu_dump_registers(CPU* cpu) {
    // Line 1: PC and Data Registers
    printf("PC: %08X | ", cpu->pc);
    for (int i = 0; i < NUM_DATA_REGISTERS; ++i) {
        printf("D%d: %08X ", i, cpu->d[i]);
    }
    printf("\n");

    // Line 2: SR and Address Registers, aligned with the line above
    printf("                             "); // Matches the 29-char width of instruction column
    printf("SR: %04X     | ", cpu->sr);      // Padded to align with PC column
    for (int i = 0; i < NUM_ADDRESS_REGISTERS; ++i) {
        printf("A%d: %08X ", i, cpu->a[i]);
    }
    printf("\n");
}
