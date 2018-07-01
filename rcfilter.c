#include <stdio.h>
#include <stdlib.h>

#define N 256

void rcfilter(short *data, int n, float a, int th) {
    static int silence = 1;
    static int s_count = 0;
    th = 300;
    for (int i = 0; i < n; i++) {
        if (silence) {
            if (abs(data[i]) < th) {
                data[i] = 0;
            } else {
                // fprintf(stderr, "\e[G%d\n", data[i]);
                silence = 0;
                if (i > 0) data[i] = a * data[i-1] + (1 - a) * data[i];
            }
        } else {
            if (s_count > 20) {
                silence = 1;
                s_count = 0;
                data[i] = 0;
            } else {
                if (abs(data[i]) < th) s_count++;
                else s_count = 0;
                if (i > 0) data[i] = a * data[i-1] + (1 - a) * data[i];
            }
        }
        if (i == 0) fprintf(stderr, "\e[2K\e[G%d", data[i]);
    }
}

int main(int argc, char *argv[]) {
    int n;
    short buf[N];
    float a = 0.8f;
    int th = 0;
    if (argc >= 2) a = atof(argv[1]);
    if (argc >= 3) th = atoi(argv[2]);
    while (1) {
        n = fread(buf, 2, N, stdin);
        if (n == -1) { fprintf(stderr, "error"); break; }
        if (n == 0) break;
        rcfilter(buf, n, a, th);
        fwrite(buf, 2, n, stdout);
    }
}
