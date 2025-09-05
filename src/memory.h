#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

// 68000 has a 24-bit address bus, but we use a smaller size for practical simulation
#define MEMORY_SIZE (16 * 1024 * 1024) // 16MB

// A structure to track changes
typedef struct {
    uint32_t address;
    uint8_t old_value;
    uint8_t new_value;
} MemoryChange;

void mem_init();
void mem_shutdown();

uint8_t mem_read_byte(uint32_t address);
uint16_t mem_read_word(uint32_t address);
uint32_t mem_read_long(uint32_t address);

void mem_write_byte(uint32_t address, uint8_t value);
void mem_write_word(uint32_t address, uint16_t value);
void mem_write_long(uint32_t address, uint32_t value);

void mem_dump_changes(const char* filename);

#endif // MEMORY_H
