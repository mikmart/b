// TODO(Tue Jun 10 21:47:26 BST 2025): Run platform specific tests

__asm__ add(a, b) {
    "add rdi, rsi"
    "mov rax, rdi"
    "ret"
}

__asm__ msg "db \"Hello, World!\", 10, 0"

main() {
    extrn printf;
    printf(&msg);
    printf("34 + 35 = %d\n", add(34, 35));
}
