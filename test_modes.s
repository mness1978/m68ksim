; test_modes.s
    ORG    $20000

    MOVE.L  #$40000,A0   ; Point A0 to some memory
    MOVE.L  #$40010,A1
    MOVE.L  #$40020,A2
    MOVE.L  #$40030,A3

    ; Test (An)
    MOVE.W  #$1234,D0
    MOVE.W  D0,(A0)      ; Write 1234 to address $40000
    MOVE.W  (A0),D1      ; Read from $40000 into D1. D1 should be $1234

    ; Test (An)+
    MOVE.W  #$5678,(A1)+ ; Write 5678 to $40010, A1 should become $40012
    MOVE.W  (A1),D2      ; Read from $40012 into D2 (should be garbage)

    ; Test -(An)
    MOVE.W  #$ABCD,-(A2) ; A2 becomes $4001E, write ABCD to $4001E

    ; Test d(An)
    MOVE.W  #$BEEF,10(A3); Write BEEF to $40030 + 10 = $4003A
    MOVE.W  10(A3),D3    ; Read from $4003A into D3. D3 should be $BEEF

    ; Test Address Register Indirect with Index and Displacement
    MOVE.L #10,D1
    MOVE.W #1, 2(A0,D1.W)  ; Write 1 to $40000 + 10 + 2 = $4000C

    ; Test Absolute Short
    MOVE.W #2, ($8000).W    ; Write 2 to address $8000

    ; Test Absolute Long
    MOVE.L #3, ($9000).L    ; Write 3 to address $9000

    ; Test PC with Displacement
    ; Note: PC is address of instruction + 2
    ; Instruction is at $20032, so PC is $20034
    ; Target is $20034 + 10 = $2003E
    MOVE.W #4, 10(PC)      ; Write 4 to $2003E

    ; Test PC with Index and Displacement
    ; Instruction is at $20036, PC is $20038
    ; Target is $20038 + 20 + 4 = $2004C
    MOVE.L #20,D2
    MOVE.W #5, 4(PC,D2.W)  ; Write 5 to $2004C

    RTS
