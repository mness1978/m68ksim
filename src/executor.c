#include "executor.h"
#include "memory.h"
#include "disassembler.h"
#include <stdio.h>
#include <stdbool.h>

#define MAX_EXECUTION_CYCLES 5000 // Safety break to prevent infinite loops

// --- Forward declarations for instruction handler functions ---
static void handle_move_b(CPU* cpu, uint16_t opcode);
static void handle_move_l(CPU* cpu, uint16_t opcode);
static void handle_move_w(CPU* cpu, uint16_t opcode);
static void handle_subq(CPU* cpu, uint16_t opcode);
static void handle_subi(CPU* cpu, uint16_t opcode);
static void handle_sub_reg(CPU* cpu, uint16_t opcode);
static void handle_add_reg(CPU* cpu, uint16_t opcode);
static void handle_addq(CPU* cpu, uint16_t opcode);
static void handle_addi(CPU* cpu, uint16_t opcode);
static void handle_andi(CPU* cpu, uint16_t opcode);
static void handle_btst_imm(CPU* cpu, uint16_t opcode);
static void handle_bchg_imm(CPU* cpu, uint16_t opcode);
static void handle_bclr_imm(CPU* cpu, uint16_t opcode);
static void handle_bset_imm(CPU* cpu, uint16_t opcode);
static void handle_bcc(CPU* cpu, uint16_t opcode);
static void handle_nop(CPU* cpu, uint16_t opcode);
static void handle_rts(CPU* cpu, uint16_t opcode);

// --- Opcode to Handler Lookup Table ---
// The order is important! More specific masks must come before more general ones.
static const OpcodeMapping instruction_table[] = {
    { 0xFFF8, 0x0800, handle_btst_imm }, // BTST #imm,Dn
    { 0xFFF8, 0x0840, handle_bchg_imm }, // BCHG #imm,Dn
    { 0xFFF8, 0x0880, handle_bclr_imm }, // BCLR #imm,Dn
    { 0xFFF8, 0x08C0, handle_bset_imm }, // BSET #imm,Dn
    { 0xFF38, 0x0200, handle_andi },     // ANDI #<data>,Dn
    { 0xFF38, 0x0400, handle_subi },     // SUBI #<data>,Dn
    { 0xFF38, 0x0600, handle_addi },     // ADDI #<data>,Dn
    { 0xF138, 0x5000, handle_addq },     // ADDQ #imm,Dn
    { 0xF138, 0x5100, handle_subq },     // SUBQ #imm,Dn
    { 0xF000, 0x1000, handle_move_b },   // MOVE.B
    { 0xF000, 0x2000, handle_move_l },   // MOVE.L / MOVEA.L
    { 0xF000, 0x3000, handle_move_w },   // MOVE.W / MOVEA.W
    { 0xF000, 0x6000, handle_bcc },      // Bcc
    { 0xF038, 0x9000, handle_sub_reg },  // SUB.B/W/L Dm,Dn
    { 0xF038, 0xD000, handle_add_reg },  // ADD.B/W/L Dm,Dn
    { 0xFFFF, 0x4E71, handle_nop },      // NOP
    { 0xFFFF, 0x4E75, handle_rts },      // RTS
};
static const int num_opcodes = sizeof(instruction_table) / sizeof(OpcodeMapping);


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
    int increment = (size_code == 0) ? 1 : (size_code == 1) ? 2 : 4;
    if (reg == 7 && (mode == 3 || mode == 4) && size_code == 0) increment = 2;

    switch (mode) {
        case 2: return cpu->a[reg]; // (An)
        case 3: address = cpu->a[reg]; cpu->a[reg] += increment; return address; // (An)+
        case 4: cpu->a[reg] -= increment; return cpu->a[reg]; // -(An)
        case 5: { // d16(An)
            int16_t displacement = (int16_t)mem_read_word(cpu->pc);
            cpu->pc += 2;
            return cpu->a[reg] + displacement;
        }
        case 7: // Special modes
            switch (reg) {
                case 0: { // Absolute Short
                    uint32_t addr = (int32_t)(int16_t)mem_read_word(cpu->pc);
                    cpu->pc += 2;
                    return addr;
                }
                case 1: { // Absolute Long
                    uint32_t addr = mem_read_long(cpu->pc);
                    cpu->pc += 4;
                    return addr;
                }
            }
    }
    return 0; // Should not happen for valid memory modes
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

// --- Instruction Handler Implementations ---

static void handle_move_b(CPU* cpu, uint16_t opcode) {
    uint8_t dest_ea_mode = (opcode >> 6) & 0x7;
    uint8_t dest_ea_reg = (opcode >> 9) & 0x7;
    uint8_t dest_ea = (dest_ea_mode << 3) | dest_ea_reg;
    uint8_t src_ea = opcode & 0x3F;
    uint32_t value = read_from_ea(cpu, src_ea, 0); // 0=Byte
    write_to_ea(cpu, dest_ea, value, 0);
    set_sr_flag(cpu, SR_V, false);
    set_sr_flag(cpu, SR_C, false);
    set_logic_flags(cpu, value, 0);
}

static void handle_move_l(CPU* cpu, uint16_t opcode) {
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
}

static void handle_move_w(CPU* cpu, uint16_t opcode) {
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
}

static void handle_subq(CPU* cpu, uint16_t opcode) {
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
    } else { // Long
        result = reg_val - data;
        cpu->d[reg_num] = result;
        set_flags(cpu, data, reg_val, result, 2, true);
    }
}

static void handle_subi(CPU* cpu, uint16_t opcode) {
    int size_code = (opcode >> 6) & 0x3;
    int reg_num = opcode & 0x7;
    
    uint32_t data;
    if (size_code == 0) { data = mem_read_word(cpu->pc) & 0xFF; cpu->pc += 2; }
    else if (size_code == 1) { data = mem_read_word(cpu->pc); cpu->pc += 2; }
    else { data = mem_read_long(cpu->pc); cpu->pc += 4; }

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
}

static void handle_sub_reg(CPU* cpu, uint16_t opcode) {
    int src_reg = opcode & 0x7;
    int dest_reg = (opcode >> 9) & 0x7;
    int size_field = (opcode >> 6) & 0x3; // 0=B, 1=W, 2=L

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
    } else { // Long
        result = dest_val - src_val;
        cpu->d[dest_reg] = result;
        set_flags(cpu, src_val, dest_val, result, 2, true);
    }
}

static void handle_add_reg(CPU* cpu, uint16_t opcode) {
    int src_reg = opcode & 0x7;
    int dest_reg = (opcode >> 9) & 0x7;
    int opmode = (opcode >> 6) & 0x3; // 0=B, 1=W, 2=L
    
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
    } else { // Long
        result = dest_val + src_val;
        cpu->d[dest_reg] = result;
        set_flags(cpu, src_val, dest_val, result, 2, false);
    }
}

static void handle_addq(CPU* cpu, uint16_t opcode) {
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
    } else { // Long
        result = reg_val + data;
        cpu->d[reg_num] = result;
        set_flags(cpu, data, reg_val, result, 2, false);
    }
}

static void handle_addi(CPU* cpu, uint16_t opcode) {
    int size_code = (opcode >> 6) & 0x3;
    int reg_num = opcode & 0x7;
    
    uint32_t data;
    if (size_code == 0) { data = mem_read_word(cpu->pc) & 0xFF; cpu->pc += 2; }
    else if (size_code == 1) { data = mem_read_word(cpu->pc); cpu->pc += 2; }
    else { data = mem_read_long(cpu->pc); cpu->pc += 4; }

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
}

static void handle_andi(CPU* cpu, uint16_t opcode) {
    int size_code = (opcode >> 6) & 0x3;
    int reg_num = opcode & 0x7;
    
    uint32_t data;
    if (size_code == 0) { data = mem_read_word(cpu->pc) & 0xFF; cpu->pc += 2; }
    else if (size_code == 1) { data = mem_read_word(cpu->pc); cpu->pc += 2; }
    else { data = mem_read_long(cpu->pc); cpu->pc += 4; }

    uint32_t reg_val = cpu->d[reg_num];
    uint32_t result;

    if (size_code == 0) { result = (reg_val & 0xFF) & (data & 0xFF); cpu->d[reg_num] = (reg_val & 0xFFFFFF00) | result; }
    else if (size_code == 1) { result = (reg_val & 0xFFFF) & (data & 0xFFFF); cpu->d[reg_num] = (reg_val & 0xFFFF0000) | result; }
    else { result = reg_val & data; cpu->d[reg_num] = result; }
    
    set_logic_flags(cpu, result, size_code);
}

static void handle_btst_imm(CPU* cpu, uint16_t opcode) {
    int reg_num = opcode & 0x7;
    uint8_t bit_num = mem_read_word(cpu->pc) & 0xFF;
    cpu->pc += 2;
    uint32_t reg_val = cpu->d[reg_num];
    uint32_t mask = 1 << (bit_num % 32);
    set_sr_flag(cpu, SR_Z, (reg_val & mask) == 0);
}

static void handle_bchg_imm(CPU* cpu, uint16_t opcode) {
    int reg_num = opcode & 0x7;
    uint8_t bit_num = mem_read_word(cpu->pc) & 0xFF;
    cpu->pc += 2;
    uint32_t reg_val = cpu->d[reg_num];
    uint32_t mask = 1 << (bit_num % 32);
    set_sr_flag(cpu, SR_Z, (reg_val & mask) == 0);
    cpu->d[reg_num] = reg_val ^ mask;
}

static void handle_bclr_imm(CPU* cpu, uint16_t opcode) {
    int reg_num = opcode & 0x7;
    uint8_t bit_num = mem_read_word(cpu->pc) & 0xFF;
    cpu->pc += 2;
    uint32_t reg_val = cpu->d[reg_num];
    uint32_t mask = 1 << (bit_num % 32);
    set_sr_flag(cpu, SR_Z, (reg_val & mask) == 0);
    cpu->d[reg_num] = reg_val & ~mask;
}

static void handle_bset_imm(CPU* cpu, uint16_t opcode) {
    int reg_num = opcode & 0x7;
    uint8_t bit_num = mem_read_word(cpu->pc) & 0xFF;
    cpu->pc += 2;
    uint32_t reg_val = cpu->d[reg_num];
    uint32_t mask = 1 << (bit_num % 32);
    set_sr_flag(cpu, SR_Z, (reg_val & mask) == 0);
    cpu->d[reg_num] = reg_val | mask;
}

static void handle_bcc(CPU* cpu, uint16_t opcode) {
    uint32_t current_pc = cpu->pc - 2; // The PC was already advanced past the opcode
    int condition = (opcode >> 8) & 0xF;
    int8_t displacement = opcode & 0xFF;
    bool branch = false;
    bool z = (cpu->sr >> SR_Z) & 1;
    bool n = (cpu->sr >> SR_N) & 1;
    bool v = (cpu->sr >> SR_V) & 1;
    bool c = (cpu->sr >> SR_C) & 1;

    switch (condition) {
        case 0x0: branch = true; break; // BRA
        case 0x2: branch = !c && !z; break; // BHI
        case 0x3: branch = c || z; break; // BLS
        case 0x4: branch = !c; break; // BCC
        case 0x5: branch = c; break; // BCS
        case 0x6: branch = !z; break; // BNE
        case 0x7: branch = z; break; // BEQ
        case 0x8: branch = !v; break; // BVC
        case 0x9: branch = v; break; // BVS
        case 0xA: branch = !n; break; // BPL
        case 0xB: branch = n; break; // BMI
        case 0xC: branch = (n && v) || (!n && !v); break; // BGE
        case 0xD: branch = (n && !v) || (!n && v); break; // BLT
        case 0xE: branch = (n && v && !z) || (!n && !v && !z); break; // BGT
        case 0xF: branch = z || (n && !v) || (!n && v); break; // BLE
    }

    if (branch) {
        cpu->pc = current_pc + 2 + displacement;
    }
}

static void handle_nop(CPU* cpu, uint16_t opcode) {
    (void)cpu;    // Silence unused parameter warning
    (void)opcode; // Silence unused parameter warning
}

static void handle_rts(CPU* cpu, uint16_t opcode) {
    (void)cpu;    // Silence unused parameter warning
    (void)opcode; // Silence unused parameter warning
    // In the future, this will pop the return address from the stack.
}

// --- Main Execution Loop ---

void execute_program(CPU* cpu) {
    printf("INFO: Beginning execution from 0x%X.\n\n", cpu->pc);
    bool running = true;
    int cycles = 0;

    printf("%-26s | ", "Initial State");
    cpu_dump_registers(cpu);

    while (running && cycles < MAX_EXECUTION_CYCLES) {
        uint32_t current_pc = cpu->pc;
        SourceMapping* map = disassembler_get_mapping(current_pc);

        uint16_t opcode = mem_read_word(cpu->pc);
        cpu->pc += 2;

        // Special case for RTS to halt simulation
        if (opcode == 0x4E75) {
            running = false;
        }

        bool handled = false;
        for (int i = 0; i < num_opcodes; ++i) {
            if ((opcode & instruction_table[i].mask) == instruction_table[i].value) {
                instruction_table[i].handler(cpu, opcode);
                handled = true;
                break;
            }
        }

        if (!handled) {
            printf("WARN: Unknown or unimplemented opcode: %04X\n", opcode);
            running = false;
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