#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// Represents a symbol in the hash table
typedef struct Symbol {
    char* name;
    uint32_t address;
    struct Symbol* next;
} Symbol;

// Represents the hash table for symbols
typedef struct HashTable {
    unsigned int size;
    Symbol** table;
} HashTable;

// Symbol table functions
HashTable* create_symbol_table(unsigned int size);
void destroy_symbol_table(HashTable* ht);
void add_symbol(HashTable* ht, const char* name, uint32_t address);
Symbol* find_symbol(HashTable* ht, const char* name);

// Represents a parsed 68k operand addressing mode
typedef enum {
    UNKNOWN_MODE = 0,
    DATA_REGISTER_DIRECT,       // Dn
    ADDRESS_REGISTER_DIRECT,    // An
    ADDRESS_REGISTER_INDIRECT,  // (An)
    ARI_POST_INCREMENT,         // (An)+
    ARI_PRE_DECREMENT,          // -(An)
    ARI_DISPLACEMENT,           // d16(An)
    ARI_INDEX_8_BIT_DISP,       // d8(An,Xn)
    IMMEDIATE,                  // #<data>
    ABSOLUTE_SHORT,             // xxxx.W or xxxx
    ABSOLUTE_LONG,              // xxxx.L
    PC_RELATIVE_DISPLACEMENT,   // d16(PC)
    PC_RELATIVE_INDEX_8_BIT,    // d8(PC,Xn)
    // --- 68020+ Modes ---
    ARI_INDEX_BASE_DISP,        // (bd,An,Xn)
    PC_INDEX_BASE_DISP,         // (bd,PC,Xn)
    MEMORY_INDIRECT_POST_INDEXED, // ([bd,An],Xn,od)
    MEMORY_INDIRECT_PRE_INDEXED,  // ([bd,An,Xn],od)
    PC_MEM_INDIRECT_POST_INDEXED, // ([bd,PC],Xn,od)
    PC_MEM_INDIRECT_PRE_INDEXED,  // ([bd,PC,Xn],od)
} AddressingMode;

// Represents a parsed operand
typedef struct {
    AddressingMode mode;
    int reg_num;                // Base register number (e.g., in (An) or (PC))
    uint32_t value;             // For immediate or absolute address values
    char* label;                // For unresolved labels
    bool is_pc_relative_label;  // true if label is PC-relative

    // --- 68020+ Fields ---
    int index_reg_num;          // Index register number
    bool index_is_an;           // true if index is An, false if Dn
    char index_size;            // 'W' or 'L'
    int scale;                  // Scale factor (0, 1, 2, 3 for 1, 2, 4, 8)

    int32_t base_displacement;  // Base displacement (bd)
    int32_t outer_displacement; // Outer displacement (od)

    // Flags to determine displacement sizes (0=null, 2=word, 4=long)
    int base_disp_size;
    int outer_disp_size;

} Operand;


// Loads an assembly file into memory
int load_file(const char* filename, uint32_t* start_address);

#endif // LOADER_H
