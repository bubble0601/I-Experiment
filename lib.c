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
#include <sys/types.h>
#include <pthread.h>
#include <errno.h>
// #include "fft.c"
#define N 128
#define TRUE 1
#define FALSE 0

typedef struct {
    int isServer;   // Work as server if TRUE
    int port;       // Port number
    char *address;  // IP Address to connect

    int play;       // Play received data if TRUE
    int f1;         // Lower cutoff frequency
    int f2;         // Upper cutoff frequency

    int fd;         // file descriptor for communication
    int quit;       // end flag
} options_t;

void die(char *s) {
    perror(s);
    exit(1);
}

void err(char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

void init(options_t *o) {
    o->isServer = TRUE;
    o->port = -1;
    o->address = NULL;
    o->play = FALSE;
    o->f1 = 100;
    o->f2 = 10000;
    o->fd = -1;
    o->quit = FALSE;
}

/**
 * getchar non-blocking
 * return pressed key, or EOF if not pressed
 */
char getch() {
    int f = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, f | O_NONBLOCK);
    
    int c = getchar();

    fcntl(STDIN_FILENO, F_SETFL, f);
    return c;
}

/**
 * parse command line args
 * return -1 if failed to parse
 */
int parse(int argc, char *argv[], options_t *o) {
    if (argc < 2) return -1;

    init(o);

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
        } else {
            return -1;
        }
    }
    return 0;
}

int connect_to(char *address, int port) {
    int s = socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1) die("socket");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    int status = inet_aton(address, &addr.sin_addr);
    if (status == 0) err("invalid format of ipv4 address");
    addr.sin_port = htons(port);

    status = connect(s, (struct sockaddr*)&addr, sizeof(addr));
    if (status != 0) die("connect");
    fflush(stdout);
    return s;
}

int listen_to(int port) {
    int ss = socket(PF_INET, SOCK_STREAM, 0);
    if (ss == -1) die("socket");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    int status = bind(ss, (struct sockaddr*)&addr, sizeof(addr));
    if (status == -1) die("bind");

    status = listen(ss, 10);
    if (status == -1) die("listen");

    return ss;
}

int acp(int ss) {
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(struct sockaddr_in);
    int s = accept(ss, (struct sockaddr*)&client_addr, &len);
    if (s == -1) die("accept");

    return s;
}

/**
 * 波形を滑らかにする
 * y_n = a * y_(n-1) + (1 - a) * x_n
 */
void rcfilter(short *data, float a) {
    for (int i = 0; i < N / 2; i++) {
        if (i > 0) data[i] = a * data[i-1] + (1 - a) * data[i];
    }
}

int send_data(int s, short *data) {
    int status = send(s, data, N, MSG_DONTWAIT);
    if (status == -1) perror("send");
    return status;
}

int recv_data(int s, sox_format_t *ft) {
    char data[N];
    int n = recv(s, data, N, 0);
    if (n == 0) {
        return -1;
    } else if (n == -1) {
        if (errno == EWOULDBLOCK) n = 0;
        perror("recv");
    } else {
        if (ft == NULL) {
            fwrite(data, 1, N, stdout);
        } else {
            // 16bit, 1 channel -> 32bit, 2 channel
            sox_sample_t buf[N];
            for (int i = 0; i < N / 2; i++) {
                buf[2*i] = ((short*)data)[i] << 16;
                buf[2*i+1] = ((short*)data)[i] << 16;
            }
            sox_write(ft, buf, N);
        }
    }
    return n;
}

void* async_send(void* _o) {
    options_t *o = (options_t*)_o;

    int status;
    sox_sample_t raw_data[N];
    short *data = (short*)malloc(N);    // N bytes = N/2 elements(short)

    // Macの内蔵マイクでは2channelにしかできない
    // precisionに関わらずsox_readで得られるデータは32bit
    sox_signalinfo_t sig;
    sig.rate = 44100;
    sig.channels = 2;
    sig.precision = 32;
    sig.length = 0;
    sig.mult = NULL;

    #ifdef __APPLE__
    sox_format_t *ftr = sox_open_read("default", &sig, NULL, "coreaudio");
    #else
    sox_format_t *ftr = sox_open_read("default", &sig, NULL, "pulseaudio");
    #endif
    
    if (ftr == NULL) {
        fprintf(stderr, "failed to open sox for read");
        return NULL;
    }

    while (TRUE) {
        sox_read(ftr, raw_data, N);
        
        // 32bit, 2 channel -> 16bit, 1 channel
        for (int i = 0; i < N / 2; i++) {
            data[i] = (raw_data[2 * i] >> 16);
        }

        // ノイズ除去(Macの内蔵マイクには環境ノイズリダクション機能があるので不要)
        #ifndef __APPLE__
        rcfilter(data, 0.6f);
        #endif

        status = send_data(o->fd, data);
        if (status == -1) break;

        if (getch() == 'q' || o->quit) break;
    }
    o->quit = TRUE;

    sox_close(ftr);
    free(data);

    return NULL;
}

void* async_recv(void* _o) {
    options_t *o = (options_t*)_o;

    int status;

    sox_format_t *ftw = NULL;
    if (o->play) {
        // なぜか1channelでうまく再生できない
        sox_signalinfo_t sig;
        sig.rate = 44100;
        sig.channels = 2;
        sig.precision = 32;
        sig.length = 0;
        sig.mult = NULL;

        #ifdef __APPLE__
        ftw = sox_open_write("default", &sig, NULL, "coreaudio", NULL, NULL);
        #else
        ftw = sox_open_write("default", &sig, NULL, "pulseaudio", NULL, NULL);
        #endif

        if (ftw == NULL) {
            fprintf(stderr, "failed to open sox for write");
            return NULL;
        }
    }

    while (TRUE) {
        status = recv_data(o->fd, ftw);
        if (status == -1) break;

        if (getch() == 'q' || o->quit) break;
    }
    o->quit = TRUE;

    if (ftw != NULL) sox_close(ftw);

    return NULL;
}

void phone(options_t *o) {
    int status;
    status = sox_init();
    if (status != SOX_SUCCESS) err("failed to initialize sox");
    status = sox_format_init();
    if (status != SOX_SUCCESS) err("failed to initialize sox format");

    pthread_t tid1, tid2;
    status = pthread_create(&tid1, NULL, async_send, (void*)o);
    if (status != 0) err(strerror(status));

    status = pthread_create(&tid2, NULL, async_recv, (void*)o);
    if (status != 0) err(strerror(status));

    fprintf(stderr, "Put q to quit: ");

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    sox_format_quit();
    sox_quit();
}

#endif
