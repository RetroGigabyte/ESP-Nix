static int zeroed_global;

int test_bss(int value) {
    int before = zeroed_global;
    zeroed_global = value;
    return before;
}
