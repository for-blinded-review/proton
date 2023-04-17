.globl _proton_switch_context
#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )
.type  _proton_switch_context, @function
#endif
_proton_switch_context: //(void** from, void** to)

leaq  -48(%rsp), %rsp
mov %rbx, 40(%rsp);
mov %rbp, 32(%rsp);
mov %r12, 24(%rsp);
mov %r13, 16(%rsp);
mov %r14, 8(%rsp);
mov %r15, 0(%rsp);
#push %rbx;
#push %rbp;
#push %r12;
#push %r13;
#push %r14;
#push %r15;
mov  %rsp, (%rdi);   // rdi is `from`
mov  (%rsi), %rsp;   // rsi is `to`
#pop  %r15;
#pop  %r14;
#pop  %r13;
#pop  %r12;
#pop  %rbp;
#pop  %rbx;
mov 0(%rsp), %r15;
mov 8(%rsp), %r14;
mov 16(%rsp), %r13;
mov 24(%rsp), %r12;
mov 32(%rsp), %rbp;
mov 40(%rsp), %rbx;
leaq 48(%rsp), %rsp
pop     %rax
jmp     *%rax;

.globl _proton_switch_context_new
#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )
.type  _proton_switch_context_new, @function
#endif
_proton_switch_context_new: //(thread* from, thread* to)
mov     %rsp, 16(%rdi);   // rdi is `from`
mov     16(%rsi), %rsp;   // rsi is `to`
pop     %rax
jmp     *%rax;



.globl _proton_die_and_jmp_to_context
#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )
.type  _proton_die_and_jmp_to_context, @function
#endif
_proton_die_and_jmp_to_context: //(void* th, void** c)

mov (%rsi), %r15;   // rsi is `c`
mov %r15, %rsp;
shr $4, %rsp;       // only to make sure that the current
shl $4, %rsp;       // stack pointer is 16-byte aligned
call *%rdx;         // _thread_die() in .cpp
mov %r15, %rsp;
pop %r15;
pop %r14;
pop %r13;
pop %r12;
pop %rbp;
pop %rbx;
ret;

.globl _proton_die_and_jmp_to_context_new
#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )
.type  _proton_die_and_jmp_to_context_new, @function
#endif
_proton_die_and_jmp_to_context_new: //(void* th, void** c)

mov     %rsp, 16(%rdi);   // rdi is `from`
mov     16(%rsi), %rsp;   // rsi is `to`, rsi is context not th
jmp     *%rdx
