# Engenharia reversa — ETS2 1.62.3.3

Notas de análise estática (Ghidra 12.2) do `eurotrucks2.exe` 1.62.3.3. Endereços absolutos com
imageBase `0x140000000`. **Tudo aqui vale só para essa versão** e ainda não foi confirmado com o
jogo rodando.

O exe e o projeto do Ghidra ficam em `re/local/` (ignorado pelo git). Scripts em `tools/ghidra/`.

## Observações gerais

- As classes do motor (prism) **não têm RTTI**; só o PhysX tem. Vtables precisam ser achadas por
  varredura de ponteiros (`FindVtables.java`).
- Analisador "Create Address Tables" estoura a memória nesse exe — desligado via
  `DisableHeavyAnalyzers.java` (pré-script).

## Ator do jogador — tecla de engate

`FUN_140967a90(actor)` processa o input do jogador. Trecho do engate (perto das mensagens
`[actor] Trailer (dis)connection disabled`):

| Campo | Significado (hipótese) |
|---|---|
| `actor+0x18` | caminhão do jogador (veículo) |
| `actor+0xc8` | reboque associado ao jogador |
| `actor+0x52` / `+0x53` | flags "engate desabilitado" / "desengate desabilitado" |
| `actor+0x1e6c8` | controle de input "trailer" (`+0xf8 == 2` → pressionado) |
| `truck+0x14b8` | estado do engate: 0 sem reboque, 2 desengatando, 3 ?, 5 engatado |
| `truck+0x1580` | primeiro reboque da composição |
| `trailer+0x1058` | próximo reboque (lista encadeada) |
| `trailer+0x200` | dados/modelo do reboque (`+0x628` tipo de engate 1..4, `+0x740` posição do engate) |

Funções:

| Endereço | Papel |
|---|---|
| `FUN_1405d7dc0(actor)` | desengate "normal": estado 5→2, marca `trailer+0x25a=1` em toda a cadeia, toca `@@trailer_decoupling@@` |
| `FUN_1405dbd20(actor, force, silent)` | desengate forçado: `trailer->vt[0x318]()`, estado = 0, limpa vínculos |
| `FUN_1405db530(actor, 0, 1)` | engate (estado 0) — mensagem `@@trailer_coupling@@` |
| `FUN_1405dbfa0(actor)` | ação no estado 3 |

## Engate entre reboques (slave hook)

| Endereço | Papel |
|---|---|
| `FUN_140774ef0(trailer, out vec3)` | posição do "slave hook locator" (engate traseiro) |
| `FUN_140775050(trailer)` | se `trailer+0x1058` existe: `slave->vt[0x310](slave, trailer, &posRelativa, 0, flag, tipo==2)` — candidato a "prender o reboque de trás neste" |
| `FUN_140775120(trailer)` | percorre a cadeia `+0x1058` somando comprimentos |

### Vtable do reboque físico — `0x1422f8c00`

Sem RTTI as vtables ficam coladas em `.rdata`; o início real foi achado pelos construtores que
gravam o ponteiro (`VtableStarts.java`). Em `0x1422f8b88..0x1422f8c00` há 5 vtables pequenas
(interfaces); a grande do reboque começa em **`0x1422f8c00`** (construtores `FUN_14076fbb0`,
`FUN_14076fe40`), ~102 entradas.

| Slot | Função | Papel |
|---|---|---|
| `+0x310` | `FUN_140774710(this, master, posEngate, int, bool teleporta, bool)` | **engatar**: `this+0x1050 = master`, cria junta física tipo 7 no mundo (`DAT_143714fb0+0x2d90`, vt+0x70) e guarda em `this+0xfb0`; no fim chama `FUN_140775050(this)` p/ engatar o próprio reboque de trás |
| `+0x318` | `FUN_140775290(this)` | **desengatar**: destrói a junta `+0xfb0` (mundo vt+0xa0), reseta estado de toda a cadeia atrás, `this+0x1050 = 0` |
| `+0x328` | `FUN_140775360(this, bool)` | altera componentes visuais; efeito sobre cabos/mangueiras não confirmado, chamada não usada pelo plugin |

Campos do reboque físico:

| Campo | Significado |
|---|---|
| `+0x1050` | mestre (caminhão ou reboque da frente) — só a física |
| `+0x1058` | reboque de trás — **owner_ptr com contagem de referência** (dono do objeto) |
| `+0xfb0` | junta física com o mestre |
| `+0x200` | dados do reboque (`+0x628` tipo de engate, `+0x740` posição do engate) |

O desengate do cavalo (`FUN_1405dbd20`) usa o mesmo `vt[0x318]` no reboque.

### Quem monta a cadeia

`FUN_14062ad80` / `FUN_14062abf0` criam o reboque de trás a partir da definição
(`param_2[0x37]`) e gravam em `+0x1058` — só na **criação** da composição. O jogo **não tem**
rotina de desmontar a cadeia em tempo de execução: os reboques internos nunca são soltos.

## Plano para o MemoryBackend (hipótese)

- **Desengatar articulação N** (entre reboque N e N+1): `reboque[N+1]->vt[0x318](reboque[N+1])`.
  Solta só a física; o ponteiro de posse `reboque[N]+0x1058` fica como está (não zerar — o objeto
  seria liberado). As funções de física já checam `+0x1050 == 0`.
- **Engatar de novo**: `FUN_140775050(reboque[N])` — o mesmo caminho que o jogo usa na criação
  (calcula a posição do engate e chama `vt[0x310]`).
- Riscos: save/carregamento, entrega da carga e multiplayer assumem a cadeia inteira; reboque solto
  continua "logicamente" na composição.

## Pendências

1. Achar o caminho de um global até o caminhão do jogador (o ator chega por `FUN_140960c10`).
2. Confirmar tudo com o jogo rodando (precisa de outro PC — este não roda o jogo).
