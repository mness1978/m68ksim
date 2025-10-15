* Countdown loop
START:
    MOVE.W #3,D0      ; Initialize counter
LOOP:
    SUBQ.W #1,D0      ; Decrement counter
    BNE LOOP          ; Branch to LOOP if D0 is not zero
    RTS               ; Return
