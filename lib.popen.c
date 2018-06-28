#ifndef LIB_C
#define LIB_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sox.h>
#include <termios.h>
#include "fft.c"

#define TRUE 1
#define FALSE 0

typedef struct {
    int isServer;   // Work as server if TRUE
    int port;       // Port number
    char *address;  // IP Address to connect

    int play;       // Play received data if TRUE
    int f1;         // Lower cutoff frequency
    int f2;         // Upper cutoff frequency
} options_t;

void die(char *s) {
    perror(s);
    exit(1);
}

void err(char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

/**
 * return pressed key, or EOF if not pressed
 */
char getch() {
    struct termios oldt, newt;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    cfmakeraw(&newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);

    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    
    int c = getchar();
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    
    return c;
}

/**
 * parse command line args
 * return -1 if failed to parse
 */
int parse(int argc, char *argv[], options_t *o) {
    if (argc < 2) return -1;
    
    // parse
    // port, address
    if (argc == 2) {
        o->port = atoi(argv[1]);
    } else { // argc > 2
        if (argv[2][0] == '-') {
            o->port = atoi(argv[1]);
        } else {
            o->isServer = FALSE;
            o->address = argv[1];
            o->port = atoi(argv[2]);
        }
    }
    // options
    int i = 2;
    if (!o->isServer) i = 3;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            o->play = TRUE;
        } else if (strcmp(argv[i], "-lf") == 0) {
            if (i + 1 < argc) {
                o->f1 = atoi(argv[++i]);
            } else {
                return -1;
            }
        } else if (strcmp(argv[i], "-uf") == 0) {
            if (i + 1 < argc) {
                o->f2 = atoi(argv[++i]);
            } else {
                return -1;
            }
        } else {
            return -1;
        }
    }
    return 0;
}

int connect_to(char *address, int port) {
    int s = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    int ret = inet_aton(address, &addr.sin_addr);
    if (ret == 0) err("invalid format of ipv4 address");
    addr.sin_port = htons(port);

    ret = connect(s, (struct sockaddr*)&addr, sizeof(addr));
    if (ret != 0) die("connect");
    fprintf(stderr, "connect\n");
    fflush(stdout);
    return s;
}

int listen_to(int port) {
    int ss = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    int ret = bind(ss, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == -1) die("bind");

    ret = listen(ss, 10);
    if (ret == -1) die("listen");
    fprintf(stderr, "listen\n");

    return ss;
}

int acp(int ss) {
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(struct sockaddr_in);
    int s = accept(ss, (struct sockaddr*)&client_addr, &len);
    if (s == -1) die("accept");
    fprintf(stderr, "accept\n");

    return s;
}

int send_data(int s, short *data) {
    int ret = send(s, data, N, 0);
    if (ret == -1) perror("send");
    return ret;
}

int recv_data(int s, FILE *fp) {
    char data[N];
    int n = recv(s, data, N, 0);
    if (n == 0) {
        return -1;
    } else if (n == -1) {
        perror("recv");
    } else {
        if (fp == NULL) {
            fwrite(data, 1, N, stdout);
        } else {
            fwrite(data, 1, N, fp);
        }
    }
    return n;
}

void phone(int s, options_t *o) {
    int ret;
    sox_sample_t raw_data[N];
    short *data = (short*)malloc(N);    // N bytes = N/2 elements

    // Macの内蔵マイクでは2channelにしかできない
    sox_signalinfo_t sig;
    sig.rate = 44100;
    sig.channels = 2;
    sig.precision = 16;
    sig.length = 0;
    sig.mult = NULL;

    sox_format_t *ft = sox_open_read("default", &sig, NULL, "coreaudio");
    if (ft == NULL) {
        fprintf(stderr, "failed to open sox for read");
        return;
    }

    FILE *fp = NULL;
    if (o->play) {
        fp = popen("play -q -t raw -b 16 -c 1 -e s -r 44100 -", "w");
        if (fp == NULL) {
            fprintf(stderr, "failed to open pipe to play");
            return;
        }
    }
    
    sox_init();
    fprintf(stderr, "Press q to quit: ");

    while (TRUE) {
        sox_read(ft, raw_data, N);
        
        // 32bit, 2 channel -> 16bit, 1 channel
        for (int i = 0; i < N / 2; i++) {
            data[i] = (raw_data[2 * i] >> 16);
        }

        // ノイズ除去
        filter(data, o->f1, o->f2);

        ret = send_data(s, data);
        if (ret == -1) break;

        ret = recv_data(s, fp);
        if (ret == -1) break;

        if (getch() == 'q') break;
    }
    sox_close(ft);
    pclose(fp);
    sox_quit();
    free(data);
}

void thr() {
    fork();
}

#endif
