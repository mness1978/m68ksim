; test.s
; A test program for the 68k simulator

    ORG    $10000

START:
    ; Test MOVE, ADDQ, SUBQ
    MOVE.W  #10,D0      ; D0 = 10
    ADDQ.W  #5,D0       ; D0 = 15
    SUBQ.W  #3,D0       ; D0 = 12

    ; Test ADDI, SUBI
    ADDI.W  #8,D1     ; D1 = 100
    SUBI.W  #4,D1      ; D1 = 50

    ; Test ADD
    MOVE.W  #20,D2
    ADD.W   D0,D2       ; D2 = 12 + 20 = 32

    ; Test ANDI
    MOVE.W  #$F0F0,D3
    ANDI.W  #$0FF0,D3   ; D3 = $00F0

    ; Test Branches
    MOVE.W  #3,D4
    SUBQ.W  #3,D4       ; Set Z=1
    BEQ     EQUAL       ; Branch if D4 is 0 (Z=1)
    NOP                 ; Should be skipped
EQUAL:
    MOVE.W  #1,D4
    SUBQ.W  #1,D4       ; Set Z=1
    ADDQ.W  #1,D4       ; Set Z=0
    BNE     NOT_EQUAL   ; Branch if D4 is not 0 (Z=0)
    NOP                 ; Should be skipped
NOT_EQUAL:

    ; Test other branches
    BRA     ALWAYS
    NOP                 ; Should be skipped
ALWAYS:
    SUBQ.W  #1,D0       ; D0 = 11, C=0
    BCC     CARRY_CLEAR
    NOP
CARRY_CLEAR:
    SUBI.W  #12,D0      ; D0 = -1, C=1
    BCS     CARRY_SET
    NOP
CARRY_SET:
    MOVE.W  #10,D0
    ADD.W   D0,D0       ; D0 = 20, N=0
    BPL     PLUS
    NOP
PLUS:
    MOVE.W  #-1,D0
    BMI     MINUS
    NOP
MINUS:
    MOVE.W  #10,D0
    MOVE.W  #20,D1
    SUB.W   D1,D0       ; D0 = -10, N=1, V=0
    BGE     GREATER_EQUAL ; (N&&V)||(!N&&!V) -> (1&&0)||(!1&&!0) -> 0
    NOP
GREATER_EQUAL:
    BLT     LESS_THAN   ; (N&&!V)||(!N&&V) -> (1&&1)||(0&&0) -> 1
    NOP
LESS_THAN:
    MOVE.W  #10,D0
    SUBQ.W  #1,D0       ; D0=9, Z=0
    BGT     GREATER_THAN ; (N&&V&&!Z)||(!N&&!V&&!Z) -> (0&&0&&1)||(1&&1&&1) -> 1
    NOP
GREATER_THAN:
    MOVE.W  #6,D0
    SUBQ.W  #6,D0      ; D0=0, Z=1
    BLE     LESS_EQUAL  ; Z || (N&&!V) || (!N&&V) -> 1 || ... -> 1
    NOP
LESS_EQUAL:
    MOVE.W  #$FFFF,D0
    MOVE.W  #1,D1
    SUB.W   D1,D0       ; D0 = $FFFE, C=0, Z=0
    BHI     HIGHER
    NOP
HIGHER:
    MOVE.W  #1,D0
    MOVE.W  #1,D1
    SUB.W   D1,D0       ; D0=0, C=0, Z=1
    BLS     LOWER_SAME
    NOP
LOWER_SAME:
    MOVE.W  #$7FFF,D0
    ADDQ.W  #1,D0       ; D0=$8000, V=1
    BVS     OVERFLOW_SET
    NOP
OVERFLOW_SET:
    MOVE.W  #10,D0
    BVC     OVERFLOW_CLEAR
    NOP
OVERFLOW_CLEAR:

    ; Test Bit Manipulation
    MOVE.L  #$FFFFFFFF,D6
    BTST    #31,D6      ; Z=0
    BCHG    #31,D6      ; D6 = $7FFFFFFF, Z=0
    BCLR    #15,D6      ; D6 = $7FFF7FFF, Z=0
    BSET    #0,D6       ; D6 = $7FFF7FFF | 1 = $7FFF7FFF, Z=0

    RTS
