.text
.globl monad_runtime_exit
.type monad_runtime_exit, @function
.align 16
monad_runtime_exit:
.cfi_startproc
    movq %rdi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    retq
.cfi_endproc
.size monad_runtime_exit, .-monad_runtime_exit
.section .note.GNU-stack,"",@progbits
