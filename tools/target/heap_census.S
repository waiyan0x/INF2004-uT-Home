/* Diagnostic only. Keep the original arguments and return address intact. */
.section .iram1,"ax",@progbits
.align 2
.global census_alloc
.global __wrap_c3audit_alloc
__wrap_c3audit_alloc:
census_alloc:
    la t1, census_seal
    lw t1, 0(t1)
    beqz t1, 9f
    lbu t1, 0(t1)
    beqz t1, 9f
    beqz a0, 9f
    csrrci t0, mstatus, 8
    mv t2, ra
    mv t3, sp
    li t6, 16
/* Walk only exact compiler-verified return sites. Stop at their first owner. */
1:  la t1, census_rules
2:  lw t4, 0(t1)
    beqz t4, 4f
    beq t4, t2, 3f
    addi t1,t1,12
    j 2b
3:  lw t4,4(t1)
    add t4,t3,t4
    lw t2,0(t4)
    lw t4,8(t1)
    add t3,t3,t4
    addi t6,t6,-1
    bnez t6,1b
    li t2,1
/* Site entries hold return PC, count, requested bytes, live bytes and peak. */
4:  la t1,census_sites
    li t5,48
5:  lw t4,0(t1)
    beq t4,t2,6f
    beqz t4,6f
    addi t1,t1,20
    addi t5,t5,-1
    bnez t5,5b
    la t1,census_over
    lw t4,0(t1)
    addi t4,t4,1
    sw t4,0(t1)
    j 8f
6:  sw t2,0(t1)
    lw t4,4(t1)
    addi t4,t4,1
    sw t4,4(t1)
    lw t4,8(t1)
    add t4,t4,a1
    sw t4,8(t1)
    /* Associate each live pointer with its site and size until free. */
    la t3,census_live
    li t5,96
7:  lw t4,0(t3)
    beqz t4,10f
    addi t3,t3,12
    addi t5,t5,-1
    bnez t5,7b
    la t3,census_over
    lw t4,4(t3)
    addi t4,t4,1
    sw t4,4(t3)
    j 8f
10: sw a0,0(t3)
    sw t1,4(t3)
    sw a1,8(t3)
    lw t4,12(t1)
    add t4,t4,a1
    sw t4,12(t1)
    lw t5,16(t1)
    bgeu t5,t4,8f
    sw t4,16(t1)
8:  andi t0,t0,8
    csrs mstatus,t0
9:  tail __real_c3audit_alloc
/* Ignore frees of storage allocated before the measurement boundary. */
.global census_free
.global __wrap_c3audit_free
__wrap_c3audit_free:
census_free:
    la t1,census_seal
    lw t1,0(t1)
    beqz t1,9f
    lbu t1,0(t1)
    beqz t1,9f
    beqz a0,9f
    csrrci t0,mstatus,8
    la t1,census_live
    li t2,96
1:  lw t3,0(t1)
    beq t3,a0,2f
    addi t1,t1,12
    addi t2,t2,-1
    bnez t2,1b
    j 8f
2:  lw t2,4(t1)
    lw t3,8(t1)
    lw t4,12(t2)
    sub t4,t4,t3
    sw t4,12(t2)
    sw zero,0(t1)
8:  andi t0,t0,8
    csrs mstatus,t0
9:  tail __real_c3audit_free
.section .bss,"aw",@nobits
.align 2
.global census_rules,census_sites,census_live,census_seal,census_over
census_rules: .space 12*64
census_sites: .space 20*48
census_live: .space 12*96
census_seal: .space 4
census_over: .space 8
