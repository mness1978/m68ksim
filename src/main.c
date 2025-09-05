#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // for getopt

#include "cpu.h"
#include "memory.h"
#include "loader.h"
#include "executor.h"
#include "disassembler.h"

void print_usage(const char* prog_name) {
    fprintf(stderr, "Usage: %s [options] <assembly_file>\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -a <address>  Load program at the specified hex address (default: 0x10000)\n");
    fprintf(stderr, "  -h            Show this help message\n");
}

int main(int argc, char* argv[]) {
    uint32_t start_address = 0x10000;
    int opt;

    while ((opt = getopt(argc, argv, "ha:")) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            case 'a':
                start_address = strtoul(optarg, NULL, 16);
                break;
            default: /* '?' */
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    mem_init();

    if (optind < argc) {
        char* filename = argv[optind];
        printf("INFO: Loading assembly file: %s\n", filename);
        if (load_file(filename, &start_address) != 0) {
            fprintf(stderr, "Error: Failed to load file '%s'.\n", filename);
            mem_shutdown();
            return EXIT_FAILURE;
        }
    } else {
        // No file provided, use a default hardcoded program.
        printf("INFO: No assembly file provided, using hardcoded program.\n");
        mem_write_word(start_address, 0x303C);      // MOVE.W #3,D0
        mem_write_word(start_address + 2, 0x0003);
        mem_write_word(start_address + 4, 0x5340);      // SUBQ.W #1,D0
        mem_write_word(start_address + 6, 0x66FC);      // BNE -4 (to 0x10004)
        mem_write_word(start_address + 8, 0x4E75);      // RTS

        disassembler_add_mapping(start_address, 1, "MOVE.W #3,D0");
        disassembler_add_mapping(start_address + 4, 2, "SUBQ.W #1,D0");
        disassembler_add_mapping(start_address + 6, 3, "BNE LOOP");
        disassembler_add_mapping(start_address + 8, 4, "RTS");
    }

    CPU cpu;
    cpu_pulse_reset(&cpu);
    cpu.pc = start_address;

    execute_program(&cpu);

    mem_dump_changes("memory_dump.txt");
    disassembler_cleanup();
    mem_shutdown();

    return EXIT_SUCCESS;
}
