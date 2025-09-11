#define _DEFAULT_SOURCE
#include "loader.h"
#include "memory.h"
#include "disassembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define HASH_TABLE_SIZE 1024

static HashTable* symbol_table = NULL;

// Hash function (djb2)
static unsigned long hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

HashTable* create_symbol_table(unsigned int size) {
    HashTable* ht = malloc(sizeof(HashTable));
    if (!ht) return NULL;
    ht->size = size;
    ht->table = calloc(size, sizeof(Symbol*));
    if (!ht->table) {
        free(ht);
        return NULL;
    }
    return ht;
}

void destroy_symbol_table(HashTable* ht) {
    if (!ht) return;
    for (unsigned int i = 0; i < ht->size; i++) {
        Symbol* current = ht->table[i];
        while (current) {
            Symbol* next = current->next;
            free(current->name);
            free(current);
            current = next;
        }
    }
    free(ht->table);
    free(ht);
}

void add_symbol(HashTable* ht, const char* name, uint32_t address) {
    if (find_symbol(ht, name)) {
        fprintf(stderr, "WARN: Duplicate symbol '%s' found. Ignoring.\n", name);
        return;
    }
    unsigned long hash_index = hash(name) % ht->size;
    Symbol* new_symbol = malloc(sizeof(Symbol));
    if (!new_symbol) {
        fprintf(stderr, "Error: Could not allocate memory for new symbol.\n");
        return;
    }
    new_symbol->name = strdup(name);
    new_symbol->address = address;
    new_symbol->next = ht->table[hash_index];
    ht->table[hash_index] = new_symbol;
}

Symbol* find_symbol(HashTable* ht, const char* name) {
    unsigned long hash_index = hash(name) % ht->size;
    Symbol* current = ht->table[hash_index];
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Dynamically read a line from a file
char* read_line_dynamically(FILE* f) {
    char* line = NULL;
    size_t size = 0;
    ssize_t len = getline(&line, &size, f);
    if (len == -1) {
        free(line);
        return NULL;
    }
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }
    return line;
}

// Helper to trim leading/trailing whitespace
char* trim(char* str) {
    char* end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Parses an instruction string like "MOVE.W" into its base and size suffix.
void parse_instruction_mnemonic(const char* opcode_str, char* base, char* size) {
    *size = 'W'; // Default to word size
    const char* dot = strrchr(opcode_str, '.');
    if (dot && (tolower(dot[1]) == 'b' || tolower(dot[1]) == 'w' || tolower(dot[1]) == 'l')) {
        strncpy(base, opcode_str, dot - opcode_str);
        base[dot - opcode_str] = '\0';
        *size = toupper(dot[1]);
    } else {
        strcpy(base, opcode_str);
    }
}

// Parses a string to determine the addressing mode and extract operands.
// Returns 0 on success, -1 on failure.
int parse_operand(const char* str, Operand* operand) {
	int reg;
	int16_t disp;

	if (!str || !operand) return -1;

    char* trimmed_str = trim((char*)str);
    memset(operand, 0, sizeof(Operand));

    // Data Register Direct: Dn
    if (sscanf(trimmed_str, "D%d", &reg) == 1 && trimmed_str[2] == '\0') {
        operand->mode = DATA_REGISTER_DIRECT;
        operand->reg_num = reg;
        return 0;
    }

    // Address Register Direct: An
    if (sscanf(trimmed_str, "A%d", &reg) == 1 && trimmed_str[2] == '\0') {
        operand->mode = ADDRESS_REGISTER_DIRECT;
        operand->reg_num = reg;
        return 0;
    }

    // Immediate: #<value>
    if (trimmed_str[0] == '#') {
        operand->mode = IMMEDIATE;
        char* value_str = &trimmed_str[1];
        if (value_str[0] == '$') { // Hex
            operand->value = strtoul(&value_str[1], NULL, 16);
        } else { // Decimal
            operand->value = strtoul(value_str, NULL, 10);
        }
        return 0;
    }

    // Check for the most specific patterns first.

    // Pre-decrement: -(An)
    if (sscanf(trimmed_str, "-(A%d)", &reg) == 1 && trimmed_str[5] == '\0') {
        operand->mode = ARI_PRE_DECREMENT;
        operand->reg_num = reg;
        return 0;
    }
  
    // Post-increment: (An)+
    if (sscanf(trimmed_str, "(A%d)+", &reg) == 1 && trimmed_str[4] == '+') {
		operand->mode = ARI_POST_INCREMENT;
        operand->reg_num = reg;
        return 0;
    }
    
    // Displacement: d(An)
    if (sscanf(trimmed_str, "%hd(A%d)", &disp, &reg) == 2) {
        operand->mode = ARI_DISPLACEMENT;
        operand->reg_num = reg;
        operand->displacement = disp;
        return 0;
    }

    // Basic Indirect: (An) - This must be checked LAST.
    if (sscanf(trimmed_str, "(A%d)", &reg) == 1) {
        operand->mode = ADDRESS_REGISTER_INDIRECT;
        operand->reg_num = reg;
        return 0;
    }

    operand->mode = UNKNOWN_MODE;
    return -1;
}

// Determines the size of an operand based on its addressing mode.
int get_operand_size(Operand* op, char size_suffix) {
    switch (op->mode) {
        case IMMEDIATE:
            return (size_suffix == 'L') ? 4 : 2;
        // Add other modes that contribute to size here
        default:
            return 0;
    }
}

// Determines the size of an instruction based on its mnemonic and operands.
int get_instruction_size(const char* opcode_str, const char* operands_str, char** saveptr) {
    char base_mnemonic[10];
    char size_suffix;
    parse_instruction_mnemonic(opcode_str, base_mnemonic, &size_suffix);

    // Fixed-size instructions
    if (strcasecmp(base_mnemonic, "NOP") == 0) return 2;
    if (strcasecmp(base_mnemonic, "RTS") == 0) return 2;
    if (strncasecmp(base_mnemonic, "B", 1) == 0 && strlen(base_mnemonic) > 1) return 2;
    if (strcasecmp(base_mnemonic, "SUBQ") == 0) return 2;
    if (strcasecmp(base_mnemonic, "ADDQ") == 0) return 2;
    
    // Instructions with variable size based on immediate data
    if (strcasecmp(base_mnemonic, "ADDI") == 0) return (size_suffix == 'L') ? 6 : 4;
    if (strcasecmp(base_mnemonic, "SUBI") == 0) return (size_suffix == 'L') ? 6 : 4;
    if (strcasecmp(base_mnemonic, "ANDI") == 0) return (size_suffix == 'L') ? 6 : 4;
    
    // Bit manipulation instructions
    if (strcasecmp(base_mnemonic, "BTST") == 0 || strcasecmp(base_mnemonic, "BCHG") == 0 ||
        strcasecmp(base_mnemonic, "BCLR") == 0 || strcasecmp(base_mnemonic, "BSET") == 0) {
        char temp_operands[100]; strcpy(temp_operands, operands_str);
        char* src_str = strtok_r(temp_operands, ",", saveptr);
        Operand src_op;
        if (src_str && parse_operand(src_str, &src_op) == 0 && src_op.mode == IMMEDIATE) return 4;
        return 2;
    }

    if (strcasecmp(base_mnemonic, "MOVE") == 0) {
        int size = 2; // Base opcode word
        char temp_operands[100]; strcpy(temp_operands, operands_str);
        char* src_str = strtok_r(temp_operands, ",", saveptr);
        char* dest_str = strtok_r(NULL, "", saveptr);
        Operand src_op, dest_op;

        if (src_str && parse_operand(src_str, &src_op) == 0) {
            if (src_op.mode == IMMEDIATE) size += (size_suffix == 'L') ? 4 : 2;
            else if (src_op.mode == ARI_DISPLACEMENT) size += 2;
        }
        if (dest_str && parse_operand(dest_str, &dest_op) == 0) {
            if (dest_op.mode == ARI_DISPLACEMENT) size += 2;
        }
        return size;
    }

    return 2; // Default
}

// Helper to encode an operand into its 6-bit Effective Address field
uint8_t encode_ea(Operand* op) {
    uint8_t mode = 0;
    uint8_t reg = op->reg_num;

    switch (op->mode) {
        case DATA_REGISTER_DIRECT:      mode = 0b000; break;
        case ADDRESS_REGISTER_DIRECT:   mode = 0b001; break;
        case ADDRESS_REGISTER_INDIRECT: mode = 0b010; break;
        case ARI_POST_INCREMENT:        mode = 0b011; break;
        case ARI_PRE_DECREMENT:         mode = 0b100; break;
        case ARI_DISPLACEMENT:          mode = 0b101; break;
        case IMMEDIATE:                 mode = 0b111; reg = 0b100; break;
        default: break; // Should not be reached for valid EA modes
    }
    return (mode << 3) | reg;
}

// First pass: find all labels and record their addresses
void perform_first_pass(FILE* f, uint32_t* start_address) {
    char* line;
    uint32_t current_address = *start_address;
    int symbol_count = 0;
    bool org_seen = false;

    while ((line = read_line_dynamically(f))) {
        char* original_line = line;
        char* comment = strchr(original_line, ';');
        if (comment) *comment = '\0';
        char* trimmed_line = trim(original_line);

        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '*') {
            free(original_line);
            continue;
        }

        char* instruction_part = trimmed_line;
        char* colon = strchr(trimmed_line, ':');
        if (colon) {
            *colon = '\0';
            char* label = trim(trimmed_line);
            add_symbol(symbol_table, label, current_address);
            symbol_count++;
            instruction_part = trim(colon + 1);
        }

        if (strlen(instruction_part) > 0) {
            char temp_instruction_part[256];
            strcpy(temp_instruction_part, instruction_part);
            char* saveptr_first_pass = NULL;
            char* opcode_str = strtok_r(temp_instruction_part, " \t", &saveptr_first_pass);
            if (opcode_str) {
                if (strcasecmp(opcode_str, "ORG") == 0) {
                    char* operand_str = strtok_r(NULL, "", &saveptr_first_pass);
                    if (operand_str) {
                        // Add this line to trim the operand string
                        char* trimmed_operand = trim(operand_str);
                
                        uint32_t org_address;
                        // Use the trimmed_operand variable for parsing
                        if (trimmed_operand[0] == '$') {
                            org_address = strtoul(&trimmed_operand[1], NULL, 16);
                        } else {
                            org_address = strtoul(trimmed_operand, NULL, 10);
                        }

                        // The rest of this logic was correct, no change needed here
                        if (!org_seen) { // org_seen flag is from the previous fix
                            *start_address = org_address;
                            org_seen = true;
                        }
                        current_address = org_address;
                    }
                } else {
                    char* operands_str = strtok_r(NULL, "", &saveptr_first_pass);
                    current_address += get_instruction_size(opcode_str, operands_str, &saveptr_first_pass);
                }
            }
        }
        free(original_line);
    }
    printf("INFO: First pass complete. Found %d symbols.\n", symbol_count);
    for (unsigned int i = 0; i < symbol_table->size; i++) {
        Symbol* current = symbol_table->table[i];
        while (current) {
            printf("  - Symbol: %-20s Address: 0x%08X\n", current->name, current->address);
            current = current->next;
        }
    }
}

// Second pass: parse instructions and write to memory using the new parsing functions
void perform_second_pass(FILE* f, uint32_t start_address) {
    char* line;
    uint32_t current_address = start_address;
    int line_number = 0;
    char* saveptr_second_pass;

    while ((line = read_line_dynamically(f))) {
        line_number++;
        char* original_line = line;
        char* comment = strchr(line, ';');
        if (comment) *comment = '\0';

        char* trimmed_line = trim(line);

        char* instruction_part = trimmed_line;
        char* colon = strchr(trimmed_line, ':');
        if (colon) {
            *colon = '\0'; // Isolate label
            instruction_part = trim(colon + 1);
        }

        if (strlen(instruction_part) == 0 || instruction_part[0] == '*') {
            free(original_line);
            continue;
        }

        disassembler_add_mapping(current_address, line_number, instruction_part);

        char* opcode_str = strtok_r(instruction_part, " \t", &saveptr_second_pass);
        if (!opcode_str) {
            free(original_line);
            continue;
        }

        if (strcasecmp(opcode_str, "ORG") == 0) {
            char* operand_str = strtok_r(NULL, "", &saveptr_second_pass);
            if (operand_str) {
                // Add this line to trim the operand string
                char* trimmed_operand = trim(operand_str);

                // Use the trimmed_operand variable for parsing
                if (trimmed_operand[0] == '$') {
                    current_address = strtoul(&trimmed_operand[1], NULL, 16);
                } else {
                    current_address = strtoul(trimmed_operand, NULL, 10);
                }
            }
            free(original_line);
            continue;
        }

        char base_mnemonic[10];
        char size_suffix;
        parse_instruction_mnemonic(opcode_str, base_mnemonic, &size_suffix);

        char* operands_str = strtok_r(NULL, "", &saveptr_second_pass);

        if (strcasecmp(base_mnemonic, "NOP") == 0) {
            mem_write_word(current_address, 0x4E71);
            current_address += 2;
        } else if (strcasecmp(base_mnemonic, "RTS") == 0) {
            mem_write_word(current_address, 0x4E75);
            current_address += 2;
        } else if (strcasecmp(base_mnemonic, "MOVE") == 0) {
            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;

            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for MOVE\n", line_number);
            } else if (src_op.mode == IMMEDIATE && dest_op.mode == ADDRESS_REGISTER_DIRECT) {
                // This is actually the MOVEA instruction: MOVEA #imm, An
                uint16_t machine_code;
                if (size_suffix == 'L') {
                    machine_code = 0x207C | (dest_op.reg_num << 9); // MOVEA.L #imm, An
                    mem_write_word(current_address, machine_code);
                    mem_write_long(current_address + 2, src_op.value);
                    current_address += 6;
                } else { // Word
                    machine_code = 0x307C | (dest_op.reg_num << 9); // MOVEA.W #imm, An
                    mem_write_word(current_address, machine_code);
                    mem_write_word(current_address + 2, src_op.value);
                    current_address += 4;
                }
            } else {
                // This handles all other MOVE instructions
                uint16_t size_bits;
                if (size_suffix == 'B') size_bits = 0x1;
                else if (size_suffix == 'L') size_bits = 0x2;
                else size_bits = 0x3; // Word

                uint16_t dest_ea = encode_ea(&dest_op);
                uint16_t src_ea = encode_ea(&src_op);
                uint16_t machine_code = (size_bits << 12) | ((dest_ea & 0b111) << 9) | ((dest_ea >> 3) << 6) | src_ea;
                
                mem_write_word(current_address, machine_code);
                current_address += 2;

                // If the source was immediate, we need to write its value now.
                if (src_op.mode == IMMEDIATE) {
                    if (size_suffix == 'L') {
                        mem_write_long(current_address, src_op.value);
                        current_address += 4;
                    } else { // Word or Byte
                        mem_write_word(current_address, src_op.value);
                        current_address += 2;
                    }
                }

                if (src_op.mode == ARI_DISPLACEMENT) {
                    mem_write_word(current_address, src_op.displacement);
                    current_address += 2;
                }
                if (dest_op.mode == ARI_DISPLACEMENT) {
                    mem_write_word(current_address, dest_op.displacement);
                    current_address += 2;
                }
            }
        } else if (strcasecmp(base_mnemonic, "SUBQ") == 0) {
            uint16_t size_bits;
            if (size_suffix == 'B') size_bits = 0x0000;
            else if (size_suffix == 'L') size_bits = 0x0080;
            else size_bits = 0x0040; // .W

            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;

            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for SUBQ\n", line_number);
            } else if (src_op.mode != IMMEDIATE || dest_op.mode != DATA_REGISTER_DIRECT) {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for SUBQ\n", line_number);
            } else if (src_op.value == 0 || src_op.value > 8) {
                fprintf(stderr, "L%d: Error: Immediate value for SUBQ must be between 1 and 8\n", line_number);
            } else {
                uint16_t data = src_op.value % 8; // 1-7 map to 1-7, 8 maps to 0
                uint16_t machine_code = 0x5100 | size_bits | (data << 9) | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                current_address += 2;
            }
        } else if (strcasecmp(base_mnemonic, "ADDQ") == 0) {
            uint16_t size_bits;
            if (size_suffix == 'B') size_bits = 0x0000;
            else if (size_suffix == 'L') size_bits = 0x0080;
            else size_bits = 0x0040; // .W

            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;

            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for ADDQ\n", line_number);
            } else if (src_op.mode != IMMEDIATE || dest_op.mode != DATA_REGISTER_DIRECT) {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for ADDQ\n", line_number);
            } else if (src_op.value < 1 || src_op.value > 8) {
                fprintf(stderr, "L%d: Error: Immediate value for ADDQ must be between 1 and 8\n", line_number);
            } else {
                uint16_t data = src_op.value % 8; // 1-7 map to 1-7, 8 maps to 0
                uint16_t machine_code = 0x5000 | size_bits | (data << 9) | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                current_address += 2;
            }
        } else if (strcasecmp(base_mnemonic, "ADDI") == 0) {
            uint16_t size_bits;
            if (size_suffix == 'B') size_bits = 0x0000;
            else if (size_suffix == 'L') size_bits = 0x0080;
            else size_bits = 0x0040; // .W

            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;

            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for ADDI\n", line_number);
            } else if (src_op.mode != IMMEDIATE || dest_op.mode != DATA_REGISTER_DIRECT) {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for ADDI\n", line_number);
            } else {
                uint16_t machine_code = 0x0600 | size_bits | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                if (size_suffix == 'L') {
                    mem_write_long(current_address + 2, src_op.value);
                    current_address += 6;
                } else {
                    mem_write_word(current_address + 2, src_op.value);
                    current_address += 4;
                }
            }
        } else if (strcasecmp(base_mnemonic, "SUBI") == 0) {
            uint16_t size_bits;
            if (size_suffix == 'B') size_bits = 0x0000;
            else if (size_suffix == 'L') size_bits = 0x0080;
            else size_bits = 0x0040; // .W

            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;

            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for SUBI\n", line_number);
            } else if (src_op.mode != IMMEDIATE || dest_op.mode != DATA_REGISTER_DIRECT) {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for SUBI\n", line_number);
            } else {
                uint16_t machine_code = 0x0400 | size_bits | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                if (size_suffix == 'L') {
                    mem_write_long(current_address + 2, src_op.value);
                    current_address += 6;
                } else {
                    mem_write_word(current_address + 2, src_op.value);
                    current_address += 4;
                }
            }
        } else if (strcasecmp(base_mnemonic, "ANDI") == 0) {
            uint16_t size_bits;
            if (size_suffix == 'B') size_bits = 0x0000;
            else if (size_suffix == 'L') size_bits = 0x0080;
            else size_bits = 0x0040; // .W

            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;

            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for ANDI\n", line_number);
            } else if (src_op.mode != IMMEDIATE || dest_op.mode != DATA_REGISTER_DIRECT) {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for ANDI\n", line_number);
            } else {
                uint16_t machine_code = 0x0200 | size_bits | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                if (size_suffix == 'L') {
                    mem_write_long(current_address + 2, src_op.value);
                    current_address += 6;
                } else {
                    mem_write_word(current_address + 2, src_op.value);
                    current_address += 4;
                }
            }
        } else if (strcasecmp(base_mnemonic, "BTST") == 0) {
            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;
            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for BTST\n", line_number);
            } else if (src_op.mode == IMMEDIATE && dest_op.mode == DATA_REGISTER_DIRECT) {
                // Correctly handles: BTST #imm, Dn
                uint16_t machine_code = 0x0800 | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                mem_write_word(current_address + 2, src_op.value);
                current_address += 4;
            } else if (src_op.mode == DATA_REGISTER_DIRECT && dest_op.mode == DATA_REGISTER_DIRECT) {
                // Correctly handles: BTST Dn, Dm
                uint16_t machine_code = 0x0100 | (src_op.reg_num << 9) | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                current_address += 2;
            } else {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for BTST\n", line_number);
            }
        } else if (strcasecmp(base_mnemonic, "BCHG") == 0) {
            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;
            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for BCHG\n", line_number);
            } else if (src_op.mode == IMMEDIATE && dest_op.mode == DATA_REGISTER_DIRECT) {
                // Correctly handles: BCHG #imm, Dn
                uint16_t machine_code = 0x0840 | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                mem_write_word(current_address + 2, src_op.value);
                current_address += 4;
            } else if (src_op.mode == DATA_REGISTER_DIRECT && dest_op.mode == DATA_REGISTER_DIRECT) {
                // Correctly handles: BCHG Dn, Dm
                uint16_t machine_code = 0x0140 | (src_op.reg_num << 9) | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                current_address += 2;
            } else {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for BCHG\n", line_number);
            }
        } else if (strcasecmp(base_mnemonic, "BCLR") == 0) {
            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;
            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for BCLR\n", line_number);
            } else if (src_op.mode == IMMEDIATE && dest_op.mode == DATA_REGISTER_DIRECT) {
                // Correctly handles: BCLR #imm, Dn
                uint16_t machine_code = 0x0880 | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                mem_write_word(current_address + 2, src_op.value);
                current_address += 4;
            } else if (src_op.mode == DATA_REGISTER_DIRECT && dest_op.mode == DATA_REGISTER_DIRECT) {
                // Correctly handles: BCLR Dn, Dm
                uint16_t machine_code = 0x0180 | (src_op.reg_num << 9) | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                current_address += 2;
            } else {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for BCLR\n", line_number);
            }
        } else if (strcasecmp(base_mnemonic, "BSET") == 0) {
            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;
            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for BSET\n", line_number);
            } else if (src_op.mode == IMMEDIATE && dest_op.mode == DATA_REGISTER_DIRECT) {
                // Correctly handles: BSET #imm, Dn
                uint16_t machine_code = 0x08C0 | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                mem_write_word(current_address + 2, src_op.value);
                current_address += 4;
            } else if (src_op.mode == DATA_REGISTER_DIRECT && dest_op.mode == DATA_REGISTER_DIRECT) {
                // Correctly handles: BSET Dn, Dm
                uint16_t machine_code = 0x01C0 | (src_op.reg_num << 9) | dest_op.reg_num;
                mem_write_word(current_address, machine_code);
                current_address += 2;
            } else {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for BSET\n", line_number);
            }
        } else if (strcasecmp(base_mnemonic, "ADD") == 0) {
            uint16_t size_bits;
            if (size_suffix == 'B') size_bits = 0x0000;
            else if (size_suffix == 'L') size_bits = 0x0080;
            else size_bits = 0x0040; // .W

            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;

            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for ADD\n", line_number);
            } else if (src_op.mode != DATA_REGISTER_DIRECT || dest_op.mode != DATA_REGISTER_DIRECT) {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for ADD\n", line_number);
            } else {
                uint16_t machine_code = 0xD000 | size_bits | (dest_op.reg_num << 9) | src_op.reg_num;
                mem_write_word(current_address, machine_code);
                current_address += 2;
            }
        } else if (strcasecmp(base_mnemonic, "SUB") == 0) {
            uint16_t size_bits;
            if (size_suffix == 'B') size_bits = 0x0000;
            else if (size_suffix == 'L') size_bits = 0x0080;
            else size_bits = 0x0040; // .W

            char* src_str = strtok_r(operands_str, ",", &saveptr_second_pass);
            char* dest_str = strtok_r(NULL, "", &saveptr_second_pass);
            Operand src_op, dest_op;

            if (parse_operand(src_str, &src_op) != 0 || parse_operand(dest_str, &dest_op) != 0) {
                fprintf(stderr, "L%d: Error: Invalid operands for SUB\n", line_number);
            } else if (src_op.mode != DATA_REGISTER_DIRECT || dest_op.mode != DATA_REGISTER_DIRECT) {
                fprintf(stderr, "L%d: Error: Unsupported operand combination for SUB\n", line_number);
            } else {
                uint16_t machine_code = 0x9000 | size_bits | (dest_op.reg_num << 9) | src_op.reg_num;
                mem_write_word(current_address, machine_code);
                current_address += 2;
            }
        } else if (strncasecmp(base_mnemonic, "B", 1) == 0 && strlen(base_mnemonic) > 1) {
            Symbol* sym = find_symbol(symbol_table, trim(operands_str));
            if (sym) {
                int32_t displacement = sym->address - (current_address + 2);
                if (displacement >= -128 && displacement <= 127) {
                    uint16_t condition_code = 0;
                    if (strcasecmp(base_mnemonic, "BRA") == 0) condition_code = 0x60;
                    else if (strcasecmp(base_mnemonic, "BHI") == 0) condition_code = 0x62;
                    else if (strcasecmp(base_mnemonic, "BLS") == 0) condition_code = 0x63;
                    else if (strcasecmp(base_mnemonic, "BCC") == 0) condition_code = 0x64;
                    else if (strcasecmp(base_mnemonic, "BCS") == 0) condition_code = 0x65;
                    else if (strcasecmp(base_mnemonic, "BNE") == 0) condition_code = 0x66;
                    else if (strcasecmp(base_mnemonic, "BEQ") == 0) condition_code = 0x67;
                    else if (strcasecmp(base_mnemonic, "BVC") == 0) condition_code = 0x68;
                    else if (strcasecmp(base_mnemonic, "BVS") == 0) condition_code = 0x69;
                    else if (strcasecmp(base_mnemonic, "BPL") == 0) condition_code = 0x6A;
                    else if (strcasecmp(base_mnemonic, "BMI") == 0) condition_code = 0x6B;
                    else if (strcasecmp(base_mnemonic, "BGE") == 0) condition_code = 0x6C;
                    else if (strcasecmp(base_mnemonic, "BLT") == 0) condition_code = 0x6D;
                    else if (strcasecmp(base_mnemonic, "BGT") == 0) condition_code = 0x6E;
                    else if (strcasecmp(base_mnemonic, "BLE") == 0) condition_code = 0x6F;
                    mem_write_word(current_address, (condition_code << 8) | (uint8_t)displacement);
                } else {
                    fprintf(stderr, "L%d: Error: Branch target out of range for %s.S\n", line_number, base_mnemonic);
                }
            } else {
                fprintf(stderr, "L%d: Error: Undefined symbol '%s'\n", line_number, operands_str);
            }
            current_address += 2;
        } else {
             fprintf(stderr, "L%d: WARN: Instruction '%s' is not yet supported by the new assembler.\n", line_number, base_mnemonic);
             current_address += 2;
        }

        free(original_line);
    }
}



int load_file(const char* filename, uint32_t* start_address) {
    symbol_table = create_symbol_table(HASH_TABLE_SIZE);
    if (!symbol_table) {
        fprintf(stderr, "Failed to create symbol table.\n");
        return -1;
    }

    FILE* f = fopen(filename, "r");
    if (!f) {
        perror("Failed to open assembly file");
        destroy_symbol_table(symbol_table);
        return -1;
    }

    printf("INFO: Starting first pass...\n");
    perform_first_pass(f, start_address);

    rewind(f);

    printf("INFO: Starting second pass...\n");
    perform_second_pass(f, *start_address);

    fclose(f);
    destroy_symbol_table(symbol_table);
    symbol_table = NULL;
    return 0; // Success
}
