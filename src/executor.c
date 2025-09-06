#include "executor.h"
#include "memory.h"
#include "disassembler.h"
#include <stdio.h>
#include <stdbool.h>

#define MAX_EXECUTION_CYCLES 5000 // Safety break to prevent infinite loops

// Helper to set/clear status register flags
void set_sr_flag(CPU* cpu, int flag, bool set) {
    if (set) {
        cpu->sr |= (1 << flag);
    } else {
        cpu->sr &= ~(1 << flag);
    }
}

void set_flags(CPU* cpu, uint32_t S, uint32_t D, uint32_t R, int size_code, bool is_sub) {
    uint32_t msb_mask;
    uint32_t size_mask;
    if (size_code == 0) { // Byte
        msb_mask = 0x80;
        size_mask = 0xFF;
    } else if (size_code == 1) { // Word
        msb_mask = 0x8000;
        size_mask = 0xFFFF;
    } else { // Long
        msb_mask = 0x80000000;
        size_mask = 0xFFFFFFFF;
    }

    uint32_t result = R & size_mask;
    set_sr_flag(cpu, SR_Z, result == 0);
    set_sr_flag(cpu, SR_N, (result & msb_mask) != 0);

    bool Sm = (S & msb_mask) != 0;
    bool Dm = (D & msb_mask) != 0;
    bool Rm = (result & msb_mask) != 0;

    if (is_sub) { // Subtraction
        set_sr_flag(cpu, SR_V, (Sm && !Dm && !Rm) || (!Sm && Dm && Rm));
        set_sr_flag(cpu, SR_C, (Sm && !Dm) || (Rm && !Dm) || (Sm && Rm));
    } else { // Addition
        set_sr_flag(cpu, SR_V, (!Sm && !Dm && Rm) || (Sm && Dm && !Rm));
        set_sr_flag(cpu, SR_C, (Sm && Dm) || (!Rm && Dm) || (Sm && !Rm));
    }

    set_sr_flag(cpu, SR_X, (cpu->sr & (1 << SR_C)) != 0);
}

void set_logic_flags(CPU* cpu, uint32_t result, int size_code) {
    set_sr_flag(cpu, SR_C, false);
    set_sr_flag(cpu, SR_V, false);
    if (size_code == 0) { // Byte
        set_sr_flag(cpu, SR_Z, (result & 0xFF) == 0);
        set_sr_flag(cpu, SR_N, (result & 0x80) != 0);
    } else if (size_code == 1) { // Word
        set_sr_flag(cpu, SR_Z, (result & 0xFFFF) == 0);
        set_sr_flag(cpu, SR_N, (result & 0x8000) != 0);
    } else { // Long
        set_sr_flag(cpu, SR_Z, result == 0);
        set_sr_flag(cpu, SR_N, (result & 0x80000000) != 0);
    }
}

void execute_program(CPU* cpu) {
    printf("INFO: Beginning execution from 0x%X.\n\n", cpu->pc);
    bool running = true;
    int cycles = 0;

    // Print initial state before first instruction
    printf("%-26s | ", "Initial State");
    cpu_dump_registers(cpu);

    while (running && cycles < MAX_EXECUTION_CYCLES) {
        uint32_t current_pc = cpu->pc;
        SourceMapping* map = disassembler_get_mapping(current_pc);

        uint16_t opcode = mem_read_word(cpu->pc);
        cpu->pc += 2;

        // Decode and execute
        if ((opcode & 0xC1FF) == 0x003C) { // Correctly detects MOVE.B/W/L #imm, Dn
            int size_field = (opcode >> 12) & 0x3; // 1=Byte, 3=Word, 2=Long
            int reg_num = (opcode >> 9) & 0x7;

            // Flags V and C are always cleared for MOVE
            set_sr_flag(cpu, SR_V, false);
            set_sr_flag(cpu, SR_C, false);

            if (size_field == 1) { // Byte (size bits are 01)
                // Immediate byte is stored in the low byte of the following word
                uint8_t data = mem_read_word(cpu->pc) & 0xFF;
                cpu->pc += 2;
                cpu->d[reg_num] = (cpu->d[reg_num] & 0xFFFFFF00) | data;
                set_sr_flag(cpu, SR_Z, data == 0);
                set_sr_flag(cpu, SR_N, (data & 0x80) != 0);
            } else if (size_field == 3) { // Word (size bits are 11)
                uint16_t data = mem_read_word(cpu->pc);
                cpu->pc += 2;
                cpu->d[reg_num] = (cpu->d[reg_num] & 0xFFFF0000) | data;
                set_sr_flag(cpu, SR_Z, data == 0);
                set_sr_flag(cpu, SR_N, (data & 0x8000) != 0);
            } else if (size_field == 2) { // Long (size bits are 10)
                uint32_t data = mem_read_long(cpu->pc);
                cpu->pc += 4;
                cpu->d[reg_num] = data;
                set_sr_flag(cpu, SR_Z, data == 0);
                set_sr_flag(cpu, SR_N, (data & 0x80000000) != 0);
            }
		} else if (opcode == 0x4E71) { // NOP
            // Do nothing.
        } else if ((opcode & 0xF138) == 0x5100) { // SUBQ #imm,Dn
            int data = (opcode >> 9) & 0x7;
            if (data == 0) data = 8;
            int size_code = (opcode >> 6) & 0x3;
            int reg_num = opcode & 0x7;

            uint32_t reg_val = cpu->d[reg_num];
            uint32_t result;

            if (size_code == 0) { // Byte
                uint8_t val = reg_val & 0xFF;
                result = val - data;
                cpu->d[reg_num] = (reg_val & 0xFFFFFF00) | (result & 0xFF);
                set_flags(cpu, data, val, result, 0, true);
            } else if (size_code == 1) { // Word
                uint16_t val = reg_val & 0xFFFF;
                result = val - data;
                cpu->d[reg_num] = (reg_val & 0xFFFF0000) | (result & 0xFFFF);
                set_flags(cpu, data, val, result, 1, true);
            } else if (size_code == 2) { // Long
                result = reg_val - data;
                cpu->d[reg_num] = result;
                set_flags(cpu, data, reg_val, result, 2, true);
            }
        } else if ((opcode & 0xFF38) == 0x0400) { // SUBI #<data>,Dn
            int size_code = (opcode >> 6) & 0x3;
            int reg_num = opcode & 0x7;
            
            uint32_t data;
            if (size_code == 0) { // Byte
                data = mem_read_word(cpu->pc) & 0xFF;
                cpu->pc += 2;
            } else if (size_code == 1) { // Word
                data = mem_read_word(cpu->pc);
                cpu->pc += 2;
            } else { // Long
                data = mem_read_long(cpu->pc);
                cpu->pc += 4;
            }

            uint32_t reg_val = cpu->d[reg_num];
            uint32_t result;

            if (size_code == 0) { // Byte
                uint8_t val = reg_val & 0xFF;
                result = val - (data & 0xFF);
                cpu->d[reg_num] = (reg_val & 0xFFFFFF00) | (result & 0xFF);
                set_flags(cpu, data, val, result, 0, true);
            } else if (size_code == 1) { // Word
                uint16_t val = reg_val & 0xFFFF;
                result = val - (data & 0xFFFF);
                cpu->d[reg_num] = (reg_val & 0xFFFF0000) | (result & 0xFFFF);
                set_flags(cpu, data, val, result, 1, true);
            } else { // Long
                result = reg_val - data;
                cpu->d[reg_num] = result;
                set_flags(cpu, data, reg_val, result, 2, true);
            }

        } else if ((opcode & 0xF038) == 0x9000) { // Catches SUB.B/W/L Dm, Dn
            int src_reg = opcode & 0x7;
            int dest_reg = (opcode >> 9) & 0x7;
            int size_field = (opcode >> 6) & 0x7; // 000=Byte, 001=Word, 010=Long

            uint32_t src_val = cpu->d[src_reg];
            uint32_t dest_val = cpu->d[dest_reg];
            uint32_t result;

            if (size_field == 0) { // Byte
                uint8_t s = src_val & 0xFF;
                uint8_t d = dest_val & 0xFF;
                result = d - s;
                cpu->d[dest_reg] = (dest_val & 0xFFFFFF00) | (result & 0xFF);
                set_flags(cpu, s, d, result, 0, true);
            } else if (size_field == 1) { // Word
                uint16_t s = src_val & 0xFFFF;
                uint16_t d = dest_val & 0xFFFF;
                result = d - s;
                cpu->d[dest_reg] = (dest_val & 0xFFFF0000) | (result & 0xFFFF);
                set_flags(cpu, s, d, result, 1, true);
            } else if (size_field == 2) { // Long
                result = dest_val - src_val;
                cpu->d[dest_reg] = result;
                set_flags(cpu, src_val, dest_val, result, 2, true);
            }
        } else if ((opcode & 0xF038) == 0xD000) { // ADD Dm, Dn
            int src_reg = opcode & 0x7;
            int dest_reg = (opcode >> 9) & 0x7;
            int opmode = (opcode >> 6) & 0x7; // opmode: 000=B, 001=W, 010=L
            
            uint32_t src_val = cpu->d[src_reg];
            uint32_t dest_val = cpu->d[dest_reg];
            uint32_t result;

            if (opmode == 0) { // Byte
                uint8_t s = src_val & 0xFF;
                uint8_t d = dest_val & 0xFF;
                result = d + s;
                cpu->d[dest_reg] = (dest_val & 0xFFFFFF00) | (result & 0xFF);
                set_flags(cpu, s, d, result, 0, false);
            } else if (opmode == 1) { // Word
                uint16_t s = src_val & 0xFFFF;
                uint16_t d = dest_val & 0xFFFF;
                result = d + s;
                cpu->d[dest_reg] = (dest_val & 0xFFFF0000) | (result & 0xFFFF);
                set_flags(cpu, s, d, result, 1, false);
            } else if (opmode == 2) { // Long
                result = dest_val + src_val;
                cpu->d[dest_reg] = result;
                set_flags(cpu, src_val, dest_val, result, 2, false);
            }
        } else if ((opcode & 0xF138) == 0x5000) { // ADDQ #imm,Dn
            int data = (opcode >> 9) & 0x7;
            if (data == 0) data = 8;
            int size_code = (opcode >> 6) & 0x3;
            int reg_num = opcode & 0x7;

            uint32_t reg_val = cpu->d[reg_num];
            uint32_t result;
            
            if (size_code == 0) { // Byte
                uint8_t val = reg_val & 0xFF;
                result = val + data;
                cpu->d[reg_num] = (reg_val & 0xFFFFFF00) | (result & 0xFF);
                set_flags(cpu, data, val, result, 0, false);
            } else if (size_code == 1) { // Word
                uint16_t val = reg_val & 0xFFFF;
                result = val + data;
                cpu->d[reg_num] = (reg_val & 0xFFFF0000) | (result & 0xFFFF);
                set_flags(cpu, data, val, result, 1, false);
            } else if (size_code == 2) { // Long
                result = reg_val + data;
                cpu->d[reg_num] = result;
                set_flags(cpu, data, reg_val, result, 2, false);
            }
        } else if ((opcode & 0xFF38) == 0x0600) { // ADDI #<data>,Dn
            int size_code = (opcode >> 6) & 0x3;
            int reg_num = opcode & 0x7;
            
            uint32_t data;
            if (size_code == 0) { // Byte
                data = mem_read_word(cpu->pc) & 0xFF;
                cpu->pc += 2;
            } else if (size_code == 1) { // Word
                data = mem_read_word(cpu->pc);
                cpu->pc += 2;
            } else { // Long
                data = mem_read_long(cpu->pc);
                cpu->pc += 4;
            }

            uint32_t reg_val = cpu->d[reg_num];
            uint32_t result;

            if (size_code == 0) { // Byte
                uint8_t val = reg_val & 0xFF;
                result = val + (data & 0xFF);
                cpu->d[reg_num] = (reg_val & 0xFFFFFF00) | (result & 0xFF);
                set_flags(cpu, data, val, result, 0, false);
            } else if (size_code == 1) { // Word
                uint16_t val = reg_val & 0xFFFF;
                result = val + (data & 0xFFFF);
                cpu->d[reg_num] = (reg_val & 0xFFFF0000) | (result & 0xFFFF);
                set_flags(cpu, data, val, result, 1, false);
            } else { // Long
                result = reg_val + data;
                cpu->d[reg_num] = result;
                set_flags(cpu, data, reg_val, result, 2, false);
            }
        } else if ((opcode & 0xFF38) == 0x0200) { // ANDI #<data>,Dn
            int size_code = (opcode >> 6) & 0x3;
            int reg_num = opcode & 0x7;
            
            uint32_t data;
            if (size_code == 0) { // Byte
                data = mem_read_word(cpu->pc) & 0xFF;
                cpu->pc += 2;
            } else if (size_code == 1) { // Word
                data = mem_read_word(cpu->pc);
                cpu->pc += 2;
            } else { // Long
                data = mem_read_long(cpu->pc);
                cpu->pc += 4;
            }

            uint32_t reg_val = cpu->d[reg_num];
            uint32_t result;

            if (size_code == 0) { // Byte
                result = (reg_val & 0xFF) & (data & 0xFF);
                cpu->d[reg_num] = (reg_val & 0xFFFFFF00) | result;
            } else if (size_code == 1) { // Word
                result = (reg_val & 0xFFFF) & (data & 0xFFFF);
                cpu->d[reg_num] = (reg_val & 0xFFFF0000) | result;
            } else { // Long
                result = reg_val & data;
                cpu->d[reg_num] = result;
            }
            
            set_logic_flags(cpu, result, size_code);
        } else if ((opcode & 0xFFF8) == 0x0800) { // BTST #<data>,Dn
            int reg_num = opcode & 0x7;
            uint8_t bit_num = mem_read_word(cpu->pc) & 0xFF;
            cpu->pc += 2;

            uint32_t reg_val = cpu->d[reg_num];
            uint32_t mask = 1 << (bit_num % 32);

            set_sr_flag(cpu, SR_Z, (reg_val & mask) == 0);
        } else if ((opcode & 0xFFF8) == 0x0840) { // BCHG #<data>,Dn
            int reg_num = opcode & 0x7;
            uint8_t bit_num = mem_read_word(cpu->pc) & 0xFF;
            cpu->pc += 2;

            uint32_t reg_val = cpu->d[reg_num];
            uint32_t mask = 1 << (bit_num % 32);

            set_sr_flag(cpu, SR_Z, (reg_val & mask) == 0);
            
            cpu->d[reg_num] = reg_val ^ mask;
        } else if ((opcode & 0xFFF8) == 0x0880) { // BCLR #<data>,Dn
            int reg_num = opcode & 0x7;
            uint8_t bit_num = mem_read_word(cpu->pc) & 0xFF;
            cpu->pc += 2;

            uint32_t reg_val = cpu->d[reg_num];
            uint32_t mask = 1 << (bit_num % 32);

            set_sr_flag(cpu, SR_Z, (reg_val & mask) == 0);
            
            cpu->d[reg_num] = reg_val & ~mask;
        } else if ((opcode & 0xFFF8) == 0x08C0) { // BSET #<data>,Dn
            int reg_num = opcode & 0x7;
            uint8_t bit_num = mem_read_word(cpu->pc) & 0xFF;
            cpu->pc += 2;

            uint32_t reg_val = cpu->d[reg_num];
            uint32_t mask = 1 << (bit_num % 32);

            set_sr_flag(cpu, SR_Z, (reg_val & mask) == 0);
            
            cpu->d[reg_num] = reg_val | mask;
        } else if ((opcode & 0xF000) == 0x6000) { // Bcc
            int condition = (opcode >> 8) & 0xF;
            int8_t displacement = opcode & 0xFF;
            bool branch = false;
            bool z = (cpu->sr >> SR_Z) & 1;
            bool n = (cpu->sr >> SR_N) & 1;
            bool v = (cpu->sr >> SR_V) & 1;
            bool c = (cpu->sr >> SR_C) & 1;

            switch (condition) {
                case 0x0: // BRA
                    branch = true;
                    break;
                case 0x2: // BHI
                    branch = !c && !z;
                    break;
                case 0x3: // BLS
                    branch = c || z;
                    break;
                case 0x4: // BCC
                    branch = !c;
                    break;
                case 0x5: // BCS
                    branch = c;
                    break;
                case 0x6: // BNE
                    branch = !z;
                    break;
                case 0x7: // BEQ
                    branch = z;
                    break;
                case 0x8: // BVC
                    branch = !v;
                    break;
                case 0x9: // BVS
                    branch = v;
                    break;
                case 0xA: // BPL
                    branch = !n;
                    break;
                case 0xB: // BMI
                    branch = n;
                    break;
                case 0xC: // BGE
                    branch = (n && v) || (!n && !v);
                    break;
                case 0xD: // BLT
                    branch = (n && !v) || (!n && v);
                    break;
                case 0xE: // BGT
                    branch = (n && v && !z) || (!n && !v && !z);
                    break;
                case 0xF: // BLE
                    branch = z || (n && !v) || (!n && v);
                    break;
                default:
                    // BSR is not implemented yet
                    break;
            }

            if (branch) {
                // The displacement is relative to the current PC, which is the address
                // of the instruction *after* the branch instruction.
                cpu->pc = current_pc + 2 + displacement;
            }
        } else if (opcode == 0x4E75) { // RTS
            running = false;
        } else {
            printf("WARN: Unknown or unimplemented opcode: %04X\n", opcode);
            running = false; // Stop on unknown instructions
        }

        // Print instruction and state AFTER execution
        if (map) {
            printf("L%-3d: %-20s | ", map->line_number, map->instruction_text);
        } else {
            printf("%-26s | ", "??: (no source)");
        }
        cpu_dump_registers(cpu);

        cycles++;
    }

    if (cycles >= MAX_EXECUTION_CYCLES) {
        printf("\nWARN: Maximum execution cycles reached. Halting simulation.\n");
    }
    printf("\nINFO: Execution finished.\n");
}
