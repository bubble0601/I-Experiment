#include "lib.c"

#define SIZE 256

int parse(options_t*);
void serv(options_t*);
void client(options_t*);

int main() {
    options_t o;
    init(&o);
    int showhelp = FALSE;

    system("clear");
    while (TRUE) {
        printf("\n1: 発信\n");
        printf("2: 待ち受け\n");
        printf("3: 終了\n");
        if (showhelp) {
            printf("\nOptions:\n");
            printf("rec <filename>:    指定したファイルに通話内容を録音\n");
            printf("quiet:             再生しない\n");
            printf("voice <name>:      声を指定\n");
            showhelp = FALSE;
        }
        printf("> ");

        int status = parse(&o);
        if (status == 0) break;
        if (status == -1) showhelp = TRUE;
    }

    return 0;
}

int parse(options_t *o) {
    char line[SIZE];
    char *split = " \t\n";
    
    char *result = fgets(line, N, stdin);
    if (result == NULL) return 0;

    char *token = strtok(line, split);
    if (token == NULL) return -1;

    if (strcmp(token, "1") == 0) {
        o->isServer = FALSE;
        client(o);
        return 0;
    } else if (strcmp(token, "2") == 0) {
        o->isServer = TRUE;
        serv(o);
        return 0;
    } else if (strcmp(token, "3") == 0) {
        return 0;
    } else if (strcmp(token, "rec") == 0) {
        if ((token = strtok(NULL, split)) == NULL) return -1;
        o->recfile = token;
        return 1;
    } else if (strcmp(token, "quiet") == 0) {
        o->play = FALSE;
        return 1;
    } else if (strcmp(token, "voice") == 0) {
        if ((token = strtok(NULL, split)) == NULL || strcmp(token, "?") == 0) {
            lang_help();
            return 1;
        }
        o->voice = token;
        return 1;
    }
    return -1;
}

void input_port(options_t *o) {
    while (TRUE) {
        printf("Port: ");
        char line[SIZE];
        fgets(line, N, stdin);
        o->port = atoi(line);
        if (0 <= o->port && o-> port < 65535) break;
        else printf("\e[31mport out of range(0 ~ 65535)\e[m\n");
    }
}

void input_address(options_t *o) {
    while (TRUE) {
        printf("Address: ");
        char line[SIZE];
        fgets(line, N, stdin);
        line[strlen(line) - 1] = '\0';  // 改行削除
        int status = inet_aton(line, &o->address);
        if (status != 0) break;
        else printf("\e[31minvalid format of ipv4 address\e[m\n");
    }
}

void serv(options_t *o) {
    input_port(o);
    
    int ss = listen_to(o->port);
    printf("待ち受け中...\n");
    o->fd = acp(ss);

    printf("応答しますか？ [y/n]: ");
    char res[SIZE];
    fgets(res, SIZE, stdin);
    if (res[0] != 'y') {
        send(o->fd, "n", 1, 0);
        return;
    }
    send(o->fd, "y", 1, 0);

    phone(o);
    close(ss);
}

void client(options_t *o) {
    input_address(o);
    input_port(o);
    
    int s = connect_to(o->address, o->port);
    o->fd = s;

    printf("呼び出し中...\n");
    char res[SIZE];
    recv(o->fd, res, 1, 0);
    if (res[0] != 'y') return;

    phone(o);
    close(s);
}
