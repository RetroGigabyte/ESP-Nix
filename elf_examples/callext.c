extern void host_print(int value);

int run_with_callback(int a, int b) {
    int sum = a + b;
    host_print(sum);
    return sum;
}
