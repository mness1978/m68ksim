    ORG $20000

START:
    ; Test case 1 from test_68020_modes.s
    MOVEA.L #$40000, A0
    MOVEA.L #$50000, A1
    MOVE.L #4, D0
    MOVE.L #8, D1

    MOVE.L #$60000, $5000A
    MOVE.L #$70000, $50014

    ; Test (bd,An,Xn)
    MOVE.W #$1111, (10,A0,D0.W) ; 40000 + 10 + 4 = 40014

    ; Test (bd,An,Xn*scale)
    MOVE.W #$2222, (10,A0,D1.L*4) ; 40000 + 10 + 8*4 = 40032

    ; Test ([bd,An],Xn,od)
    MOVE.W #$3333, ([10,A1],D0.W,20) ; [10+50000] -> [$5000A] -> $60000, $60000 + 4 + 20 = $60024

    ; Test ([bd,An,Xn],od)
    MOVE.W #$4444, ([10,A1],D0.W],20) ; [10+50000+4] -> [$50014] -> $70000, $70000 + 20 = $70020

    ; Test case 2 from test_68020_modes_2.s
    MOVEA.L #$40000, A0
    MOVEA.L #$50000, A1
    MOVE.L #4, D0
    MOVE.L #8, D1

    MOVE.L #$60000, $5000A
    MOVE.L #$70000, $50018

    ; Test (bd,An,Xn*s)
    MOVE.W #$1111, (10,A0,D0.W*2) ; 40000 + 10 + 4*2 = 40018

    ; Test ([bd,An],Xn*s,od)
    MOVE.W #$3333, ([10,A1],D0.W*4,20) ; [10+50000] -> [$5000A] -> $60000, $60000 + 4*4 + 20 = $60030

    ; Test ([bd,An,Xn*s],od)
    MOVE.W #$4444, ([10,A1],D0.W*2],20) ; [10+50000+4*2] -> [$50018] -> $70000, $70000 + 20 = $70020

    ; Test case 3 from test_68020_modes_3.s
    MOVEA.L #$40000,A0
    MOVE.L #4,D0
    MOVE.W #$1111,(10,A0,D0.W*2)

    ; Test case 4 from test_68020_modes_4.s
    MOVEA.L #$40000, A0
    MOVE.L #4, D0
    MOVE.W #$1111, (10,A0,D0.W*2)

    RTS
