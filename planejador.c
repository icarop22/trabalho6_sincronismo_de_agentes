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

/* cria o socket udp e monta o endereço do servidor, a porta do cliente o sistema escolhe sozinho */
int criar_socket_cliente(int porta, struct sockaddr_in *servidor)
{
    int s;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == -1) {
        perror("socket");
        exit(1);
    }

    /* zera a struct antes de preencher pra não sobrar lixo */
    memset(servidor, 0, sizeof *servidor);
    servidor->sin_family = AF_INET;
    servidor->sin_port = htons(porta);
    servidor->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    return s;
}

/* imprime o que aconteceu junto com o valor de L e a hora */
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

    /* passa tudo pra milissegundo e soma o desvio */
    ms = tv.tv_sec * 1000LL + tv.tv_usec / 1000 + DESVIO_MS;
    seg = (time_t)(ms / 1000);

    if (localtime_r(&seg, &h) == NULL) {
        fprintf(stderr, "deu erro ao converter a hora\n");
        exit(1);
    }

    printf("%-10s L %3d hora %02d:%02d:%02d.%03lld %s\n",
           "planejador", L, h.tm_hour, h.tm_min, h.tm_sec, ms % 1000, evento);
    fflush(stdout);
}

/* regra 2: soma 1 antes de mandar e devolve o L pra ir junto na mensagem */
int lamport_envio(void)
{
    L++;
    return L;
}

/* junta texto e L num pacote só e manda pro servidor */
void enviar_mensagem(int s, const char *texto, int l,
                     const struct sockaddr_in *dst, socklen_t tam)
{
    char buf[128];
    int n;

    n = snprintf(buf, sizeof buf, "%s|%d", texto, l);
    if (n < 0 || (size_t)n >= sizeof buf) {
        fprintf(stderr, "deu erro ao montar a mensagem\n");
        exit(1);
    }

    /* manda o pacote pro endereço do servidor */
    if (sendto(s, buf, (size_t)n, 0,
               (const struct sockaddr *)dst, tam) == -1) {
        perror("sendto");
        exit(1);
    }
}

/* espera a resposta chegar e separa o texto do número do lamport */
int receber_mensagem(int s, char *texto, struct sockaddr_in *orig, socklen_t *tam)
{
    char buf[128];
    int r;
    int ts;
    struct sockaddr_in remetente;
    socklen_t tamanho_remetente = sizeof remetente;
    struct sockaddr_in *destino = orig;
    socklen_t *tamanho = tam;

    /* aqui eu não preciso saber quem respondeu, mas o recvfrom quer um lugar pra guardar */
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

    /* coloco o \0 na mão porque o udp não manda o fim da string */
    buf[r] = '\0';

    /* o formato é texto|numero, pego o texto até a barra e depois o número */
    if (sscanf(buf, "%63[^|]|%d", texto, &ts) != 2) {
        fprintf(stderr, "mensagem veio num formato estranho: %s\n", buf);
        exit(1);
    }

    return ts;
}

/* regra 3: fico com o maior entre o meu L e o que chegou, e somo 1 */
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

        snprintf(ev, sizeof ev, "enviou tarefa %d", k);
        registrar(ev);

        ts = receber_mensagem(s, texto, NULL, NULL);
        lamport_recebimento(ts);

        snprintf(ev, sizeof ev, "recebeu resultado %d", k);
        registrar(ev);
    }

    close(s);
    return 0;
}
