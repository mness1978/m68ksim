#define _DEFAULT_SOURCE
#include "disassembler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define HASH_TABLE_SIZE 1024

typedef struct MappingNode {
    SourceMapping mapping;
    struct MappingNode* next;
} MappingNode;

static MappingNode* hash_table[HASH_TABLE_SIZE] = {NULL};

static unsigned int hash(uint32_t address) {
    return address % HASH_TABLE_SIZE;
}

void disassembler_add_mapping(uint32_t address, int line_number, const char* text) {
    unsigned int index = hash(address);
    MappingNode* new_node = (MappingNode*)malloc(sizeof(MappingNode));
    if (new_node == NULL) {
        // Handle malloc failure
        return;
    }
    new_node->mapping.address = address;
    new_node->mapping.line_number = line_number;
    new_node->mapping.instruction_text = strdup(text);
    new_node->next = hash_table[index];
    hash_table[index] = new_node;
}

SourceMapping* disassembler_get_mapping(uint32_t address) {
    unsigned int index = hash(address);
    MappingNode* current = hash_table[index];
    while (current != NULL) {
        if (current->mapping.address == address) {
            return &current->mapping;
        }
        current = current->next;
    }
    return NULL;
}

void disassembler_cleanup() {
    for (int i = 0; i < HASH_TABLE_SIZE; ++i) {
        MappingNode* current = hash_table[i];
        while (current != NULL) {
            MappingNode* temp = current;
            current = current->next;
            free(temp->mapping.instruction_text);
            free(temp);
        }
        hash_table[i] = NULL;
    }
}