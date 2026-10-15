/*
 * pix.c - INF0336 (Sistemas Operacionais) - A01: Emulando o Sistema PIX
 *
 * Grupo:
 *   Andre de Oliveira Machado Filho   (202406913)
 *   Jamilly Vitorya da Silva Barbosa  (202103736)
 *   Alan Queiroz Dantas de Oliveira   (202109882)
 *
 * Programa unico com modos por linha de comando:
 *   ./pix --passo-a-passo    (P1) corrida deterministica com 2 threads
 *   ./pix --sem-mutex  N     (P1) N envios SEM protecao
 *   ./pix --com-mutex  N     (P1) N envios COM mutex
 *   ./pix --demo             (P1) varre N e mede a corrida
 *   ./pix --fcfs  P N        (P2) pool de P workers + fila FCFS (em breve)
 *   ./pix                    (P3) sistema completo + debug por sinais (em breve)
 *
 * Compilar: gcc -O2 -o pix pix.c -pthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>

/* --- base de contas (modelo N:1): N envios creditam 1 conta central --- */
#define POOL_WORKERS    8L      /* tamanho fixo da thread pool */
#define SALDO_INICIAL  1000000L /* saldo inicial de cada conta de origem */
#define VALOR_ENVIO       1L    /* cada envio transfere 1 unidade */

static long  saldo_central;         /* recurso compartilhado (disputado) */
static long  origem[POOL_WORKERS];  /* conta de cada worker (sem disputa) */
static int   cur_W;                 /* workers usados na rodada atual */

static pthread_mutex_t mutex_central = PTHREAD_MUTEX_INITIALIZER;
static int usar_mutex = 0;           /* 1 protege a secao critica */

/* portao de partida: libera todos os workers de uma vez (maximiza a disputa) */
static pthread_mutex_t gate_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  gate_cv  = PTHREAD_COND_INITIALIZER;
static int liberado = 0;

/* amplia a janela entre ler e escrever o saldo (revela a corrida) */
static volatile long lixo_corrida;
static void trabalho_na_secao(void)
{
    for (int i = 0; i < 200; i++)
        lixo_corrida += i;
}

/* uma transferencia: debita a origem e credita a central (read-modify-write) */
static void transferir(int id)
{
    origem[id] -= VALOR_ENVIO;

    /* >>> secao critica: RMW do saldo central <<< */
    if (usar_mutex) pthread_mutex_lock(&mutex_central);
    long tmp = saldo_central;        /* (1) le */
    trabalho_na_secao();
    tmp = tmp + VALOR_ENVIO;         /* (2) modifica */
    saldo_central = tmp;             /* (3) escreve */
    if (usar_mutex) pthread_mutex_unlock(&mutex_central);
}

/* ===================== PARTE 1 - SINCRONISMO ===================== */

typedef struct { int id; long envios; } WorkerArg;

static void *worker_envio(void *arg)
{
    WorkerArg *w = (WorkerArg *)arg;

    pthread_mutex_lock(&gate_mtx);
    while (!liberado)
        pthread_cond_wait(&gate_cv, &gate_mtx);
    pthread_mutex_unlock(&gate_mtx);

    for (long k = 0; k < w->envios; k++)
        transferir(w->id);
    return NULL;
}

/* soma de todas as contas: invariante (deve ser constante) */
static long total_do_sistema(void)
{
    long total = saldo_central;
    for (int i = 0; i < cur_W; i++)
        total += origem[i];
    return total;
}

/* roda N envios concorrentes por uma thread pool fixa; devolve o saldo central */
static long roda_rodada(int n)
{
    int W = (n < (int)POOL_WORKERS) ? n : (int)POOL_WORKERS;
    cur_W = W;
    saldo_central = 0;
    liberado = 0;

    pthread_t tids[POOL_WORKERS];
    WorkerArg args[POOL_WORKERS];
    for (int i = 0; i < W; i++) {
        origem[i]      = SALDO_INICIAL;
        args[i].id     = i;
        args[i].envios = n / W + (i < n % W ? 1 : 0);  /* distribui N entre W */
        pthread_create(&tids[i], NULL, worker_envio, &args[i]);
    }

    pthread_mutex_lock(&gate_mtx);         /* largada */
    liberado = 1;
    pthread_cond_broadcast(&gate_cv);
    pthread_mutex_unlock(&gate_mtx);

    for (int i = 0; i < W; i++)
        pthread_join(tids[i], NULL);
    return saldo_central;
}

/* --sem-mutex N / --com-mutex N */
static int modo_rodada_unica(int n, int com_mutex)
{
    usar_mutex = com_mutex;

    long obtido       = roda_rodada(n);
    long total_antes  = (long)cur_W * SALDO_INICIAL;
    long esperado     = (long)n * VALOR_ENVIO;
    long perdidas     = esperado - obtido;
    long total_depois = total_do_sistema();

    printf("\n=== PARTE 1: %d envios para a conta central (modelo N:1) ===\n", n);
    printf("  Thread pool (workers)     : %d\n", cur_W);
    printf("  Protecao da secao critica : %s\n",
           com_mutex ? "COM mutex (pthread_mutex)" : "SEM mutex");
    printf("  Saldo central ESPERADO    : %ld\n", esperado);
    printf("  Saldo central OBTIDO      : %ld\n", obtido);
    printf("  Atualizacoes PERDIDAS     : %ld  (%.1f%% dos envios)\n",
           perdidas, esperado ? 100.0 * perdidas / esperado : 0.0);
    printf("  Total ANTES  (soma contas): %ld\n", total_antes);
    printf("  Total DEPOIS (soma contas): %ld\n", total_depois);
    printf("  Dinheiro que SUMIU        : %ld\n", total_antes - total_depois);
    printf(perdidas == 0
           ? "  => OK: nenhuma atualizacao perdida, total conservado.\n"
           : "  => CORRIDA: creditos sobrescritos; o total nao se conservou.\n");
    return perdidas != 0;
}

/* --demo: varre N e mede, empiricamente, a partir de quando a corrida aparece */
static void modo_demo(void)
{
    int ns[]  = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512,
                 1000, 2000, 5000, 10000, 50000, 100000};
    int qtd   = (int)(sizeof(ns) / sizeof(ns[0]));
    const int REPS = 15;

    printf("\n=== PARTE 1: demonstracao empirica da condicao de corrida ===\n");
    printf("Para cada N: %d rodadas SEM mutex (cada envio = 1 unidade).\n\n", REPS);
    printf("     N | esperado |  perdidas(media) | erro medio | rodadas c/ erro\n");
    printf("-------+----------+------------------+------------+----------------\n");

    usar_mutex = 0;
    for (int k = 0; k < qtd; k++) {
        int  n = ns[k];
        long soma_perdidas = 0;
        int  rodadas_com_erro = 0;
        for (int r = 0; r < REPS; r++) {
            long perdidas = (long)n * VALOR_ENVIO - roda_rodada(n);
            soma_perdidas += perdidas;
            if (perdidas != 0) rodadas_com_erro++;
        }
        double media = (double)soma_perdidas / REPS;
        printf("%6d | %8d | %16.1f | %8.2f%% | %6d / %d\n",
               n, n, media, 100.0 * media / n, rodadas_com_erro, REPS);
    }
    printf("\nN pequeno: a corrida quase nao aparece. Conforme N cresce, a disputa\n");
    printf("pela conta central se intensifica e o erro passa a ser sistematico.\n");
}

/* --passo-a-passo: prova deterministica de um lost update com 2 threads */
typedef struct { const char *nome; long atraso_ler_ms; long atraso_escrever_ms; } PassoArg;

static void *passo_thread(void *arg)
{
    PassoArg *p = (PassoArg *)arg;
    usleep((useconds_t)p->atraso_ler_ms * 1000);
    long tmp = saldo_central;                    /* le */
    printf("  [thread %s] LEU saldo_central = %ld\n", p->nome, tmp);
    usleep((useconds_t)p->atraso_escrever_ms * 1000);
    saldo_central = tmp + 1;                     /* escreve */
    printf("  [thread %s] ESCREVEU saldo_central = %ld\n", p->nome, tmp + 1);
    return NULL;
}

static void *passo_thread_mutex(void *arg)
{
    PassoArg *p = (PassoArg *)arg;
    usleep((useconds_t)p->atraso_ler_ms * 1000);
    pthread_mutex_lock(&mutex_central);
    long tmp = saldo_central;
    printf("  [thread %s] (lock) LEU %ld\n", p->nome, tmp);
    usleep((useconds_t)p->atraso_escrever_ms * 1000);
    saldo_central = tmp + 1;
    printf("  [thread %s] ESCREVEU %ld (unlock)\n", p->nome, tmp + 1);
    pthread_mutex_unlock(&mutex_central);
    return NULL;
}

static void modo_passo_a_passo(void)
{
    printf("\n=== PARTE 1: condicao de corrida passo a passo (2 threads) ===\n");
    printf("Duas transferencias de 1 unidade para a conta central (esperado = 2).\n\n");

    /* A le em t=0 e escreve em t=200ms; B le em t=100ms (saldo ainda 0) */
    PassoArg a = { "A",   0, 200 };
    PassoArg b = { "B", 100, 200 };
    pthread_t ta, tb;

    printf("-- SEM mutex (corrida acontece) --\n");
    saldo_central = 0;
    pthread_create(&ta, NULL, passo_thread, &a);
    pthread_create(&tb, NULL, passo_thread, &b);
    pthread_join(ta, NULL); pthread_join(tb, NULL);
    printf("  >> saldo final = %ld (esperado 2). %s\n", saldo_central,
           saldo_central == 2 ? "sem perda" : "UMA transferencia foi PERDIDA!");

    printf("\n-- COM mutex (secao critica protegida) --\n");
    saldo_central = 0;
    pthread_create(&ta, NULL, passo_thread_mutex, &a);
    pthread_create(&tb, NULL, passo_thread_mutex, &b);
    pthread_join(ta, NULL); pthread_join(tb, NULL);
    printf("  >> saldo final = %ld (esperado 2). %s\n", saldo_central,
           saldo_central == 2 ? "correto: nada se perdeu." : "ERRO inesperado.");
}

/* ===================== main ===================== */

static void uso(const char *prog)
{
    printf("Uso:\n");
    printf("  %s --passo-a-passo    (P1) corrida deterministica com 2 threads\n", prog);
    printf("  %s --sem-mutex  N     (P1) N envios SEM protecao\n", prog);
    printf("  %s --com-mutex  N     (P1) N envios COM mutex\n", prog);
    printf("  %s --demo             (P1) varre N e mede a corrida\n", prog);
    printf("  %s --fcfs  P N        (P2) pool de P workers + fila FCFS (em breve)\n", prog);
    printf("  %s                    (P3) sistema completo + debug por sinais (em breve)\n", prog);
}

int main(int argc, char **argv)
{
    srand((unsigned)time(NULL));

    if (argc >= 2 && strcmp(argv[1], "--passo-a-passo") == 0) { modo_passo_a_passo(); return 0; }
    if (argc >= 2 && strcmp(argv[1], "--demo") == 0)          { modo_demo();          return 0; }
    if (argc >= 3 && strcmp(argv[1], "--sem-mutex") == 0)     { modo_rodada_unica(atoi(argv[2]), 0); return 0; }
    if (argc >= 3 && strcmp(argv[1], "--com-mutex") == 0)     { modo_rodada_unica(atoi(argv[2]), 1); return 0; }

    uso(argv[0]);
    return 0;
}
