extern void host_print(int value);

__attribute__((noinline)) static int first_fn(int x) {
    return x + 1;
}

__attribute__((noinline)) static int second_fn(int x) {
    return x * 3;
}

int get_second_fn_address(int a) {
    host_print(first_fn(a));
    return (int)(void*)second_fn;
}
