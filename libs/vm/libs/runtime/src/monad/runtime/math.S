.text
.intel_syntax noprefix
.globl monad_runtime_mul
.type monad_runtime_mul, @function
.align 16
monad_runtime_mul:
.cfi_startproc
    push    r14
    push    rbx
    mov     r9, qword ptr [rsi]
    mov     r11, qword ptr [rsi + 8]
    mov     r10, qword ptr [rsi + 16]
    mov     rsi, qword ptr [rsi + 24]
    mov     rcx, qword ptr [rdx]
    mov     rax, qword ptr [rdx + 8]
    mov     rbx, qword ptr [rdx + 16]
    mov     r8, qword ptr [rdx + 24]
    mov     rdx, r9
    mulx    rdx, r14, rbx
    imul    r8, r9
    imul    rbx, r11
    add     rbx, r8
    add     rbx, rdx
    mov     rdx, r10
    mulx    rdx, r8, rcx
    imul    r10, rax
    imul    rsi, rcx
    add     rsi, r10
    add     rsi, rdx
    add     r8, r14
    adc     rsi, rbx
    mov     rdx, rcx
    mulx    r10, rbx, r9
    mov     rdx, rax
    mulx    r14, r9, r9
    add     r9, r10
    adc     r14, 0
    mov     rdx, rcx
    mulx    r10, rcx, r11
    add     rcx, r9
    adc     r10, r14
    setb    r9b
    mov     rdx, rax
    mulx    r11, rdx, r11
    movzx   eax, r9b
    add     rdx, r10
    adc     r11, rax
    add     rdx, r8
    adc     r11, rsi
    mov     qword ptr [rdi], rbx
    mov     qword ptr [rdi + 8], rcx
    mov     qword ptr [rdi + 16], rdx
    mov     qword ptr [rdi + 24], r11
    pop     rbx
    pop     r14
    ret
.cfi_endproc
.size monad_runtime_mul, .-monad_runtime_mul

.globl monad_runtime_mul_192
.type monad_runtime_mul_192, @function
.align 16
monad_runtime_mul_192:
.cfi_startproc
    mov     r9, qword ptr [rsi]
    mov     r11, qword ptr [rsi + 8]
    mov     rsi, qword ptr [rsi + 16]
    mov     rcx, qword ptr [rdx]
    mov     rax, qword ptr [rdx + 8]
    mov     r8, qword ptr [rdx + 16]
    mov     rdx, r9
    mulx    rdx, r10, r8
    mov     rdx, rsi
    mulx    rdx, r8, rcx
    add     r8, r10
    imul    rsi, rax
    mov     rdx, rcx
    mulx    rsi, rdx, r9
    mov     qword ptr [rdi], rdx
    mov     rdx, rax
    mulx    r10, r9, r9
    add     r9, rsi
    adc     r10, 0
    mov     rdx, rcx
    mulx    rsi, rcx, r11
    add     rcx, r9
    adc     rsi, r10
    mov     rdx, rax
    mulx    r11, rdx, r11
    add     rdx, rsi
    add     rdx, r8
    mov     qword ptr [rdi + 8], rcx
    mov     qword ptr [rdi + 16], rdx
    ret
.cfi_endproc
.size monad_runtime_mul_192, .-monad_runtime_mul_192

.globl monad_runtime_mul_128
.type monad_runtime_mul_128, @function
.align 16
monad_runtime_mul_128:
.cfi_startproc
    mov     rcx, qword ptr [rsi]
    mov     r10, qword ptr [rdx + 8]
    mov     rdx, qword ptr [rdx]
    mulx    r8, r9, rcx
    imul    rdx, qword ptr [rsi + 8]
    imul    rcx, r10
    add     rcx, rdx
    add     rcx, r8
    mov     qword ptr [rdi], r9
    mov     qword ptr [rdi + 8], rcx
    ret
.cfi_endproc
.size monad_runtime_mul_128, .-monad_runtime_mul_128
.section .note.GNU-stack,"",@progbits
