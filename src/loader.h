    
#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include <stdio.h>

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
    ARI_DISPLACEMENT,           // d(An)
    IMMEDIATE,                  // #<data>
    ABSOLUTE_SHORT,             // xxxx.W or xxxx
    ABSOLUTE_LONG,              // xxxx.L
    PC_RELATIVE_DISPLACEMENT    // d16(PC) or label(PC)
} AddressingMode;

// Represents a parsed operand
typedef struct {
    AddressingMode mode;
    int reg_num;                // For register modes
    uint32_t value;             // For immediate or absolute address values
    int16_t displacement;       // For d(An) mode
    char* label;                // For unresolved labels
} Operand;

// Loads an assembly file into memory
int load_file(const char* filename, uint32_t* start_address);

#endif // LOADER_H
