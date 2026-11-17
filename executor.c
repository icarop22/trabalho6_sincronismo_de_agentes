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

/* cria o socket udp e prende ele na porta pra ficar esperando mensagem */
int criar_socket_servidor(int porta)
{
    int s;
    struct sockaddr_in endereco;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == -1) {
        perror("socket");
        exit(1);
    }

    /* zera tudo antes pra não ficar lixo na struct */
    memset(&endereco, 0, sizeof endereco);
    endereco.sin_family = AF_INET;
    endereco.sin_port = htons(porta);
    endereco.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    /* o bind é o que liga o socket na porta, sem ele ninguém consegue falar com o executor */
    if (bind(s, (struct sockaddr *)&endereco, sizeof endereco) == -1) {
        perror("bind");
        close(s);
        exit(1);
    }

    return s;
}

/* imprime o que aconteceu junto com o valor de L e a hora (com o desvio somado) */
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

    /* passa tudo pra milissegundo e já soma o desvio do relógio */
    ms = tv.tv_sec * 1000LL + tv.tv_usec / 1000 + DESVIO_MS;
    seg = (time_t)(ms / 1000);

    if (localtime_r(&seg, &h) == NULL) {
        fprintf(stderr, "deu erro ao converter a hora\n");
        exit(1);
    }

    printf("%-10s L %3d hora %02d:%02d:%02d.%03lld %s\n",
           "executor", L, h.tm_hour, h.tm_min, h.tm_sec, ms % 1000, evento);
    fflush(stdout);
}

/* espera chegar um pacote, guarda quem mandou e separa o texto do número do lamport */
int receber_mensagem(int s, char *texto, struct sockaddr_in *orig, socklen_t *tam)
{
    char buf[128];
    int r;
    int ts;
    struct sockaddr_in remetente;
    socklen_t tamanho_remetente = sizeof remetente;
    struct sockaddr_in *destino = orig;
    socklen_t *tamanho = tam;

    /* se quem chamou não quer saber o remetente, uso uma variável daqui mesmo */
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

    /* o udp não manda o fim da string, então coloco o \0 na mão */
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

/* só segura 300 ms pra fingir que tá demorando pra falar com o llm */
void chamar_llm(void)
{
    struct timespec espera = {0, 300000000L};

    if (nanosleep(&espera, NULL) == -1) {
        perror("nanosleep");
        exit(1);
    }
}

/* regra 1: qualquer coisa que acontece só dentro do processo soma 1 no L */
void lamport_evento(void)
{
    L++;
}

/* regra 2: soma 1 antes de mandar e devolve o L pra ir junto na mensagem */
int lamport_envio(void)
{
    L++;
    return L;
}

/* junta texto e L num pacote só e manda de volta pra quem pediu */
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

    /* o sendto já recebe o endereço de destino, que é o que o recvfrom guardou */
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

        snprintf(ev, sizeof ev, "recebeu tarefa %d", k);
        registrar(ev);

        chamar_llm();
        lamport_evento();
        registrar("chamou o llm");

        /* respondo pro endereço de quem mandou, não pra porta fixa */
        snprintf(msg, sizeof msg, "resultado %d", k);
        enviar_mensagem(s, msg, lamport_envio(), &cliente, tam);

        snprintf(ev, sizeof ev, "enviou resultado %d", k);
        registrar(ev);
    }

    close(s);
    return 0;
}