#define _DEFAULT_SOURCE
#include "loader.h"
#include "memory.h"
#include "disassembler.h"
#include "operand_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define HASH_TABLE_SIZE 1024

static HashTable* symbol_table = NULL;

static unsigned long hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}
HashTable* create_symbol_table(unsigned int size) {
    HashTable* ht = malloc(sizeof(HashTable));
    if (!ht) return NULL;
    ht->size = size;
    ht->table = calloc(size, sizeof(Symbol*));
    if (!ht->table) { free(ht); return NULL; }
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
    if (find_symbol(ht, name)) { return; }
    unsigned long hash_index = hash(name) % ht->size;
    Symbol* new_symbol = malloc(sizeof(Symbol));
    if (!new_symbol) { return; }
    new_symbol->name = strdup(name);
    new_symbol->address = address;
    new_symbol->next = ht->table[hash_index];
    ht->table[hash_index] = new_symbol;
}
Symbol* find_symbol(HashTable* ht, const char* name) {
    unsigned long hash_index = hash(name) % ht->size;
    Symbol* current = ht->table[hash_index];
    while (current) {
        if (strcmp(current->name, name) == 0) return current;
        current = current->next;
    }
    return NULL;
}
char* read_line_dynamically(FILE* f) {
    char* line = NULL;
    size_t size = 0;
    ssize_t len = getline(&line, &size, f);
    if (len == -1) { free(line); return NULL; }
    if (len > 0 && line[len - 1] == '\n') { line[len - 1] = '\0'; }
    return line;
}
char* trim(char* str) {
    if (!str) return str;
    char* end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}
void parse_instruction_mnemonic(const char* opcode_str, char* base, char* size) {
    *size = 'W';
    const char* dot = strrchr(opcode_str, '.');
    if (dot && (tolower(dot[1]) == 'b' || tolower(dot[1]) == 'w' || tolower(dot[1]) == 'l')) {
        strncpy(base, opcode_str, dot - opcode_str);
        base[dot - opcode_str] = '\0';
        *size = toupper(dot[1]);
    } else {
        strcpy(base, opcode_str);
    }
}
int parse_operand(const char* str, Operand* operand) {
    if (!str || !operand) return -1;
    char trimmed_str[100];
    strncpy(trimmed_str, str, 99);
    trimmed_str[99] = '\0';
    char* start = trim(trimmed_str);
    if (*start == '\0') return -1;
    pcc_string_input_t input = { start, 0 };
    pcc_context_t* ctx = pcc_create(&input);
    Operand* result = NULL;
    if (pcc_parse(ctx, &result) != 1 || result == NULL) {
        pcc_destroy(ctx);
        return -1;
    }
    *operand = *result;
    pcc_destroy(ctx);
    return 0;
}
int get_operand_extension_size(Operand* op, char size_suffix) {
    switch (op->mode) {
        case IMMEDIATE:
            return (size_suffix == 'L') ? 4 : 2;
        case ARI_DISPLACEMENT:
        case PC_RELATIVE_DISPLACEMENT:
        case ABSOLUTE_SHORT:
            return 2;
        case ABSOLUTE_LONG:
            return 4;
        default:
            return 0;
    }
}
int get_instruction_size(const char* opcode_str, const char* operands_str) {
    char base_mnemonic[10];
    char size_suffix;
    parse_instruction_mnemonic(opcode_str, base_mnemonic, &size_suffix);
    if (strcasecmp(base_mnemonic, "NOP") == 0) return 2;
    if (strcasecmp(base_mnemonic, "RTS") == 0) return 2;
    if (strcasecmp(base_mnemonic, "SUBQ") == 0) return 2;
    if (strcasecmp(base_mnemonic, "ADDQ") == 0) return 2;
    if (strcasecmp(base_mnemonic, "ADDI") == 0) return (size_suffix == 'L') ? 6 : 4;
    if (strcasecmp(base_mnemonic, "SUBI") == 0) return (size_suffix == 'L') ? 6 : 4;
    if (strcasecmp(base_mnemonic, "ANDI") == 0) return (size_suffix == 'L') ? 6 : 4;
    if (strncasecmp(base_mnemonic, "B", 1) == 0 && strlen(base_mnemonic) > 1) {
        if (strcasecmp(base_mnemonic, "BTST") != 0 && strcasecmp(base_mnemonic, "BCHG") != 0 &&
            strcasecmp(base_mnemonic, "BCLR") != 0 && strcasecmp(base_mnemonic, "BSET") != 0) {
            return 2;
        }
    }
    if (!operands_str) return 2;
    
    char temp_src_str[100];
    char temp_dest_str[100];
    char* src_str = NULL;
    char* dest_str = NULL;

    const char* comma = strchr(operands_str, ',');
    if (comma) {
        size_t len = comma - operands_str;
        strncpy(temp_src_str, operands_str, len);
        temp_src_str[len] = '\0';
        strcpy(temp_dest_str, comma + 1);
        src_str = temp_src_str;
        dest_str = temp_dest_str;
    } else {
        strcpy(temp_src_str, operands_str);
        src_str = temp_src_str;
    }

    if (strcasecmp(base_mnemonic, "BTST") == 0 || strcasecmp(base_mnemonic, "BCHG") == 0 ||
        strcasecmp(base_mnemonic, "BCLR") == 0 || strcasecmp(base_mnemonic, "BSET") == 0) {
        Operand src_op = {0};
        if (src_str && parse_operand(src_str, &src_op) == 0) {
            int size = (src_op.mode == IMMEDIATE) ? 4 : 2;
            if (src_op.label) free(src_op.label);
            return size;
        }
        return 2;
    }
    if (strcasecmp(base_mnemonic, "MOVE") == 0 || strcasecmp(base_mnemonic, "ADD") == 0 || strcasecmp(base_mnemonic, "SUB") == 0) {
        int size = 2;
        Operand src_op = {0}, dest_op = {0};
        if (src_str && parse_operand(src_str, &src_op) == 0) {
            size += get_operand_extension_size(&src_op, size_suffix);
            if (src_op.label) free(src_op.label);
        }
        if (dest_str && parse_operand(dest_str, &dest_op) == 0) {
            size += get_operand_extension_size(&dest_op, size_suffix);
            if (dest_op.label) free(dest_op.label);
        }
        return size;
    }
    return 2;
}
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
        case PC_RELATIVE_DISPLACEMENT:  mode = 0b111; reg = 0b010; break;
        case ABSOLUTE_SHORT:            mode = 0b111; reg = 0b000; break;
        case ABSOLUTE_LONG:             mode = 0b111; reg = 0b001; break;
        case IMMEDIATE:                 mode = 0b111; reg = 0b100; break;
        default: break;
    }
    return (mode << 3) | reg;
}
void write_operand_extensions(uint32_t* address, Operand* op, char size_suffix, uint32_t current_pc) {
    if (op->label) {
        Symbol* sym = find_symbol(symbol_table, op->label);
        uint32_t value = sym ? sym->address : 0;
        if (!sym) fprintf(stderr, "WARN: Undefined symbol '%s' in second pass.\n", op->label);
        if (op->mode == PC_RELATIVE_DISPLACEMENT) {
            int16_t displacement = value - (current_pc + 2);
            mem_write_word(*address, displacement); *address += 2;
        } else if (op->mode == IMMEDIATE || op->mode == ABSOLUTE_LONG) {
            mem_write_long(*address, value); *address += 4;
        } else {
            mem_write_word(*address, value); *address += 2;
        }
        free(op->label);
        op->label = NULL;
    } else {
        switch(op->mode) {
            case IMMEDIATE:
                if (size_suffix == 'L') { mem_write_long(*address, op->value); *address += 4; }
                else { mem_write_word(*address, op->value); *address += 2; }
                break;
            case ABSOLUTE_SHORT:
                mem_write_word(*address, op->value); *address += 2;
                break;
            case ABSOLUTE_LONG:
                mem_write_long(*address, op->value); *address += 4;
                break;
            case ARI_DISPLACEMENT:
            case PC_RELATIVE_DISPLACEMENT:
                mem_write_word(*address, op->displacement); *address += 2;
                break;
            default: break;
        }
    }
}
void perform_first_pass(FILE* f, uint32_t* start_address) {
    char* line;
    uint32_t current_address = *start_address;
    int symbol_count = 0;
    bool org_seen = false;
    while ((line = read_line_dynamically(f))) {
        char* original_line = line;
        char* comment = strchr(original_line, ';'); if (comment) *comment = '\0';
        char* trimmed_line = trim(original_line);
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '*') { free(original_line); continue; }
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
                    char* operand_str = saveptr_first_pass;
                    if (operand_str) {
                        uint32_t org_address = strtoul(trim(operand_str) + 1, NULL, 16);
                        if (!org_seen) { *start_address = org_address; org_seen = true; }
                        current_address = org_address;
                    }
                } else {
                    char* operands_str = saveptr_first_pass;
                    current_address += get_instruction_size(opcode_str, operands_str);
                }
            }
        }
        free(original_line);
    }
    printf("INFO: First pass complete. Found %d symbols.\n", symbol_count);
}
void perform_second_pass(FILE* f, uint32_t start_address) {
    char* line;
    uint32_t current_address = start_address;
    int line_number = 0;
    while ((line = read_line_dynamically(f))) {
        line_number++;
        char* original_line = line;
        char* comment = strchr(line, ';'); if (comment) *comment = '\0';
        char* instruction_part = trim(line);
        char* colon = strchr(instruction_part, ':'); if (colon) instruction_part = trim(colon + 1);
        if (strlen(instruction_part) == 0 || instruction_part[0] == '*') { free(original_line); continue; }
        disassembler_add_mapping(current_address, line_number, instruction_part);
        char temp_instruction_part[256];
        strcpy(temp_instruction_part, instruction_part);
        char* saveptr_line = NULL;
        char* opcode_str = strtok_r(temp_instruction_part, " \t", &saveptr_line);
        if (!opcode_str) { free(original_line); continue; }
        if (strcasecmp(opcode_str, "ORG") == 0) {
            char* operand_str = saveptr_line;
            if (operand_str) current_address = strtoul(trim(operand_str) + 1, NULL, 16);
            free(original_line); continue;
        }
        char base_mnemonic[10]; char size_suffix;
        parse_instruction_mnemonic(opcode_str, base_mnemonic, &size_suffix);
        char* operands_str = saveptr_line;
        
        char temp_src_str[100];
        char temp_dest_str[100];
        char* src_str = NULL;
        char* dest_str = NULL;

        if (operands_str) {
            const char* comma = strchr(operands_str, ',');
            if (comma) {
                size_t len = comma - operands_str;
                strncpy(temp_src_str, operands_str, len);
                temp_src_str[len] = '\0';
                strcpy(temp_dest_str, comma + 1);
                src_str = temp_src_str;
                dest_str = temp_dest_str;
            } else {
                strcpy(temp_src_str, operands_str);
                src_str = temp_src_str;
            }
        }
        
        if (strcasecmp(base_mnemonic, "NOP") == 0) {
            mem_write_word(current_address, 0x4E71);
        } else if (strcasecmp(base_mnemonic, "RTS") == 0) {
            mem_write_word(current_address, 0x4E75);
        } else if (strcasecmp(base_mnemonic, "MOVE") == 0) {
            Operand src_op = {0}, dest_op = {0};
            if (src_str && dest_str && parse_operand(trim(src_str), &src_op) == 0 && parse_operand(trim(dest_str), &dest_op) == 0) {
                uint16_t size_bits = (size_suffix == 'B') ? 1 : (size_suffix == 'L') ? 2 : 3;
                uint16_t dest_ea = encode_ea(&dest_op);
                uint16_t src_ea = encode_ea(&src_op);
                uint16_t machine_code = (size_bits << 12) | ((dest_ea & 0b111) << 9) | ((dest_ea >> 3) << 6) | src_ea;
                mem_write_word(current_address, machine_code);
                uint32_t addr = current_address + 2;
                write_operand_extensions(&addr, &src_op, size_suffix, current_address);
                write_operand_extensions(&addr, &dest_op, size_suffix, current_address);
            } else { fprintf(stderr, "L%d: Error: Invalid operands for MOVE\n", line_number); }
        } else if (strcasecmp(base_mnemonic, "ADDQ") == 0 || strcasecmp(base_mnemonic, "SUBQ") == 0) {
            Operand src_op = {0}, dest_op = {0};
            if (src_str && dest_str && parse_operand(trim(src_str), &src_op) == 0 && parse_operand(trim(dest_str), &dest_op) == 0) {
                uint16_t data = (src_op.value == 8) ? 0 : src_op.value;
                uint16_t size_bits = (size_suffix == 'B') ? 0b00 : (size_suffix == 'W') ? 0b01 : 0b10;
                uint16_t base_op = (strcasecmp(base_mnemonic, "ADDQ") == 0) ? 0x5000 : 0x5100;
                uint16_t machine_code = base_op | (data << 9) | (size_bits << 6) | encode_ea(&dest_op);
                mem_write_word(current_address, machine_code);
            } else { fprintf(stderr, "L%d: Error: Invalid operands for %s\n", line_number, base_mnemonic); }
        } else if (strcasecmp(base_mnemonic, "ADDI") == 0 || strcasecmp(base_mnemonic, "SUBI") == 0 || strcasecmp(base_mnemonic, "ANDI") == 0) {
            Operand src_op = {0}, dest_op = {0};
            if (src_str && dest_str && parse_operand(trim(src_str), &src_op) == 0 && parse_operand(trim(dest_str), &dest_op) == 0) {
                uint16_t size_bits = (size_suffix == 'B') ? 0b00 : (size_suffix == 'W') ? 0b01 : 0b10;
                uint16_t base_op = 0;
                if (strcasecmp(base_mnemonic, "ADDI") == 0) base_op = 0x0600;
                else if (strcasecmp(base_mnemonic, "SUBI") == 0) base_op = 0x0400;
                else if (strcasecmp(base_mnemonic, "ANDI") == 0) base_op = 0x0200;
                uint16_t machine_code = base_op | (size_bits << 6) | encode_ea(&dest_op);
                mem_write_word(current_address, machine_code);
                uint32_t addr = current_address + 2;
                write_operand_extensions(&addr, &src_op, size_suffix, current_address);
            } else { fprintf(stderr, "L%d: Error: Invalid operands for %s\n", line_number, base_mnemonic); }
        } else if (strcasecmp(base_mnemonic, "ADD") == 0 || strcasecmp(base_mnemonic, "SUB") == 0) {
            Operand src_op = {0}, dest_op = {0};
            if (src_str && dest_str && parse_operand(trim(src_str), &src_op) == 0 && parse_operand(trim(dest_str), &dest_op) == 0) {
                uint16_t size_bits = (size_suffix == 'B') ? 0b000 : (size_suffix == 'W') ? 0b001 : 0b010;
                uint16_t base_op = (strcasecmp(base_mnemonic, "ADD") == 0) ? 0xD000 : 0x9000;
                uint16_t machine_code = base_op | (dest_op.reg_num << 9) | (size_bits << 6) | encode_ea(&src_op);
                mem_write_word(current_address, machine_code);
            } else { fprintf(stderr, "L%d: Error: Invalid operands for %s\n", line_number, base_mnemonic); }
        } else if (strcasecmp(base_mnemonic, "BTST") == 0 || strcasecmp(base_mnemonic, "BCHG") == 0 ||
                 strcasecmp(base_mnemonic, "BCLR") == 0 || strcasecmp(base_mnemonic, "BSET") == 0) {
            Operand src_op = {0}, dest_op = {0};
            if (src_str && dest_str && parse_operand(trim(src_str), &src_op) == 0 && parse_operand(trim(dest_str), &dest_op) == 0) {
                uint16_t base_opcode = 0;
                if (strcasecmp(base_mnemonic, "BTST") == 0) base_opcode = (src_op.mode == IMMEDIATE) ? 0x0800 : 0x0100;
                else if (strcasecmp(base_mnemonic, "BCHG") == 0) base_opcode = (src_op.mode == IMMEDIATE) ? 0x0840 : 0x0140;
                else if (strcasecmp(base_mnemonic, "BCLR") == 0) base_opcode = (src_op.mode == IMMEDIATE) ? 0x0880 : 0x0180;
                else if (strcasecmp(base_mnemonic, "BSET") == 0) base_opcode = (src_op.mode == IMMEDIATE) ? 0x08C0 : 0x01C0;
                uint16_t machine_code = base_opcode | encode_ea(&dest_op);
                if (src_op.mode != IMMEDIATE) { machine_code |= (src_op.reg_num << 9); }
                mem_write_word(current_address, machine_code);
                if (src_op.mode == IMMEDIATE) { mem_write_word(current_address + 2, src_op.value); }
            } else { fprintf(stderr, "L%d: Error: Invalid operands for %s\n", line_number, base_mnemonic); }
        } else if (strncasecmp(base_mnemonic, "B", 1) == 0 && strlen(base_mnemonic) > 1) {
            Symbol* sym = find_symbol(symbol_table, trim(src_str));
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
                    
                    if (condition_code != 0) {
                        mem_write_word(current_address, (condition_code << 8) | (uint8_t)displacement);
                    } else {
                        fprintf(stderr, "L%d: Error: Unknown branch instruction '%s'\n", line_number, base_mnemonic);
                    }
                } else { fprintf(stderr, "L%d: Error: Branch target out of range\n", line_number); }
            } else { fprintf(stderr, "L%d: Error: Undefined symbol '%s'\n", line_number, trim(src_str)); }
        } else {
            fprintf(stderr, "L%d: WARN: Assembler does not support instruction '%s'\n", line_number, base_mnemonic);
        }
        current_address += get_instruction_size(opcode_str, operands_str);
        free(original_line);
    }
}
int load_file(const char* filename, uint32_t* start_address) {
    symbol_table = create_symbol_table(HASH_TABLE_SIZE);
    if (!symbol_table) return -1;
    FILE* f = fopen(filename, "r");
    if (!f) { perror("Failed to open assembly file"); destroy_symbol_table(symbol_table); return -1; }
    printf("INFO: Starting first pass...\n");
    perform_first_pass(f, start_address);
    rewind(f);
    printf("INFO: Starting second pass...\n");
    perform_second_pass(f, start_address);
    fclose(f);
    destroy_symbol_table(symbol_table);
    symbol_table = NULL;
    return 0;
}