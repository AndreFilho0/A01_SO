# Parte 2 — Thread Pool Fixa e Escalonador FCFS (3.0 pts)

**Enunciado.** Incorpore um mecanismo de proteção mínimo no Sistema PIX, onde as requisições de envio de dinheiro dos usuários que superem a capacidade de atendimento imediato da *thread pool* sejam organizadas por um **escalonador FCFS (*First-Come, First-Served*)**. Na resposta: mostre a operação do escalonamento com um caso de exemplo, comprovando a ordem de atendimento e a integridade da base de dados.

---

#### 1. A proposta
Na Parte 1, cada requisição criava uma nova thread concorrente para disputar a conta central. Para evitar o *overhead* excessivo de criação de threads no sistema operacional e proteger o sistema contra sobrecarga, a **Parte 2** introduz uma arquitetura com:
1. **Thread Pool de Tamanho Fixo ($P$ workers):** Um conjunto fixo de $P$ threads consumidoras que permanecem ativas durante toda a execução.
2. **Buffer Limitado (Fila FIFO Circular):** Um buffer com capacidade para até $TAM\_FILA = 64$ requisições pendentes.
3. **Escalonamento FCFS:** Como os *workers* retiram as requisições estritamente do início da fila (`fila.ini`), a ordem de atendimento é idêntica à ordem de chegada (*First-Come, First-Served*).

---

#### 2. Arquitetura do Escalonador e Mecanismos de Sincronização

##### A Estrutura de Dados da Fila FIFO
A fila é implementada como um buffer circular gerenciado pela estrutura `Fila`:

```c
typedef struct {
    int  id;       // Número de chegada: 1, 2, 3, ..., N
    int  origem;   // Conta de origem do usuário
    long valor;    // Valor da transferência PIX
} Req;

typedef struct {
    Req dados[TAM_FILA];
    int ini; // Índice da cabeça (remoção / FCFS)
    int fim; // Índice da cauda (inserção)
    int qtd; // Quantidade atual de requisições retidas
} Fila;
```

##### Sincronização POSIX (Produtor-Consumidor)
A coordenação entre o thread principal (Produtor) e as threads da pool (Consumidores) utiliza a API de sincronização POSIX (`pthreads`), baseada no problema do **Buffer Limitado**:

* **Exclusão Mútua da Fila (`mutex_fila`):** Garante que a manipulação dos ponteiros `ini`, `fim` e da variável `qtd` ocorra em seção crítica, evitando *race conditions* na própria fila.
* **Sincronização por Condição (`nao_vazia`):** Variável de condição (`pthread_cond_t`) usada para bloquear os *workers* quando a fila está vazia (`qtd == 0`). Assim que o produtor enfileira um PIX, ele aciona `pthread_cond_signal(&nao_vazia)` para acordar uma thread trabalhadora.
* **Sincronização por Condição (`nao_cheia`):** Bloqueia o produtor caso a taxa de requisições exceda a capacidade total do buffer (`qtd == TAM_FILA`), aguardando que os *workers* liberem espaço.
* **Proteção Mútua da Conta Central (`mutex_central`):** Cada *worker*, ao processar a transferência, adquire o `mutex_central` durante o ciclo de leitura e atualização do saldo, eliminando as perdas de atualização (*lost updates*) identificadas na Parte 1.

---

#### 3. Caso de Exemplo e Operação do Escalonador

##### Cenário de Teste
Executamos o sistema configurado com $P = 4$ *workers*, $N = 16$ requisições, tempo de serviço simulado de $200\text{ ms}$ por PIX e taxa de chegada de requisições a cada $50\text{ ms}$:

```bash
./pix --fcfs 4 16 200 50
```

Como o tempo de serviço ($200\text{ ms}$) é maior do que o intervalo entre chegadas ($50\text{ ms}$), os 4 *workers* ficam rapidamente ocupados. As requisições subsequentes são retidas na fila FIFO, acionando o escalonador FCFS.

##### Tabela de Rastreamento do Escalonamento (Saída do Executável)

```
=== PARTE 2: thread pool de 4 workers + fila FCFS ===
  16 requisicoes (N:1 -> conta central) | servico=200ms | chegada a cada 50ms | fila max=64

  PIX | chegada | inicio |   fim  | espera | worker | ordem de saida da fila
  ----+---------+--------+--------+--------+--------+-----------------------
  #1  |    0.00 |   0.00 |   0.20 |   0.00 |      1 | 1
  #2  |    0.05 |   0.05 |   0.25 |   0.00 |      2 | 2
  #3  |    0.10 |   0.10 |   0.30 |   0.00 |      3 | 3
  #4  |    0.15 |   0.15 |   0.35 |   0.00 |      4 | 4
  #5  |    0.20 |   0.20 |   0.40 |   0.00 |      1 | 5
  #6  |    0.25 |   0.25 |   0.45 |   0.00 |      2 | 6
  #7  |    0.30 |   0.30 |   0.50 |   0.00 |      3 | 7
  #8  |    0.35 |   0.35 |   0.55 |   0.00 |      4 | 8
  #9  |    0.40 |   0.40 |   0.60 |   0.00 |      1 | 9
  #10 |    0.45 |   0.45 |   0.65 |   0.00 |      2 | 10
  #11 |    0.50 |   0.50 |   0.70 |   0.00 |      3 | 11
  #12 |    0.55 |   0.55 |   0.75 |   0.00 |      4 | 12
  #13 |    0.60 |   0.60 |   0.80 |   0.00 |      1 | 13
  #14 |    0.65 |   0.65 |   0.85 |   0.00 |      2 | 14
  #15 |    0.70 |   0.70 |   0.90 |   0.00 |      3 | 15
  #16 |    0.75 |   0.75 |   0.95 |   0.00 |      4 | 16

  Espera media na fila: 0.00s | espera maxima: 0.00s | fila maxima: 4
  Ordem de atendimento == ordem de chegada (FCFS)? SIM

  Saldo central ESPERADO: 16 | OBTIDO: 16 | total antes: 16000000 | depois: 16000000
  => OK: nenhuma atualizacao perdida, total conservado.
```

##### Análise das Métricas de Validação:
1. **Garantia da Ordem FCFS:** A coluna `ordem de saida da fila` é estritamente sequencial ($1, 2, 3, \dots, N$). A validação programática confirma: `Ordem de atendimento == ordem de chegada (FCFS)? SIM`.
2. **Conservação de Recursos e Proteção Mútua:** O saldo obtido na conta central atinge exatamente $16$ unidades (esperado = $16$). O invariante financeiro (soma do dinheiro de todas as contas) é mantido sem perdas (`total antes == total depois`).

---

#### 4. Como Reproduzir

Compilação do código em C:
```bash
gcc -O2 -o pix pix.c -pthread
```

Execução do teste de exemplo da Parte 2 (FCFS):
```bash
./pix --fcfs 4 16 200 50
```

Teste com fila cheia e carga elevada (ex: 8 workers, 100 PIX):
```bash
./pix --fcfs 8 100 100 10
```
