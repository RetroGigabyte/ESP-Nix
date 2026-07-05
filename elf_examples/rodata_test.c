int test_rodata(int index) {
    const char* msg = "Hello from rodata!";
    return (int)msg[index];
}
