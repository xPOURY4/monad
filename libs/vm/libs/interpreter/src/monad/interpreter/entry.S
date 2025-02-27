// This function acts as a trampoline to make the interpreter's core loop
// compatible with the existing runtime exit mechanism. It sets up the stack and
// preserved registers so that `monad_runtime_exit` can clean them up in the
// same way as it does for compiled code.
.text
.globl interpreter_core_loop
.globl interpreter_runtime_trampoline
.type interpreter_runtime_trampoline, @function
.align 16
interpreter_runtime_trampoline:
.cfi_startproc
    pushq %rbp
    pushq %rbx    
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    jmp interpreter_core_loop
.cfi_endproc
.size interpreter_runtime_trampoline, .-interpreter_runtime_trampoline
.section .note.GNU-stack,"",@progbits
