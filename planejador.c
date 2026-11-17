#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORTA 20000
#define N_TAREFAS 3
#define L_INICIAL 47
#define DESVIO_MS 0

static int L = L_INICIAL;

/* Prepara o endereço do servidor; o sistema escolhe a porta local do cliente. */
int criar_socket_cliente(int porta, struct sockaddr_in *servidor)
{
    int s;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == -1) {
        perror("socket");
        exit(1);
    }

    memset(servidor, 0, sizeof *servidor);
    servidor->sin_family = AF_INET;
    servidor->sin_port = htons(porta);
    servidor->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    return s;
}

/* Registra o horário físico e o contador lógico do planejador. */
void registrar(const char *evento)
{
    struct timeval tv;
    struct tm h;
    long long ms;
    time_t seg;

    if (gettimeofday(&tv, NULL) == -1) {
        perror("gettimeofday");
        exit(1);
    }

    ms = tv.tv_sec * 1000LL + tv.tv_usec / 1000 + DESVIO_MS;
    seg = (time_t)(ms / 1000);

    if (localtime_r(&seg, &h) == NULL) {
        fprintf(stderr, "Erro ao converter o horario.\n");
        exit(1);
    }

    printf("%-10s L %3d hora %02d:%02d:%02d.%03lld %s\n",
           "planejador", L, h.tm_hour, h.tm_min, h.tm_sec, ms % 1000, evento);
    fflush(stdout);
}

/* Regra 2: incrementa antes do envio e retorna o valor que vai no pacote. */
int lamport_envio(void)
{
    L++;
    return L;
}

/* Monta o pacote com o texto e o relógio lógico, e envia ao servidor. */
void enviar_mensagem(int s, const char *texto, int l,
                     const struct sockaddr_in *dst, socklen_t tam)
{
    char buf[128];
    int n;

    n = snprintf(buf, sizeof buf, "%s|%d", texto, l);
    if (n < 0 || (size_t)n >= sizeof buf) {
        fprintf(stderr, "Erro ao montar a mensagem.\n");
        exit(1);
    }

    if (sendto(s, buf, (size_t)n, 0,
               (const struct sockaddr *)dst, tam) == -1) {
        perror("sendto");
        exit(1);
    }
}

/* Aguarda a resposta e extrai o contador lógico que veio junto dela. */
int receber_mensagem(int s, char *texto, struct sockaddr_in *orig, socklen_t *tam)
{
    char buf[128];
    int r;
    int ts;
    struct sockaddr_in remetente;
    socklen_t tamanho_remetente = sizeof remetente;
    struct sockaddr_in *destino = orig;
    socklen_t *tamanho = tam;

    /* O planejador não usa o endereço de quem respondeu, mas recvfrom pode preenchê-lo. */
    if (destino == NULL) {
        destino = &remetente;
    }
    if (tamanho == NULL) {
        tamanho = &tamanho_remetente;
    }

    r = (int)recvfrom(s, buf, sizeof buf - 1, 0,
                      (struct sockaddr *)destino, tamanho);
    if (r < 0) {
        perror("recvfrom");
        exit(1);
    }

    buf[r] = '\0';

    if (sscanf(buf, "%63[^|]|%d", texto, &ts) != 2) {
        fprintf(stderr, "Mensagem recebida em formato invalido: %s\n", buf);
        exit(1);
    }

    return ts;
}

/* Regra 3: atualiza o relógio usando o valor recebido e soma uma unidade. */
void lamport_recebimento(int ts)
{
    if (ts > L) {
        L = ts;
    }
    L++;
}

int main(void)
{
    int s, ts;
    struct sockaddr_in servidor;
    char texto[64], msg[64], ev[96];
    int k;

    s = criar_socket_cliente(PORTA, &servidor);
    registrar("pronto");

    for (k = 1; k <= N_TAREFAS; k++) {
        snprintf(msg, sizeof msg, "tarefa %d", k);
        enviar_mensagem(s, msg, lamport_envio(), &servidor, sizeof servidor);

        snprintf(ev, sizeof ev, "ENVIA tarefa %d", k);
        registrar(ev);

        ts = receber_mensagem(s, texto, NULL, NULL);
        lamport_recebimento(ts);

        snprintf(ev, sizeof ev, "RECEBE resultado %d", k);
        registrar(ev);
    }

    close(s);
    return 0;
}
