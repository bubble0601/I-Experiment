#include <stdio.h>

#define N 256
#define std_out 1

// 16bitのrawファイルをgnuplotでプロットできるように
int main() {
    int n;
    int m = 1;
    short buf[N];
    while (m < 10000) {
        n = fread(buf, 2, N, stdin);
        if (n == 0) break;
        for (int i = 0; i < n; i++) {
            printf("%d %d\n", m++, buf[i]);
        }
    }
}
