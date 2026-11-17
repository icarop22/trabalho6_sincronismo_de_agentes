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
#define DESVIO_MS (-3000)

static int L = 0;

/* Cria o ponto de escuta do executor na porta definida. */
int criar_socket_servidor(int porta)
{
    int s;
    struct sockaddr_in endereco;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == -1) {
        perror("socket");
        exit(1);
    }

    memset(&endereco, 0, sizeof endereco);
    endereco.sin_family = AF_INET;
    endereco.sin_port = htons(porta);
    endereco.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(s, (struct sockaddr *)&endereco, sizeof endereco) == -1) {
        perror("bind");
        close(s);
        exit(1);
    }

    return s;
}

/* Mostra o relógio lógico junto com o horário físico deste processo. */
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
           "executor", L, h.tm_hour, h.tm_min, h.tm_sec, ms % 1000, evento);
    fflush(stdout);
}

/* Recebe o pacote, identifica o texto e separa o valor de Lamport enviado. */
int receber_mensagem(int s, char *texto, struct sockaddr_in *orig, socklen_t *tam)
{
    char buf[128];
    int r;
    int ts;
    struct sockaddr_in remetente;
    socklen_t tamanho_remetente = sizeof remetente;
    struct sockaddr_in *destino = orig;
    socklen_t *tamanho = tam;

    /* Quando o chamador não precisa do remetente, usamos variáveis locais. */
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

    /* UDP entrega bytes; por isso colocamos o terminador manualmente. */
    buf[r] = '\0';

    if (sscanf(buf, "%63[^|]|%d", texto, &ts) != 2) {
        fprintf(stderr, "Mensagem recebida em formato invalido: %s\n", buf);
        exit(1);
    }

    return ts;
}

/* Regra 3: considera o maior relógio e avança mais uma unidade. */
void lamport_recebimento(int ts)
{
    if (ts > L) {
        L = ts;
    }
    L++;
}

/* Simula o tempo de processamento de uma chamada ao modelo de linguagem. */
void chamar_llm(void)
{
    struct timespec espera = {0, 300000000L};

    if (nanosleep(&espera, NULL) == -1) {
        perror("nanosleep");
        exit(1);
    }
}

/* Regra 1: um acontecimento interno também faz o relógio avançar. */
void lamport_evento(void)
{
    L++;
}

/* Regra 2: avança o relógio antes de enviar e devolve o valor transmitido. */
int lamport_envio(void)
{
    L++;
    return L;
}

/* Envia a resposta para o endereço de origem informado pelo recvfrom. */
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

int main(void)
{
    int s, ts;
    socklen_t tam;
    struct sockaddr_in cliente;
    char texto[64], msg[64], ev[96];
    int k;

    s = criar_socket_servidor(PORTA);
    registrar("pronto");

    for (k = 1; k <= N_TAREFAS; k++) {
        tam = sizeof cliente;
        ts = receber_mensagem(s, texto, &cliente, &tam);
        lamport_recebimento(ts);

        snprintf(ev, sizeof ev, "RECEBE tarefa %d", k);
        registrar(ev);

        chamar_llm();
        lamport_evento();
        registrar("chamou o LLM");

        snprintf(msg, sizeof msg, "resultado %d", k);
        enviar_mensagem(s, msg, lamport_envio(), &cliente, tam);

        snprintf(ev, sizeof ev, "ENVIA resultado %d", k);
        registrar(ev);
    }

    close(s);
    return 0;
}
