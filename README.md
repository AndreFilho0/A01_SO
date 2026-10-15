# INF0336 – Sistemas Operacionais — Primeira Avaliação (A01)

## Emulando o Sistema de Funcionamento do PIX

Trabalho da disciplina **INF0336 (Sistemas Operacionais)** — Ciência da Computação / UFG.
As soluções usam **apenas os conceitos estudados em sala/laboratório** (processos, threads,
IPC, escalonamento, sincronismo e sinais), implementados em **C com POSIX threads (pthreads)**.

---

## Grupo

| Nome | Matrícula | Usuário | E-mail |
|------|-----------|---------|--------|
| André de Oliveira Machado Filho | 202406913 | andrefilho | andrefilho@discente.ufg.br |
| Jamilly Vitorya da Silva Barbosa | 202103736 | jamilly_barbosa | jamilly_barbosa@discente.ufg.br |
| Alan Queiroz Dantas de Oliveira | 202109882 | alandantas | alandantas@discente.ufg.br |

Curso: **Ciência da Computação** — Universidade Federal de Goiás (UFG).

---

## Vídeo-respostas

Cada solução é acompanhada de um link com uma vídeo-resposta explicativa de até 5 minutos.
Cada integrante participa de, no mínimo, uma gravação.

| Parte | Responsável pela gravação | Link |
|-------|---------------------------|------|
| Parte 1 — Sincronismo (5.0 pts) | _a definir_ | _(adicionar link)_ |
| Parte 2 — Escalonador FCFS (3.0 pts) | _a definir_ | _(adicionar link)_ |
| Parte 3 — Debugging por sinais (2.0 pts) | _a definir_ | _(adicionar link)_ |

---

## Visão geral da solução

O enunciado descreve **um único sistema PIX que vai ganhando funcionalidades**
("incorpore…", "adicione…"). Por isso a solução é **um único programa** (`pix.c`)
com **modos selecionados por linha de comando**, onde cada modo isola um conceito
para a vídeo-resposta correspondente.

| Parte | O que demonstra | Como rodar |
|-------|-----------------|-----------|
| **1** | Condição de corrida, recurso e seção crítica (modelo N:1) | `./pix --passo-a-passo`, `./pix --sem-mutex N`, `./pix --com-mutex N`, `./pix --demo` |
| **2** | Escalonador **FCFS** para requisições que excedem a thread pool | `./pix --fcfs P N` |
| **3** | **Debugging por sinais**: uma tecla mostra o estado atual da fila | `./pix` |

---

## Como compilar e executar

```bash
# compilar
make            # ou: gcc -O2 -o pix pix.c -pthread

# Parte 1 — sincronismo
./pix --passo-a-passo      # corrida determinística com 2 threads (didático)
./pix --sem-mutex 1000     # 1000 envios SEM proteção  -> dinheiro some
./pix --com-mutex 1000     # 1000 envios COM mutex      -> correto
./pix --demo               # varre N e mostra a partir de quando o erro aparece

# Parte 2 — escalonador FCFS
./pix --fcfs 3 10          # pool de 3 workers, 10 requisições (7 esperam na fila)

# Parte 3 — debugging por sinais
./pix                      # sistema completo; pressione Ctrl+C para ver a fila
```

> Requisitos: `gcc`/`clang` com suporte a POSIX threads (Linux ou macOS).

---

## Estrutura do repositório

```
A01_SO/
├── README.md       # este arquivo
├── Makefile        # compila o executável 'pix'
├── pix.c           # solução única (Partes 1, 2 e 3)
└── docs/           # explicações escritas exigidas em "Na resposta: ..."
    ├── parte1_sincronismo.md
    ├── parte2_fcfs.md
    └── parte3_debug_sinais.md
```

---

## Mapeamento enunciado → solução

- **(5.0) Sincronismo:** thread pool N:1 sobre uma base de contas compartilhada.
  O **recurso** é o saldo da conta central; a **seção crítica** é o
  *read-modify-write* desse saldo; a **condição de corrida** aparece quando
  várias threads executam essa seção sem exclusão mútua. A demonstração empírica
  (`--demo`) mostra a partir de que valores de **N** o problema se torna evidente.
- **(3.0) Proteção mínima — FCFS:** a thread pool passa a ter tamanho fixo; as
  requisições excedentes são organizadas numa fila **First-Come, First-Served**.
- **(2.0) Debugging por sinais:** um handler de sinal (tecla Ctrl+C) solicita o
  *dump* do estado atual da fila de requisições, com tempos de execução simulados
  para facilitar a verificação.
