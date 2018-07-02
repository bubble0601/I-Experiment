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

#define N 1024
#define TRUE 1
#define FALSE 0

typedef struct {
    int isServer;           // Work as server if TRUE
    int port;               // Port number
    struct in_addr address; // IP Address to connect

    int play;               // Play received data if TRUE
    char *recfile;          // filename to record sound
    char voice[256];            // speaker

    int fd;                 // file descriptor for communication
    int quit;               // end flag
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
    o->address.s_addr = 0;
    o->play = TRUE;
    o->recfile = NULL;
    strcpy(o->voice, "Kyoko");
    o->fd = -1;
    o->quit = FALSE;
}

void lang_help() {
  printf("Alex                en_US    # Most people recognize me by my voice.\n");
  printf("Alice               it_IT    # Salve, mi chiamo Alice e sono una voce italiana.\n");
  printf("Anna                de_DE    # Hallo, ich heiße Anna und ich bin eine deutsche Stimme.\n");
  printf("Daniel              en_GB    # Hello, my name is Daniel. I am a British-English voice.\n");
  printf("Fred                en_US    # I sure like being inside this fancy computer\n");
  printf("Jorge               es_ES    # Hola, me llamo Jorge y soy una voz española.\n");
  printf("Kyoko               ja_JP    # こんにちは、私の名前はKyokoです。日本語の音声をお届けします。\n");
  printf("Monica              es_ES    # Hola, me llamo Monica y soy una voz española.\n");
  printf("Samantha            en_US    # Hello, my name is Samantha. I am an American-English voice.\n");;
  printf("Thomas              fr_FR    # Bonjour, je m’appelle Thomas. Je suis une voix française.\n");
  printf("Ting-Ting           zh_CN    # 您好，我叫Ting-Ting。我讲中文普通话。\n");
  printf("Victoria            en_US    # Isn't it nice to have a computer that will talk to you?\n");
  printf("Yuna                ko_KR    # 안녕하세요. 제 이름은 Yuna입니다. 저는 한국어 음성입니다.\n");
  printf("Yuri                ru_RU    # Здравствуйте, меня зовут Yuri. Я – русский голос системы.\n");
}

/**
 * getchar non-blocking
 * return pressed key, or EOF if not pressed
 */
char getch() {
    int f = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, f | O_NONBLOCK);
    
    int c = getchar();

    fcntl(STDIN_FILENO, F_SETFL, f & ~O_NONBLOCK);
    return c;
}

int connect_to(struct in_addr address, int port) {
    int s = socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1) die("socket");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = address;

    int status = connect(s, (struct sockaddr*)&addr, sizeof(addr));
    if (status == -1) die("connect");
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

    socklen_t len = sizeof(addr);
    getsockname(ss,(struct sockaddr*)&addr, &len);
    printf("ポート%dで", ntohs(addr.sin_port));

    status = listen(ss, 10);
    if (status == -1) die("listen");

    return ss;
}

int acp(int ss) {
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(struct sockaddr_in);
    int s = accept(ss, (struct sockaddr*)&client_addr, &len);
    if (s == -1) die("accept");
    printf("%s:%dから呼び出し\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);

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

int recv_data(int s, FILE *fp, sox_format_t *ft) {
    char data[N];
    int n = recv(s, data, N, 0);
    if (n == 0) {
        return -1;
    } else if (n == -1) {
        if (errno == EWOULDBLOCK) n = 0;
        perror("recv");
    } else {
        if (ft != NULL) {
            // 16bit, 1 channel -> 32bit, 2 channel
            sox_sample_t buf[N];
            for (int i = 0; i < N / 2; i++) {
                buf[2*i] = ((short*)data)[i] << 16;
                buf[2*i+1] = ((short*)data)[i] << 16;
            }
            sox_write(ft, buf, N);
        }
        if (fp != NULL) {
            fwrite(data, 1, N, fp);
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
        fprintf(stderr, "failed to open sox for read\n");
        o->quit = TRUE;
        return NULL;
    }

    while (!o->quit) {
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
        if (status == -1) o->quit = TRUE;
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
    FILE *fp = NULL;
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
            fprintf(stderr, "failed to open sox for write\n");
            o->quit = TRUE;
            return NULL;
        }
    }
    if (o->recfile != NULL) {
        fp = fopen(o->recfile, "wb");
        if (fp == NULL) {
            fprintf(stderr, "failed to open %s\n", o->recfile);
            o->quit = TRUE;
            return NULL;
        }
    }

    while (!o->quit) {
        status = recv_data(o->fd, fp, ftw);
        if (status == -1) o->quit = TRUE;
    }

    if (ftw != NULL) sox_close(ftw);
    if (fp != NULL) fclose(fp);

    return NULL;
}

void *txt(void* _o) {
    options_t *o = (options_t*)_o;
    int n;
    char data[N];
    char cmd[256];
    sprintf(cmd, "say -v %s", o->voice);
    FILE *say = NULL;
  
    while(!o->quit) {
        n = read(0, data, N);
        if (n == -1) {
            perror("read");
            o->quit = TRUE;
        }
        if (n == 0) o->quit = TRUE;
        say = popen(cmd, "w");
        fprintf(say, "%s\n", data);
        memset(data, '\0', N);
        pclose(say);
    }
    return NULL;
}

void phone(options_t *o) {
    sox_globals_t *g = sox_get_globals();
    g->verbosity = 1;

    int status;
    status = sox_init();
    if (status != SOX_SUCCESS) err("failed to initialize sox");
    status = sox_format_init();
    if (status != SOX_SUCCESS) err("failed to initialize sox format");

    pthread_t tid1, tid2, tid3;
    status = pthread_create(&tid1, NULL, async_send, (void*)o);
    if (status != 0) err(strerror(status));

    status = pthread_create(&tid2, NULL, async_recv, (void*)o);
    if (status != 0) err(strerror(status));

    status = pthread_create(&tid3, NULL, txt, (void*)o);
    if (status != 0) err(strerror(status));

    printf("通話中...\n> ");

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    pthread_join(tid3, NULL);

    sox_format_quit();
    sox_quit();
}

#endif
