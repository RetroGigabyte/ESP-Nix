extern void* malloc(unsigned int size);
extern void free(void* ptr);

int test_malloc(int value) {
    int* buf = (int*)malloc(sizeof(int));
    if (!buf) return -1;
    *buf = value * 2;
    int result = *buf;
    free(buf);
    return result;
}
