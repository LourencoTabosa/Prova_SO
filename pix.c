/* A01: N remetentes enviam 1000 centavos para a conta 0. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX 100
#define POOL 2

int n, proteger, tempo = 500;
int saldo[MAX + 1], fila[MAX], proxima, concluidas, atual[POOL];
pthread_mutex_t banco = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t controle = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t debug = 0;

void pausa(int ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) == -1 && errno == EINTR) {}
}

void receber_sinal(int sinal) {
    (void)sinal;
#ifdef _WIN32
    signal(SIGINT, receber_sinal);
#endif
    debug = 1; /* A impressao sera feita pela main, fora do handler. */
}

/* Chamada somente com o mutex controle adquirido. */
void mostrar_fila(void) {
    printf("\n--- DEBUG ---\nFila FCFS:");
    for (int i = proxima; i < n; i++) printf(" R%d", fila[i]);
    if (proxima == n) printf(" vazia");
    for (int i = 0; i < POOL; i++)
        printf("\nT%d: R%d (R0 = livre)", i + 1, atual[i]);
    printf("\nConcluidas: %d/%d\n-------------\n", concluidas, n);
}

void transferir(int origem) {
    pthread_mutex_lock(&banco);
    int antigo = saldo[0];
    /* Modo 0: a transacao fica desprotegida entre ler e gravar. */
    if (!proteger) pthread_mutex_unlock(&banco);
    pausa(10);
    if (!proteger) pthread_mutex_lock(&banco);
    saldo[origem] -= 1000;
    saldo[0] = antigo + 1000;
    pthread_mutex_unlock(&banco);
}

void *trabalhar(void *arg) {
    int id = *(int *)arg;
    while (1) {
        pthread_mutex_lock(&controle);
        if (proxima == n) {
            pthread_mutex_unlock(&controle);
            break; /* O lote terminou: nao chegarao novas requisicoes. */
        }
        int r = fila[proxima++]; /* FCFS: retirar sempre a mais antiga. */
        atual[id] = r;
        printf("INICIO R%d em T%d\n", r, id + 1);
        pthread_mutex_unlock(&controle);

        pausa(tempo); /* Simula atendimento fora da secao critica. */
        transferir(r);

        pthread_mutex_lock(&controle);
        atual[id] = 0;
        concluidas++;
        printf("FIM R%d em T%d\n", r, id + 1);
        pthread_mutex_unlock(&controle);
    }
    return NULL;
}

int numero(const char *texto, int minimo, int maximo) {
    char *fim;
    errno = 0;
    long valor = strtol(texto, &fim, 10);
    if (errno || fim == texto || *fim || valor < minimo || valor > maximo) {
        fprintf(stderr, "Valor invalido: %s (use %d..%d)\n", texto, minimo, maximo);
        exit(1);
    }
    return (int)valor;
}

int main(int argc, char **argv) {
    pthread_t threads[POOL];
    int ids[POOL], total = 0;
    if (argc != 3 && argc != 4) {
        printf("Uso: %s N PROTEGER [TEMPO_MS]\n", argv[0]);
        printf("N=1..100; PROTEGER=0 ou 1; TEMPO_MS=0..60000 (padrao 500).\n");
        return 1;
    }
    n = numero(argv[1], 1, MAX);
    proteger = numero(argv[2], 0, 1);
    if (argc == 4) tempo = numero(argv[3], 0, 60000);
    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef _WIN32
    if (signal(SIGINT, receber_sinal) == SIG_ERR) return 1;
#else
    struct sigaction acao = {0};
    sigset_t bloqueio, anterior;
    acao.sa_handler = receber_sinal;
    sigemptyset(&acao.sa_mask);
    if (sigaction(SIGINT, &acao, NULL) != 0) return 1;
    sigemptyset(&bloqueio);
    sigaddset(&bloqueio, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &bloqueio, &anterior) != 0) return 1;
#endif
    for (int i = 0; i < n; i++) {
        fila[i] = i + 1;
        saldo[i + 1] = 10000;
    }
    printf("N=%d | pool=%d | protegido=%d | Ctrl+C mostra a fila\n", n, POOL, proteger);
    pthread_mutex_lock(&controle);
    for (int i = 0; i < POOL; i++) {
        ids[i] = i;
        if (pthread_create(&threads[i], NULL, trabalhar, &ids[i]) != 0) {
            fprintf(stderr, "Nao foi possivel criar a pool.\n");
            exit(1);
        }
    }
    pthread_mutex_unlock(&controle);
#ifndef _WIN32
    /* As trabalhadoras herdam SIGINT bloqueado; a main pode recebe-lo. */
    if (pthread_sigmask(SIG_SETMASK, &anterior, NULL) != 0) return 1;
#endif
    while (1) {
        pthread_mutex_lock(&controle);
        if (debug) { debug = 0; mostrar_fila(); }
        int terminou = concluidas == n;
        pthread_mutex_unlock(&controle);
        if (terminou) break;
        pausa(20);
    }
    for (int i = 0; i < POOL; i++)
        if (pthread_join(threads[i], NULL) != 0) return 1;
    for (int i = 0; i <= n; i++) total += saldo[i];
    printf("Destino: esperado=%d recebido=%d centavos\n", n * 1000, saldo[0]);
    printf("Total: antes=%d depois=%d centavos\n", n * 10000, total);
    pthread_mutex_destroy(&banco);
    pthread_mutex_destroy(&controle);
    return proteger && (saldo[0] != n * 1000 || total != n * 10000);
}
