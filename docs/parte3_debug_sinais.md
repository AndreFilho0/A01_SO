# Parte 3 - Debugging por sinais

## Proposta

Esta parte reutiliza a thread pool e a fila FCFS da parte 2. Ao pressionar
**Ctrl+C**, o terminal envia o sinal **SIGINT**. O programa recebe esse sinal,
mostra o estado atual da fila e continua atendendo os PIX.

A consulta mostra:

- Quantas requisições já entraram no sistema e quantas ainda serão enfileiradas.
- A fila completa de requisições aguardando, na ordem FCFS.
- Qual PIX cada worker está atendendo, ou se ele está livre.
- Quantas transferências já foram concluídas.

As entradas da fila representam requisições de transferência; não são processos
independentes do sistema operacional. Os workers são threads do mesmo programa.

## Como o sinal é recebido

1. A thread principal bloqueia `SIGINT` e `SIGUSR1` com `pthread_sigmask` antes de
   criar as outras threads. As novas threads herdam essa máscara.
2. Uma thread dedicada chama `sigwait`, ficando adormecida até receber um sinal.
3. Quando recebe `SIGINT`, ela adquire `mutex_fila`, mostra o estado e libera o
   mutex. Depois volta a aguardar outro sinal.
4. Após todos os workers terminarem, a principal envia `SIGUSR1` apenas à thread
   de debugging com `pthread_kill`. Ela sai de `sigwait` e termina. A principal
   aguarda essa thread antes de liberar a memória da fila e dos relatórios.

`sigwait` é uma forma síncrona de receber sinais. A impressão ocorre em uma
thread normal: não há handler assíncrono executando `printf` ou tentando adquirir
um mutex. Isso evita interromper uma thread no momento em que ela já está usando
esses mesmos recursos.

## Proteção da consulta

O mesmo `mutex_fila` protege a inserção, a retirada, o estado dos workers e a
contagem de concluídos. Ao retirar uma requisição, o worker já registra seu PIX
em atendimento dentro da seção crítica. Quando conclui, registra a conclusão e
fica livre também com esse mutex adquirido.

Assim, cada consulta representa um único instante: nenhuma requisição aparece
simultaneamente esperando e em atendimento, nem desaparece entre esses estados.
Durante a impressão, as operações que precisam desse mutex aguardam brevemente;
as transferências não são canceladas e a ordem FCFS não é modificada.

## Como executar

Em um terminal Linux/WSL:

```bash
gcc -O2 -o pix pix.c -pthread
./pix
```

O padrão é uma pool de 3 workers e 12 requisições. Cada atendimento demora
3 segundos, e uma nova requisição chega a cada 100 ms. A execução dura
aproximadamente 12 segundos, dando tempo para acionar várias consultas.

Para configurar os mesmos parâmetros explicitamente:

```bash
./pix --debug 3 12 3000 100
```

Os parâmetros são: tamanho da pool, quantidade de PIX, tempo de atendimento
em milissegundos e intervalo entre chegadas em milissegundos.

Ctrl+C consulta a fila apenas no modo da parte 3 (`./pix` ou `--debug`).
O programa encerra automaticamente após processar todos os PIX. Para interromper
manualmente a execução nesse modo em um terminal Linux, pode-se usar Ctrl+\.

## Demonstração para a video-resposta

1. Compile e execute `./pix`.
2. Após aparecer a chegada do PIX #12 e antes dos primeiros 3 segundos, pressione
   Ctrl+C. Em uma execução típica haverá 3 PIX em atendimento e 9 na fila.
3. Explique que Ctrl+C gerou `SIGINT` e que a thread de debugging mostrou a fila
   sob a proteção do mutex.
4. Após algumas conclusões, pressione Ctrl+C novamente. Mostre a redução da fila
   e o aumento da quantidade de concluídos.
5. Quando o PIX #12 já estiver em atendimento e ainda não tiver terminado,
   pressione Ctrl+C: a fila estará vazia, embora ainda existam PIX em execução.
6. Mostre o relatório final: ordem de atendimento FCFS, saldo central de 12 e
   conservação do total de dinheiro.

Os tempos exatos dependem do escalonamento do sistema operacional. A garantia é
a ordem de retirada da fila, e não a ordem de conclusão entre workers distintos.

Para observar uma fila cheia, incluindo a espera do produtor por espaço:

```bash
./pix --debug 1 80 100 0
```

Pressione Ctrl+C enquanto há muitas requisições pendentes. A fila comporta até
64 entradas; a consulta lista todas elas em ordem. O produtor aguarda espaço
quando a fila atinge esse limite.
