#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>
/****************************************************************************************************
 *                                Start of Global Variables                                         *
 ***************************************************************************************************/
// Create a variable to determine if the program is running
int running;

// Create memory: 65536 locations
uint16_t memory[UINT16_MAX];

// Create the registers:
enum registers {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT
};
uint16_t reg[R_COUNT];

// Create the instructions:
enum operations {
    OP_BR = 0,      // Branch
    OP_ADD,         // Add
    OP_LD,          // Load
    OP_ST,          // Store
    OP_JSR,         // Jump Register
    OP_AND,         // Bitwise and
    OP_LDR,         // Load Register
    OP_STR,         // Store Register
    OP_RTI,         // Unused
    OP_NOT,         // Bitwise not
    OP_LDI,         // Load Indirect
    OP_STI,         // Store Indirect
    OP_JMP,         // Jump
    OP_RES,         // Reserved (Unused)
    OP_LEA,         // Lead Effective Address
    OP_TRAP         // Trap
};

// Create the condition Flags
enum flags {
    FL_POS = 1 << 0,
    FL_ZRO = 1 << 1,
    FL_NEG = 1 << 2,
};

// Create the possible traps
enum traps {
    TRAP_GETC  = 0x20,      // Get a character from keyboard
    TRAP_OUT   = 0x21,      // Output a character
    TRAP_PUTS  = 0x22,      // Output a word string
    TRAP_IN    = 0x23,      // Input a string
    TRAP_PUTSP = 0x24,      // Output a bytestring
    TRAP_HALT  = 0x25       // Halt the program
};

// Create memory mapped registers
enum mem_regs {
    MR_KBSR = 0xFE00,       // Keyboard Status
    MR_KBDR = 0xFE02        // Keyboard Data
};

struct termios original_tio;
/****************************************************************************************************
 *                                  End of Global Variables                                         *
 ***************************************************************************************************/


/****************************************************************************************************
 *                                Start of Helper Functions                                         *
 ***************************************************************************************************/
uint16_t sign_extend(uint16_t x, int bit_count) {
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}


void update_flags(uint16_t r) {
    if (reg[r] == 0) {
        reg[R_COND] = FL_ZRO;
    } else if (reg[r] >> 15) {
        // If the leftmost bit is a 1, then the number is negative
        //  shift right 15 times to get the most significant bit only
        reg[R_COND] = FL_NEG;
    } else {
        reg[R_COND] = FL_POS;
    }
}

uint16_t change_endian(uint16_t v) {
    return (v << 8) | (v >> 8);
}


void read_image_file(FILE* file) {
    // Origin says where to put the .text section
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = change_endian(origin);

    // We know the max possible file size (last possible address - starting addr)
    //  so only 1 fread is needed
    uint16_t max_read = UINT16_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    // Swap to little endian
    while (read-- > 0) {
        *p = change_endian(*p);
        ++p;
    }
}


int read_image(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    read_image_file(file);
    fclose(file);
    return 1;
}


uint16_t check_key() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}


uint16_t mem_read(uint16_t addr) {
    if (addr == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        } else {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[addr];
}

void mem_write(uint16_t addr, uint16_t val) {
    memory[addr] = val;
}


void disable_input_buffering() {
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}


void sigint_handler(int signal) {
    restore_input_buffering();
    printf("\n");
    exit(-2);
}
/****************************************************************************************************
 *                                  End of Helper Functions                                         *
 ***************************************************************************************************/


/****************************************************************************************************
 *                             Start of Operation Functions                                         *
 ***************************************************************************************************/
void and(uint16_t instruction) {
    uint16_t dr = (instruction >> 9) & 0x7;
    uint16_t sr1 = (instruction >> 6) & 0x7;
    uint16_t immediate_flag = (instruction >> 5) & 0x1;

    if (immediate_flag) {
        uint16_t immediate = sign_extend(instruction & 0x1F, 5);
        reg[dr] = reg[sr1] & immediate;
    } else {
        uint16_t sr2 = instruction & 0x7;
        reg[dr] = reg[sr1] & reg[sr2];
    }
    update_flags(dr);
}
void not(uint16_t instruction) {
    uint16_t dr = (instruction >> 9) & 0x7;
    uint16_t sr = (instruction >> 6) & 0x7;
    reg[dr] = ~reg[sr];
    update_flags(dr);
}
void add(uint16_t instruction) {
    uint16_t dr = (instruction >> 9) & 0x7;
    uint16_t r1 = (instruction >> 6) & 0x7;
    uint16_t immediate_flag = (instruction >> 5) & 0x1;

    if (immediate_flag) {
        uint16_t immediate = sign_extend(instruction & 0x1F, 5);
        reg[dr] = reg[r1] + immediate;
    } else {
        uint16_t r2 = instruction & 0x7;
        reg[dr] = reg[r1] + reg[r2];
    }

    update_flags(dr);
}
void br(uint16_t instruction) {
    uint16_t offset = sign_extend(instruction & 0x1FF, 9);
    uint16_t cond_flag = (instruction >> 9) & 0x7;
    if (cond_flag & reg[R_COND]) {
        reg[R_PC] = reg[R_PC] + offset;
    }
}
void jmp(uint16_t instruction) {
    uint16_t sr = (instruction >> 6) & 0x7;
    reg[R_PC] = reg[sr];
}
void jsr(uint16_t instruction) {
    uint16_t offset_flag = (instruction >> 11) & 0x1;
    reg[R_R7] = reg[R_PC];

    if (offset_flag) {
        uint16_t offset = sign_extend(instruction & 0x7FF, 11);
        reg[R_PC] = reg[R_PC] + offset;

    } else {
        uint16_t sr = (instruction >> 6) & 0x7;
        reg[R_PC] = reg[sr];
    }
}
void ld(uint16_t instruction) {
    uint16_t dr = (instruction >> 9) & 0x7;
    uint16_t offset = sign_extend(instruction & 0x1FF, 9);
    reg[dr] = mem_read(reg[R_PC] + offset);
    update_flags(dr);
}
void st(uint16_t instruction) {
    uint16_t sr = (instruction >> 9) & 0x7;
    uint16_t offset = sign_extend(instruction & 0x1FF, 9);
    mem_write(reg[R_PC] + offset, reg[sr]);
}
void lea(uint16_t instruction) {
    uint16_t dr = (instruction >> 9) & 0x7;
    uint16_t offset = sign_extend(instruction & 0x1FF, 9);
    reg[dr] = reg[R_PC] + offset;
    update_flags(dr);
}
void ldi(uint16_t instruction) {
    uint16_t dr = (instruction >> 9) & 0x7;
    uint16_t offset = sign_extend(instruction & 0x1FF, 9);
    reg[dr] = mem_read(mem_read(reg[R_PC] + offset));
    update_flags(dr);
}
void sti(uint16_t instruction) {
    uint16_t sr = (instruction >> 9) & 0x7;
    uint16_t offset = sign_extend(instruction & 0x1FF, 9);
    mem_write(mem_read(reg[R_PC] + offset), reg[sr]);
}
void ldr(uint16_t instruction) {
    uint16_t dr = (instruction >> 9) & 0x7;
    uint16_t r1 = (instruction >> 6) & 0x7;
    uint16_t offset = sign_extend(instruction & 0x3F, 6);
    reg[dr] = mem_read(reg[r1] + offset);
    update_flags(dr);
    return;
}
void str(uint16_t instruction) {
    uint16_t sr = (instruction >> 9) & 0x7;
    uint16_t r1 = (instruction >> 6) & 0x7;
    uint16_t offset = sign_extend(instruction & 0x3F, 6);
    mem_write(reg[r1] + offset, reg[sr]);
}
void rti(uint16_t instruction) {
    abort();
}
void res(uint16_t instruction) {
    abort();
}



/****************************************************************************************************
 *                                  Start of Trap Functions                                         *
 ***************************************************************************************************/

void trap_getc() {
    reg[R_R0] = (uint16_t)getchar();
}
void trap_out() {
    putc((char)reg[R_R0], stdout);
    fflush(stdout);
}
void trap_puts() {
    uint16_t* c = memory + reg[R_R0];
    while (*c) {
        putc((char)*c, stdout);
        ++c;
    }
    fflush(stdout);
}
void trap_in() {
    printf("Enter a character: ");
    reg[R_R0] = (uint16_t)getchar();
}
void trap_putsp() {
    uint16_t* c = memory + reg[R_R0];
    while (*c) {
        char char1 = (*c) & 0xFF;
        putc(char1, stdout);
        char char2 = (*c) >> 8;
        if (char2) {
            putc(char2, stdout);
        }
        ++c;
    }
    fflush(stdout);
}
void trap_halt() {
    printf("Halting execution\n");
    running = 0;
}

void trap(uint16_t instruction) {
    uint16_t trapvect = instruction & 0xFF;
    switch (trapvect) {
        case TRAP_PUTS:
            trap_puts();
            break;
        case TRAP_GETC:
            trap_getc();
            break;
        case TRAP_OUT:
            trap_out();
            break;
        case TRAP_IN:
            trap_in();
            break;
        case TRAP_PUTSP:
            trap_putsp();
            break;
        case TRAP_HALT:
            trap_halt();
            break;
    }
}

/****************************************************************************************************
 *                                    End of Trap Functions                                         *
 ***************************************************************************************************/


/****************************************************************************************************
 *                               End of Operation Functions                                         *
 ***************************************************************************************************/

int main(int argc, const char* argv[]) {
    // Load Args
    if (argc < 2) {
        // Show usage string
        printf("lc3-vm [image-file1] ...\n");
        exit(2);
    }
    for (int i = 0; i < argc; ++i) {
        if (!read_image(argv[i])) {
            printf("Failed to load image %s\n", argv[i]);
            exit(1);
        }
    }

    // Initial Setup
    signal(SIGINT, sigint_handler);
    disable_input_buffering();

    // Set the PC to the starting position
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    running = 1;

    while(running) {
        // Fetch an instruction
        uint16_t instruction = mem_read(reg[R_PC]++);
        uint16_t op = instruction >> 12;

        switch (op) {
            case OP_BR:
                br(instruction);
                break;
            case OP_ADD:
                add(instruction);
                break;
            case OP_LD:
                ld(instruction);
                break;
            case OP_ST:
                st(instruction);
                break;
            case OP_JSR:
                jsr(instruction);
                break;
            case OP_AND:
                and(instruction);
                break;
            case OP_LDR:
                ldr(instruction);
                break;
            case OP_STR:
                str(instruction);
                break;
            case OP_RTI:
                rti(instruction);
                break;
            case OP_NOT:
                not(instruction);
                break;
            case OP_LDI:
                ldi(instruction);
                break;
            case OP_STI:
                sti(instruction);
                break;
            case OP_JMP:
                jmp(instruction);
                break;
            case OP_RES:
                res(instruction);
                break;
            case OP_LEA:
                lea(instruction);
                break;
            case OP_TRAP:
                trap(instruction);
                break;
            default:
                // Bad Opcode
                abort();
                break;
        }
    }

    restore_input_buffering();
}
