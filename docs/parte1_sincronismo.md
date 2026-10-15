# Parte 1 — Sincronismo (5.0 pts)

> **Enunciado.** Simule o sincronismo do Sistema Distribuído PIX, onde as requisições
> de envio de dinheiro dos usuários são atendidas por uma **thread pool** que acessa uma
> **base de dados compartilhada de contas**. Assuma um modelo **N:1** para simular os envios.
> Na resposta: (a) explique a proposta descrevendo os elementos fundamentais — **condição de
> corrida, recurso e seção crítica**; (b) mostre, **empiricamente, a partir de que valores de N**
> o problema se torna evidente.

---

## 1. A proposta

Modelamos o PIX como **N requisições de envio** que creditam **uma única conta central**
(modelo **N:1**). As requisições são atendidas por uma **thread pool** de tamanho fixo
(`POOL_WORKERS = 8`): cada worker atende uma fatia das N requisições, em laço, e todos
disputam a mesma conta central.

- Cada worker tem a **sua** conta de origem (`origem[id]`): só ele a debita → **sem disputa**.
- Todos creditam a **mesma** conta central (`saldo_central`): é o ponto de disputa.

O crédito na conta central é um **read-modify-write (RMW)** em três passos:

```c
long tmp = saldo_central;   // (1) LÊ
tmp = tmp + VALOR_ENVIO;     // (2) MODIFICA (em cópia local)
saldo_central = tmp;         // (3) ESCREVE
```

Para tornar a disputa visível de forma estável, dois recursos de simulação são usados
(ambos apenas *revelam* a corrida que já existe, não a criam):
- um **portão de partida** (`gate_cv`): os 8 workers esperam e são liberados juntos, para
  que a disputa aconteça simultaneamente (sem isso, os primeiros workers terminariam antes
  dos últimos nascerem);
- uma pequena janela (`trabalho_na_secao()`) entre o LÊ e o ESCREVE, que simula a thread
  ser preemptada no meio do RMW.

---

## 2. Os três elementos fundamentais

### Recurso (resource)
O **recurso compartilhado** é a base de dados de contas — concretamente a variável
**`saldo_central`**, acessada por todos os workers da pool. É o único dado escrito por mais
de uma thread; as contas de origem, por serem exclusivas de cada worker, **não** são recurso
disputado.

### Seção crítica (critical section)
A **seção crítica** é o trecho que lê-modifica-escreve `saldo_central` (os três passos acima,
em `transferir()`). É a região que, para manter a consistência, só pode ser executada por
**uma thread por vez**. No código ela está delimitada por comentários `>>> SEÇÃO CRÍTICA <<<`
e protegida (quando habilitado) por `pthread_mutex_lock/unlock(&mutex_central)`.

### Condição de corrida (race condition)
A **condição de corrida** ocorre quando **duas ou mais threads executam a seção crítica de
forma intercalada e sem exclusão mútua**, e o resultado final passa a depender da ordem
(não-determinística) de escalonamento. Aqui, duas threads leem o **mesmo** `saldo_central`,
somam 1 cada uma sobre o mesmo valor e escrevem de volta — a segunda escrita **sobrescreve**
a primeira, e **uma transferência desaparece** (*lost update*).

**Prova determinística** (`./pix --passo-a-passo`):

```
-- SEM mutex (corrida acontece) --
  [thread A] LEU saldo_central = 0
  [thread B] LEU saldo_central = 0        <- B lê antes de A escrever
  [thread A] ESCREVEU saldo_central = 1
  [thread B] ESCREVEU saldo_central = 1   <- sobrescreve o crédito de A
  >> saldo_central final = 1 (esperado 2). UMA transferencia foi PERDIDA!

-- COM mutex (secao critica protegida) --
  [thread A] (lock) LEU 0
  [thread A] ESCREVEU 1 (unlock)
  [thread B] (lock) LEU 1
  [thread B] ESCREVEU 2 (unlock)
  >> saldo_central final = 2 (esperado 2). correto: nada se perdeu.
```

### A solução — sincronização
O **mutex** (`pthread_mutex_t`) garante **exclusão mútua**: enquanto uma thread está na seção
crítica, as demais ficam bloqueadas, serializando o RMW. Com isso o resultado volta a ser
determinístico e correto (`./pix --com-mutex N` → 0 perdas).

Um **invariante** confirma a correção: o **dinheiro total do sistema** (soma de todas as
contas) deve ser constante, pois cada transferência apenas move valor de uma conta para
outra. Sem mutex o total **cai** (dinheiro some); com mutex ele se **conserva**.

---

## 3. Demonstração empírica — a partir de que N

Saída de `./pix --demo` (15 rodadas por N, **sem** mutex):

```
     N | esperado |  perdidas(media) | erro medio | rodadas c/ erro
-------+----------+------------------+------------+----------------
     1 |        1 |              0.0 |     0.00% |      0 / 15
     2 |        2 |              0.1 |     3.33% |      1 / 15
     4 |        4 |              0.4 |    10.00% |      6 / 15
     8 |        8 |              1.5 |    18.33% |     14 / 15
    16 |       16 |              2.9 |    18.33% |     13 / 15
    32 |       32 |              9.3 |    28.96% |     15 / 15
    64 |       64 |             28.1 |    43.96% |     15 / 15
   128 |      128 |             68.7 |    53.70% |     15 / 15
   256 |      256 |            164.9 |    64.43% |     15 / 15
   512 |      512 |            392.3 |    76.61% |     15 / 15
  1000 |     1000 |            796.4 |    79.64% |     15 / 15
 10000 |    10000 |           8417.3 |    84.17% |     15 / 15
100000 |   100000 |          81410.0 |    81.41% |     15 / 15
```

**Leitura dos resultados:**

- **N = 1:** nunca há erro — com um único envio não há concorrência.
- **N = 2 a 4:** o erro começa a aparecer **esporadicamente** (depende do escalonador).
- **N = 8:** já falha em **14 de 15** rodadas — com a pool cheia (8 workers), a disputa
  é quase sempre perceptível.
- **N ≥ 32:** o problema se torna **sistemático** (100% das rodadas falham) e a fração de
  atualizações perdidas cresce e se estabiliza em torno de **~80%**.

**Conclusão:** o problema se torna evidente **a partir de N ≈ 8** (ocasional já em N = 2) e
**sistemático a partir de N ≈ 32**. Isso motiva a necessidade de um mecanismo de
sincronização — resolvido com o **mutex** sobre a seção crítica.

---

## Como reproduzir

```bash
make
./pix --passo-a-passo      # prova determinística do lost update
./pix --sem-mutex 100000   # ~88% dos envios perdidos
./pix --com-mutex 100000   # 0 perdas, total conservado
./pix --demo               # varredura de N (tabela acima)
```
