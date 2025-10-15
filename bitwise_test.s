; bitwise_test.s
; A test program for the 68k simulator

    ORG    $10000

START:
    BTST    #31,D6
    RTS
