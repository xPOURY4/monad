.text
.globl monad_runtime_mul
.type monad_runtime_mul, @function
.align 16
.intel_syntax noprefix 
monad_runtime_mul:
.cfi_startproc
    push    r14
    push    rbx
    mov     r10, qword ptr [rsi + 16]
    mov     r9, qword ptr [rsi]
    mov     r11, qword ptr [rsi + 8]
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
    mov     rsi, qword ptr [rsi + 24]
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
    mov     rax, rdi
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
.section .note.GNU-stack,"",@progbits
