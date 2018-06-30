#include "lib.c"

void print_help();
void serv(options_t*);
void client(options_t*);

int main(int argc, char *argv[]) {
    options_t o;
    int status = parse(argc, argv, &o);

    if (status == -1) {
        print_help(argv[0]);
    } else if (o.isServer) {
        serv(&o);
    } else {
        client(&o);
    }

    return 0;
}

void print_help(char *filename) {
    fprintf(stderr, "Usage as server:\n");
    fprintf(stderr, "    %s <Port> [options]\n", filename);
    fprintf(stderr, "Usage as client:\n");
    fprintf(stderr, "    %s <IP Address> <Port> [options]\n", filename);
    fprintf(stderr, "\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "  -p            output received data to sound device (default: output to stdout)\n");
}

void serv(options_t *o) {
    int ss = listen_to(o->port);
    o->fd = acp(ss);
    phone(o);
    close(ss);
}

void client(options_t *o) {
    int s = connect_to(o->address, o->port);
    o->fd = s;
    phone(o);
    close(s);
}
