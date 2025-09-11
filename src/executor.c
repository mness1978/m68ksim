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

// Resolves an effective address, returns the final address, and handles pre/post operations
uint32_t resolve_ea(CPU* cpu, uint8_t ea_field, int size_code) {
    uint8_t mode = (ea_field >> 3) & 0x7;
    uint8_t reg = ea_field & 0x7;
    uint32_t address;
    int increment = 0;

    if (size_code == 0) increment = 1; // Byte
    else if (size_code == 1) increment = 2; // Word
    else increment = 4; // Long
    if (reg == 7 && (mode == 3 || mode == 4)) { // A7 with pre/post is always 2 for byte
        if (size_code == 0) increment = 2;
    }
    switch (mode) {
        case 2: // (An)
            return cpu->a[reg];
        case 3: // (An)+
            address = cpu->a[reg];
            cpu->a[reg] += increment;
            return address;
        case 4: // -(An)
            cpu->a[reg] -= increment;
            return cpu->a[reg];
        case 5: // d16(An)
            {
                int16_t displacement = (int16_t)mem_read_word(cpu->pc);
                cpu->pc += 2;
                return cpu->a[reg] + displacement;
            }
    }
    return 0; // Should not happen for these modes
}

// Reads a value from an effective address
uint32_t read_from_ea(CPU* cpu, uint8_t ea_field, int size_code) {
    uint8_t mode = (ea_field >> 3) & 0x7;
    uint8_t reg = ea_field & 0x7;

    // The special EA field for Immediate is Mode=7, Reg=4
	if (mode == 7 && reg == 4) { // Immediate
        uint32_t data;
        if (size_code == 2) { // Long
            data = mem_read_long(cpu->pc);
            cpu->pc += 4;
        } else { // Byte or Word
            data = mem_read_word(cpu->pc);
            cpu->pc += 2;
        }
		return data; // Return the full data read
    }

    switch (mode) {
        case 0: // Dn
            return cpu->d[reg];
        case 1: // An
            return cpu->a[reg];
        default: // All other memory-based modes
            {
                uint32_t address = resolve_ea(cpu, ea_field, size_code);
                if (size_code == 0) return mem_read_byte(address);
                if (size_code == 1) return mem_read_word(address);
                return mem_read_long(address);
            }
    }
}

// Writes a value to an effective addres
void write_to_ea(CPU* cpu, uint8_t ea_field, uint32_t value, int size_code) {
    uint8_t mode = (ea_field >> 3) & 0x7;
    uint8_t reg = ea_field & 0x7;

    switch (mode) {
        case 0: // Dn - Data Register Direct
            if (size_code == 0) { // Byte
                cpu->d[reg] = (cpu->d[reg] & 0xFFFFFF00) | (value & 0xFF);
            } else if (size_code == 1) { // Word
                cpu->d[reg] = (cpu->d[reg] & 0xFFFF0000) | (value & 0xFFFF);
            } else { // Long
                cpu->d[reg] = value;
            }
            break;
        case 1: // An - Address Register Direct
            if (size_code == 1) { // Word
                // Word writes to An are sign-extended to 32 bits
                cpu->a[reg] = (int32_t)(int16_t)value;
            } else { // Long
                cpu->a[reg] = value;
            }
            break;
        default: // All other memory-based modes
            {
                uint32_t address = resolve_ea(cpu, ea_field, size_code);
                if (size_code == 0) mem_write_byte(address, value);
                else if (size_code == 1) mem_write_word(address, value);
                else mem_write_long(address, value);
            }
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

        // Handler for MOVE.B
        if ((opcode & 0xF000) == 0x1000) {
            uint8_t dest_ea_mode = (opcode >> 6) & 0x7;
            uint8_t dest_ea_reg = (opcode >> 9) & 0x7;
            uint8_t dest_ea = (dest_ea_mode << 3) | dest_ea_reg;
            uint8_t src_ea = opcode & 0x3F;
            uint32_t value = read_from_ea(cpu, src_ea, 0); // 0=Byte
            write_to_ea(cpu, dest_ea, value, 0);
            set_sr_flag(cpu, SR_V, false);
            set_sr_flag(cpu, SR_C, false);
            set_logic_flags(cpu, value, 0);
        
        // Handler for MOVE.L and MOVEA.L
        } else if ((opcode & 0xF000) == 0x2000) {
            uint8_t dest_ea_mode = (opcode >> 6) & 0x7;
            uint8_t dest_ea_reg = (opcode >> 9) & 0x7;
            uint8_t dest_ea = (dest_ea_mode << 3) | dest_ea_reg;
            uint8_t src_ea = opcode & 0x3F;
            uint32_t value = read_from_ea(cpu, src_ea, 2); // 2=Long

            if (dest_ea_mode == 1) { // Destination is An (MOVEA.L)
                write_to_ea(cpu, dest_ea, value, 2);
                // MOVEA does not affect flags
            } else { // Destination is not An (MOVE.L)
                write_to_ea(cpu, dest_ea, value, 2);
                set_sr_flag(cpu, SR_V, false);
                set_sr_flag(cpu, SR_C, false);
                set_logic_flags(cpu, value, 2);
            }

        // Handler for MOVE.W and MOVEA.W
        } else if ((opcode & 0xF000) == 0x3000) {
            uint8_t dest_ea_mode = (opcode >> 6) & 0x7;
            uint8_t dest_ea_reg = (opcode >> 9) & 0x7;
            uint8_t dest_ea = (dest_ea_mode << 3) | dest_ea_reg;
            uint8_t src_ea = opcode & 0x3F;
            uint32_t value = read_from_ea(cpu, src_ea, 1); // 1=Word

            if (dest_ea_mode == 1) { // Destination is An (MOVEA.W)
                // Word moves to An are sign-extended, so write as Long
                write_to_ea(cpu, dest_ea, (int32_t)(int16_t)value, 2);
                // MOVEA does not affect flags
            } else { // Destination is not An (MOVE.W)
				write_to_ea(cpu, dest_ea, value, 1);
                set_sr_flag(cpu, SR_V, false);
                set_sr_flag(cpu, SR_C, false);
                set_logic_flags(cpu, value, 1);
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
        } else if ((opcode & 0xC000) == 0x0000) { // Catches MOVE.B/W/L (but not MOVEA)
            uint16_t size_bits = (opcode >> 12) & 0x3; // 1=B, 3=W, 2=L
            int size_code = -1;
            if (size_bits == 1) size_code = 0; // Byte
            if (size_bits == 3) size_code = 1; // Word
            if (size_bits == 2) size_code = 2; // Long

            if (size_code != -1) {
                uint8_t src_ea = opcode & 0x3F;
                uint8_t dest_ea = ((opcode >> 6) & 0x7) | (((opcode >> 9) & 0x7) << 3);

                // Note: Immediate source is NOT handled by this instruction format.
                // It's handled by specific instructions like ADDI, SUBI, ANDI, etc.
                // Or by MOVE #imm which is a different pattern.
                // Let's ensure this handler doesn't run for immediate sources.
                if (src_ea != 0b111100) {
                    uint32_t value = read_from_ea(cpu, src_ea, size_code);
                    write_to_ea(cpu, dest_ea, value, size_code);
                    
                    set_sr_flag(cpu, SR_V, false);
                    set_sr_flag(cpu, SR_C, false);
                    set_logic_flags(cpu, value, size_code);
                } else {
                    // This case indicates an immediate instruction that we should have caught earlier.
                    // If we get here, it means we have a bug in our instruction ordering or masks.
                    printf("WARN: Unhandled immediate-style opcode: %04X\n", opcode);
                    running = false;
                }
            } else {
                 printf("WARN: Unimplemented MOVE variant: %04X\n", opcode);
                 running = false;
            }
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
