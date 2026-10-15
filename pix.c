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
 *   ./pix --fcfs  P N        (P2) pool de P workers + fila FCFS
 *   ./pix                    (P3) sistema completo; Ctrl+C mostra a fila
 *   ./pix --debug P N [servico_ms] [chegada_ms] (P3) configura a demonstracao
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
#include <signal.h>

/* Cores somente em terminais; NO_COLOR permite desativar manualmente. */
static int cores_terminal;
#define C_RESET  "\033[0m"
#define C_TITULO "\033[1;36m"
#define C_SUAVE  "\033[90m"
#define C_FILA   "\033[33m"
#define C_ATIVO  "\033[36m"
#define C_OK     "\033[32m"
#define C_ERRO   "\033[31m"
#define PAINEL_TEXTO 72

static const char *cor(const char *codigo)
{
    return cores_terminal ? codigo : "";
}

static void painel_borda(void)
{
    printf("%s+", cor(C_SUAVE));
    for (int i = 0; i < PAINEL_TEXTO + 2; i++) putchar('-');
    printf("+%s\n", cor(C_RESET));
}

static void painel_linha(const char *codigo, const char *texto)
{
    printf("%s|%s %s%-*s%s %s|%s\n", cor(C_SUAVE), cor(C_RESET),
           cor(codigo), PAINEL_TEXTO, texto, cor(C_RESET), cor(C_SUAVE), cor(C_RESET));
}

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

/* ============ PARTE 2 - THREAD POOL FIXA + FILA FCFS ============ */
 
#define TAM_FILA      64  
#define NUM_USUARIOS  16  
#define MAX_POOL      64
 
typedef struct {
    int  id;       
    int  origem;   
    long valor;
} Req;
 
typedef struct {
    Req dados[TAM_FILA];
    int ini, fim, qtd;
} Fila;
 
static Fila fila;                       
static pthread_mutex_t mutex_fila = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  nao_vazia = PTHREAD_COND_INITIALIZER; 
static pthread_cond_t  nao_cheia = PTHREAD_COND_INITIALIZER;  
static int encerrar = 0;                                      
 
static long    usuarios[NUM_USUARIOS];  
static int     f_pool;                  
static long    f_servico_ms;            
static double  t0;                      
static int     max_fila;                
static int     seq_saida;               
static int     concluidas;             /* protegido por mutex_fila */
static int     f_total;
 
static double *t_chegada, *t_inicio, *t_fim;
static int    *t_worker, *t_ordem;
static int    *em_atendimento;
 
static double agora_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9 - t0;
}
 
/* As mensagens de eventos ficam alinhadas e usam a mesma protecao da fila. */
static void evento_fcfs(double tempo, const char *tipo, const char *codigo,
                        const char *detalhe)
{
    printf("%s  %6.2fs  %s%-10s%s %-46s %4d\n", cor(C_SUAVE), tempo,
           cor(codigo), tipo, cor(C_RESET), detalhe, fila.qtd);
}

/* ============ PARTE 3 - DEBUGGING POR SINAIS ============ */

/* Chamado com mutex_fila: a fila e os workers pertencem ao mesmo instante. */
static void mostrar_estado(void)
{
    char texto[192];
    int ocupados = 0;
    int recebidas = seq_saida + fila.qtd;
    for (int w = 0; w < f_pool; w++)
        if (em_atendimento[w] != 0) ocupados++;

    putchar('\n');
    painel_borda();
    snprintf(texto, sizeof texto, "=== DEBUG SIGINT [t=%.2fs] ===", agora_s());
    painel_linha(C_TITULO, texto);
    painel_linha(C_SUAVE, "Estado atual | FCFS: primeiro a chegar, primeiro a sair");
    painel_borda();
    snprintf(texto, sizeof texto, "Recebidas: %d/%d | Em atendimento: %d | Concluidas: %d",
           recebidas, f_total, ocupados, concluidas);
    painel_linha(C_RESET, texto);
    snprintf(texto, sizeof texto, "Ainda nao enfileiradas: %d", f_total - recebidas);
    painel_linha(C_SUAVE, texto);
    char barra[21];
    int preenchidos = (int)(20LL * concluidas / f_total);
    for (int i = 0; i < 20; i++) barra[i] = i < preenchidos ? '=' : '.';
    barra[20] = '\0';
    snprintf(texto, sizeof texto, "Progresso [%s] %5.1f%%", barra, 100.0 * concluidas / f_total);
    painel_linha(C_OK, texto);
    painel_borda();

    /* Quebra a fila em linhas, mantendo visiveis todas as 64 entradas. */
    char linha[PAINEL_TEXTO + 1];
    size_t pos = (size_t)snprintf(linha, sizeof linha, "Fila FCFS (%d aguardando): [", fila.qtd);
    for (int k = 0; k < fila.qtd; k++) {
        const Req *r = &fila.dados[(fila.ini + k) % TAM_FILA];
        char item[24];
        int n = snprintf(item, sizeof item, "%s#%d", k ? " -> " : "", r->id);
        if (pos + (size_t)n + 1 > PAINEL_TEXTO) {
            painel_linha(C_FILA, linha);
            pos = (size_t)snprintf(linha, sizeof linha, "  ");
        }
        pos += (size_t)snprintf(linha + pos, sizeof linha - pos, "%s", item);
    }
    snprintf(linha + pos, sizeof linha - pos, "]%s", fila.qtd == 0 ? " (vazia)" : "");
    painel_linha(fila.qtd ? C_FILA : C_SUAVE, linha);
    painel_borda();
    painel_linha(C_TITULO, "THREAD POOL / ATENDIMENTO");
    for (int w = 0; w < f_pool; w++) {
        if (em_atendimento[w] != 0) {
            int id = em_atendimento[w];
            snprintf(texto, sizeof texto, "worker %d: atendendo PIX #%d | tempo: %.2fs",
                     w + 1, id, agora_s() - t_inicio[id - 1]);
        } else {
            snprintf(texto, sizeof texto, "worker %d: livre", w + 1);
        }
        painel_linha(em_atendimento[w] ? C_ATIVO : C_SUAVE, texto);
    }
    painel_borda();
    painel_linha(C_OK, "=== FIM DEBUG: simulacao continua ===");
    painel_linha(C_SUAVE, "Ctrl+C: nova consulta | encerramento automatico ao concluir todos os PIX");
    painel_borda();
    putchar('\n');
    fflush(stdout);
}

/* sigwait recebe o sinal em uma thread normal, onde printf e mutex sao seguros.
 * Nao ha handler assincrono acessando a fila. SIGUSR1 so acorda esta thread
 * internamente, quando todas as transferencias terminam. */
static void *debug_sinais(void *arg)
{
    (void)arg;
    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGUSR1);
    for (;;) {
        int sinal;
        int erro = sigwait(&sinais, &sinal);
        if (erro != 0) {
            fprintf(stderr, "Falha ao aguardar sinal: %s\n", strerror(erro));
            return NULL;
        }
        if (sinal == SIGUSR1) return NULL;
        pthread_mutex_lock(&mutex_fila);
        mostrar_estado();
        pthread_mutex_unlock(&mutex_fila);
    }
}
 
/* PRODUTOR: coloca a requisicao no FIM da fila (dorme se cheia) */
static void enfileirar(Req r)
{
    char detalhe[160];
    pthread_mutex_lock(&mutex_fila);
    while (fila.qtd == TAM_FILA)
        pthread_cond_wait(&nao_cheia, &mutex_fila);
    fila.dados[fila.fim] = r;
    fila.fim = (fila.fim + 1) % TAM_FILA;
    fila.qtd++;
    if (fila.qtd > max_fila) max_fila = fila.qtd;
    snprintf(detalhe, sizeof detalhe, "PIX #%d chegou", r.id);
    evento_fcfs(agora_s(), "ENTRADA", C_FILA, detalhe);
    pthread_cond_signal(&nao_vazia);         
    pthread_mutex_unlock(&mutex_fila);
}
 
/* CONSUMIDOR: retira da CABECA da fila; devolve id=0 se acabou o expediente */
static Req retirar(int w)
{
    char detalhe[160];
    Req r = { 0, 0, 0 };
    pthread_mutex_lock(&mutex_fila);
    while (fila.qtd == 0 && !encerrar)
        pthread_cond_wait(&nao_vazia, &mutex_fila);   
    if (fila.qtd == 0) {                              
        pthread_mutex_unlock(&mutex_fila);
        return r;
    }
    r = fila.dados[fila.ini];
    fila.ini = (fila.ini + 1) % TAM_FILA;
    fila.qtd--;
    em_atendimento[w - 1] = r.id;
    t_ordem[r.id - 1]  = ++seq_saida;
    t_inicio[r.id - 1] = agora_s();
    t_worker[r.id - 1] = w;
    snprintf(detalhe, sizeof detalhe, "worker %d iniciou PIX #%d (espera %.2fs)",
             w, r.id, t_inicio[r.id - 1] - t_chegada[r.id - 1]);
    evento_fcfs(t_inicio[r.id - 1], "INICIO", C_ATIVO, detalhe);
    pthread_cond_signal(&nao_cheia);
    pthread_mutex_unlock(&mutex_fila);
    return r;
}
 
/* transferencia com protecao minima: o mutex cobre debito e credito */
static void transferir_fcfs(const Req *r)
{
    pthread_mutex_lock(&mutex_central);
    usuarios[r->origem] -= r->valor;
    long tmp = saldo_central;        
    trabalho_na_secao();
    tmp = tmp + r->valor;           
    saldo_central = tmp;             
    pthread_mutex_unlock(&mutex_central);
}
 
/* WORKER da thread pool: fica dormindo ate haver requisicao na fila */
static void *worker_fcfs(void *arg)
{
    int w = (int)(intptr_t)arg;                  
    for (;;) {
        Req r = retirar(w);                      
        if (r.id == 0) break;                    
 
        usleep((useconds_t)f_servico_ms * 1000); 
        transferir_fcfs(&r);
        pthread_mutex_lock(&mutex_fila);
        t_fim[r.id - 1] = agora_s();
        em_atendimento[w - 1] = 0;
        concluidas++;
 
        char detalhe[160];
        snprintf(detalhe, sizeof detalhe, "worker %d concluiu PIX #%d", w, r.id);
        evento_fcfs(t_fim[r.id - 1], "CONCLUIDO", C_OK, detalhe);
        pthread_mutex_unlock(&mutex_fila);
    }
    return NULL;
}
 
/* --fcfs P N [servico_ms] [chegada_ms] */
static int modo_fcfs(int P, int N, long servico_ms, long chegada_ms, int debug)
{
    if (P < 1 || P > MAX_POOL || N < 1 || servico_ms < 0 || chegada_ms < 0) {
        fprintf(stderr, "Parametros invalidos (1 <= P <= %d, N >= 1, tempos >= 0).\n", MAX_POOL);
        return 1;
    }
    f_pool = P;
    f_total = N;
    f_servico_ms = servico_ms;
 
    char texto[192];
    putchar('\n');
    painel_borda();
    painel_linha(C_TITULO, "PIX  /  SIMULADOR DE TRANSFERENCIAS");
    painel_linha(C_SUAVE, debug ? "PARTE 3  |  Debugging por sinais + fila FCFS"
                                       : "PARTE 2  |  Thread pool + fila FCFS");
    painel_borda();
    snprintf(texto, sizeof texto, "POOL %d workers   |   PIX %d requisicoes   |   FILA ate %d entradas", P, N, TAM_FILA);
    painel_linha(C_RESET, texto);
    snprintf(texto, sizeof texto, "Atendimento: %ld ms   |   Intervalo de chegada: %ld ms", servico_ms, chegada_ms);
    painel_linha(C_RESET, texto);
    painel_borda();
 
    // estado inicial
    saldo_central = 0;
    for (int i = 0; i < NUM_USUARIOS; i++) usuarios[i] = SALDO_INICIAL;
    fila.ini = fila.fim = fila.qtd = 0;
    max_fila = 0; seq_saida = 0; encerrar = 0; concluidas = 0;
    t_chegada = calloc((size_t)N, sizeof(double));
    t_inicio  = calloc((size_t)N, sizeof(double));
    t_fim     = calloc((size_t)N, sizeof(double));
    t_worker  = calloc((size_t)N, sizeof(int));
    t_ordem   = calloc((size_t)N, sizeof(int));
    em_atendimento = calloc((size_t)P, sizeof(int));

    if (!t_chegada || !t_inicio || !t_fim || !t_worker || !t_ordem || !em_atendimento) {
        fprintf(stderr, "Memoria insuficiente para a simulacao.\n");
        free(t_chegada); free(t_inicio); free(t_fim);
        free(t_worker); free(t_ordem); free(em_atendimento);
        return 1;
    }

    t0 = 0; t0 = agora_s();

    pthread_t thread_debug;
    if (debug) {
        sigset_t sinais, anteriores;
        sigemptyset(&sinais);
        sigaddset(&sinais, SIGINT);
        sigaddset(&sinais, SIGUSR1);
        /* Antes de criar qualquer thread: todas herdam os sinais bloqueados.
         * Assim Ctrl+C chega ao sigwait, sem encerrar um worker por acidente. */
        int erro = pthread_sigmask(SIG_BLOCK, &sinais, &anteriores);
        if (erro == 0) {
            erro = pthread_create(&thread_debug, NULL, debug_sinais, NULL);
            if (erro != 0) pthread_sigmask(SIG_SETMASK, &anteriores, NULL);
        }
        if (erro != 0) {
            fprintf(stderr, "Falha ao iniciar debugging: %s\n", strerror(erro));
            free(t_chegada); free(t_inicio); free(t_fim);
            free(t_worker); free(t_ordem); free(em_atendimento);
            return 1;
        }
        printf("\n%s  Ctrl+C%s  consultar fila e workers | a simulacao continua\n",
               cor(C_TITULO), cor(C_RESET));
        printf("%s  Encerramento automatico apos concluir os %d PIX.%s\n", cor(C_SUAVE), N, cor(C_RESET));
        fflush(stdout);
    }

    printf("\n%s  %-8s %-10s %-46s %4s%s\n", cor(C_SUAVE),
           "TEMPO", "EVENTO", "ATIVIDADE", "FILA", cor(C_RESET));
    printf("%s  -----------------------------------------------------------------------%s\n",
           cor(C_SUAVE), cor(C_RESET));
 
    // cria a thread pool (tamanho fixo P)
    pthread_t tids[MAX_POOL];
    for (int w = 1; w <= P; w++)
        pthread_create(&tids[w - 1], NULL, worker_fcfs, (void *)(intptr_t)w);
 
    // N usuarios enviam PIX: as requisicoes chegam em ordem 1, 2, ..., N
    for (int i = 1; i <= N; i++) {
        Req r = { i, i % NUM_USUARIOS, VALOR_ENVIO };
        t_chegada[i - 1] = agora_s();
        enfileirar(r);
        usleep((useconds_t)chegada_ms * 1000);
    }
 
    pthread_mutex_lock(&mutex_fila);                      
    encerrar = 1;
    pthread_cond_broadcast(&nao_vazia);                
    pthread_mutex_unlock(&mutex_fila);
    for (int w = 0; w < P; w++) pthread_join(tids[w], NULL);  

    if (debug) {
        pthread_kill(thread_debug, SIGUSR1);
        pthread_join(thread_debug, NULL);
        /* Mantem os sinais bloqueados ate o processo sair, inclusive durante
         * o relatorio final e a liberacao das estruturas compartilhadas. */
    }
 
    /* ---------- relatorio do escalonamento ---------- */
    double soma_espera = 0, max_espera = 0;
    int fcfs_ok = 1;
    for (int i = 0; i < N; i++) {
        double e = t_inicio[i] - t_chegada[i];
        soma_espera += e;
        if (e > max_espera) max_espera = e;
        if (t_ordem[i] != i + 1) fcfs_ok = 0;
    }
 
    putchar('\n');
    painel_borda();
    painel_linha(C_TITULO, "RELATORIO FINAL  /  ESCALONAMENTO E INTEGRIDADE");
    painel_borda();
    if (N <= 40) {
        printf("\n%s  PIX | chegada | inicio |   fim  | espera | worker | ordem de saida da fila%s\n",
               cor(C_SUAVE), cor(C_RESET));
        printf("  ----+---------+--------+--------+--------+--------+-----------------------\n");
        for (int i = 0; i < N; i++)
            printf("  #%-2d | %7.2f | %6.2f | %6.2f | %6.2f | %6d | %d\n", i + 1, t_chegada[i],
                   t_inicio[i], t_fim[i], t_inicio[i] - t_chegada[i], t_worker[i], t_ordem[i]);
    }
    putchar('\n');
    painel_borda();
    snprintf(texto, sizeof texto, "Espera media: %.2fs | espera maxima: %.2fs | fila maxima: %d",
           soma_espera / N, max_espera, max_fila);
    painel_linha(C_RESET, texto);
    snprintf(texto, sizeof texto, "Ordem de atendimento == ordem de chegada (FCFS)? %s",
           fcfs_ok ? "SIM" : "NAO");
    painel_linha(fcfs_ok ? C_OK : C_ERRO, texto);
 
    /* ---------- conferencia da protecao ---------- */
    long soma_usuarios = 0;
    for (int i = 0; i < NUM_USUARIOS; i++) soma_usuarios += usuarios[i];
    long esperado = (long)N * VALOR_ENVIO;
    long total_antes = (long)NUM_USUARIOS * SALDO_INICIAL;
    long total_depois = saldo_central + soma_usuarios;
    snprintf(texto, sizeof texto, "Saldo central ESPERADO: %ld | OBTIDO: %ld", esperado, saldo_central);
    painel_linha(C_RESET, texto);
    snprintf(texto, sizeof texto, "total antes: %ld | depois: %ld", total_antes, total_depois);
    painel_linha(C_RESET, texto);
    painel_borda();
    int saldo_ok = saldo_central == esperado && total_antes == total_depois;
    painel_linha(saldo_ok ? C_OK : C_ERRO, saldo_ok
           ? "OK: nenhuma atualizacao perdida, total conservado."
           : "ERRO: inconsistencia detectada!");
    painel_borda();
    putchar('\n');
 
    free(t_chegada); free(t_inicio); free(t_fim);
    free(t_worker); free(t_ordem); free(em_atendimento);
    return !(fcfs_ok && saldo_central == esperado && total_antes == total_depois);
}

/* ===================== main ===================== */

static void uso(const char *prog)
{
    printf("Uso:\n");
    printf("  %s --passo-a-passo    (P1) corrida deterministica com 2 threads\n", prog);
    printf("  %s --sem-mutex  N     (P1) N envios SEM protecao\n", prog);
    printf("  %s --com-mutex  N     (P1) N envios COM mutex\n", prog);
    printf("  %s --demo             (P1) varre N e mede a corrida\n", prog);
    printf("  %s --fcfs P N [servico_ms=200] [chegada_ms=50]\n", prog);
    printf("                        (P2) pool de P workers + fila FCFS\n");
    printf("  %s                    (P3) pool=3, PIX=12, servico=3000ms, chegada=100ms\n", prog);
    printf("  %s --debug [P N [servico_ms=3000] [chegada_ms=100]]\n", prog);
    printf("                        (P3) Ctrl+C mostra a fila sem encerrar\n");
}

int main(int argc, char **argv)
{
    srand((unsigned)time(NULL));
    const char *term = getenv("TERM");
    cores_terminal = isatty(STDOUT_FILENO) && !getenv("NO_COLOR")
                  && term && strcmp(term, "dumb") != 0;
    /* Mantem as mensagens visiveis tambem ao redirecionar a saida. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--debug") == 0))
        return modo_fcfs(3, 12, 3000, 100, 1);
    if (argc >= 4 && argc <= 6 && strcmp(argv[1], "--debug") == 0) {
        long servico = argc > 4 ? atol(argv[4]) : 3000;
        long chegada = argc > 5 ? atol(argv[5]) : 100;
        return modo_fcfs(atoi(argv[2]), atoi(argv[3]), servico, chegada, 1);
    }

    if (argc >= 2 && strcmp(argv[1], "--passo-a-passo") == 0) { modo_passo_a_passo(); return 0; }
    if (argc >= 2 && strcmp(argv[1], "--demo") == 0)          { modo_demo();          return 0; }
    if (argc >= 3 && strcmp(argv[1], "--sem-mutex") == 0)     { modo_rodada_unica(atoi(argv[2]), 0); return 0; }
    if (argc >= 3 && strcmp(argv[1], "--com-mutex") == 0)     { modo_rodada_unica(atoi(argv[2]), 1); return 0; }
    if (argc >= 4 && strcmp(argv[1], "--fcfs") == 0) {
        long servico = argc > 4 ? atol(argv[4]) : 200;
        long chegada = argc > 5 ? atol(argv[5]) : 50;
        return modo_fcfs(atoi(argv[2]), atoi(argv[3]), servico, chegada, 0);
    }
    uso(argv[0]);
    return 1;
}
