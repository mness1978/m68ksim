#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t* memory = NULL;
static MemoryChange* changes = NULL;
static int change_count = 0;
static int change_capacity = 0;

void mem_init() {
    memory = (uint8_t*)malloc(MEMORY_SIZE);
    if (!memory) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
    }
    memset(memory, 0, MEMORY_SIZE);
    change_capacity = 1024; // Initial capacity
    changes = (MemoryChange*)malloc(change_capacity * sizeof(MemoryChange));
    if (!changes) {
        perror("Failed to allocate memory for changes");
        free(memory);
        exit(EXIT_FAILURE);
    }
}

void mem_shutdown() {
    free(memory);
    free(changes);
    memory = NULL;
    changes = NULL;
}

static void record_change(uint32_t address, uint8_t old_val, uint8_t new_val) {
    if (change_count >= change_capacity) {
        change_capacity *= 2;
        changes = (MemoryChange*)realloc(changes, change_capacity * sizeof(MemoryChange));
        if (!changes) {
            perror("Failed to reallocate memory for changes");
            // Not a fatal error, we just lose change tracking
            return;
        }
    }
    changes[change_count].address = address;
    changes[change_count].old_value = old_val;
    changes[change_count].new_value = new_val;
    change_count++;
}

uint8_t mem_read_byte(uint32_t address) {
    return memory[address % MEMORY_SIZE];
}

uint16_t mem_read_word(uint32_t address) {
    return (memory[address % MEMORY_SIZE] << 8) | memory[(address + 1) % MEMORY_SIZE];
}

uint32_t mem_read_long(uint32_t address) {
    return (memory[address % MEMORY_SIZE] << 24) |
           (memory[(address + 1) % MEMORY_SIZE] << 16) |
           (memory[(address + 2) % MEMORY_SIZE] << 8) |
           memory[(address + 3) % MEMORY_SIZE];
}

void mem_write_byte(uint32_t address, uint8_t value) {
    uint32_t addr = address % MEMORY_SIZE;
    record_change(addr, memory[addr], value);
    memory[addr] = value;
}

void mem_write_word(uint32_t address, uint16_t value) {
    uint32_t addr = address % MEMORY_SIZE;
    mem_write_byte(addr, (value >> 8) & 0xFF);
    mem_write_byte(addr + 1, value & 0xFF);
}

void mem_write_long(uint32_t address, uint32_t value) {
    uint32_t addr = address % MEMORY_SIZE;
    mem_write_byte(addr, (value >> 24) & 0xFF);
    mem_write_byte(addr + 1, (value >> 16) & 0xFF);
    mem_write_byte(addr + 2, (value >> 8) & 0xFF);
    mem_write_byte(addr + 3, value & 0xFF);
}

void mem_dump_changes(const char* filename) {
    if (change_count == 0) {
        return;
    }
    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("Could not open memory dump file");
        return;
    }
    fprintf(f, "--- Memory Changes ---\n");
    // Simple linear dump for now. A hexdump format would be better.
    for (int i = 0; i < change_count; ++i) {
        fprintf(f, "0x%08X: 0x%02X -> 0x%02X\n",
                changes[i].address, changes[i].old_value, changes[i].new_value);
    }
    fclose(f);
    printf("Memory changes written to %s\n", filename);
}
