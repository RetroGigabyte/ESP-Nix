extern void host_print(int value);

__attribute__((noinline)) static int square(int x) {
    return x * x;
}

int sum_of_squares(int a, int b) {
    int result = square(a) + square(b);
    host_print(result);
    return result;
}
