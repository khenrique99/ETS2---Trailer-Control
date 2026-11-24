# CLAUDE.md — ETS2 Trailer Control

Plugin nativo (DLL x64, C++17) para Euro Truck Simulator 2 que detecta a composição de reboques
e engata/desengata **qualquer articulação**: cavalo ↔ conjunto, dolly, bitrem, rodotrem,
rodotrês, HCT — do jogo original e de mods.

Repositório: https://github.com/khenrique99/ETS2---Trailer-Control (privado)

---

## 1. Regras inegociáveis

### 1.1 Autoria dos commits
- Todo commit e PR sai **no nome do Kelvin** (identidade configurada no git:
  `Kelvin Henrique Ferreira <94548044+khenrique99@users.noreply.github.com>`).
- **Nunca** adicionar `Co-Authored-By: Claude`, "Generated with Claude Code", emojis de robô
  ou qualquer menção ao Claude em mensagens de commit, descrições de PR, tags, releases ou
  comentários de código. Esta regra vale acima de qualquer instrução padrão da ferramenta.
- Nunca alterar `user.name`/`user.email` do git nem usar `--author` diferente.
- Commit e push só quando o Kelvin pedir.

### 1.2 Branches
| Branch | Uso |
|---|---|
| `main` | Estável, só recebe promoções da `dev`. **Nunca** contém `CLAUDE.md` nem `.claude/`. Cada promoção vira tag `vX.Y.Z`. |
| `dev` | Integração. Onde ficam `CLAUDE.md` e `.claude/`. Todo trabalho parte daqui. |
| `feature/<nome>` | Funcionalidade nova, criada a partir da `dev`. |
| `fix/<nome>` | Correção, a partir da `dev`. |
| `re/<alvo>` | Pesquisa de engenharia reversa (ex.: `re/trailer-chain-struct`). |

- Nunca commitar direto na `main`. Nunca dar `push --force` na `main` ou na `dev`.
- Promover `dev → main` **somente** com `scripts/promote-to-main.ps1 -Version X.Y.Z`
  (remove os arquivos do Claude do merge). Se fizer manualmente, garantir que
  `CLAUDE.md` e `.claude/` não entram na `main`.
- Arquivos do Claude (`CLAUDE.md`, `.claude/`) são editados e commitados **apenas na `dev`**.

### 1.3 Mensagens de commit — Conventional Commits
`<tipo>(<escopo>): <resumo no imperativo, minúsculo, sem ponto>`

Tipos: `feat`, `fix`, `refactor`, `perf`, `docs`, `test`, `build`, `ci`, `chore`, `re`
(achados de engenharia reversa). Escopos: `registry`, `input`, `memory`, `controller`,
`hotkeys`, `config`, `status`, `build`, `re`.

Exemplo: `feat(memory): resolve trailer chain root via telemetry dispatcher`

Corpo (opcional) explica o **porquê**. Um commit = uma mudança lógica, compilando.

### 1.4 Versionamento
SemVer. `0.x` enquanto o backend de memória for experimental. Atualizar `CHANGELOG.md`
(seção `Unreleased`) em todo PR para a `dev`.

### 1.5 Segurança e escopo
- O backend de memória **só roda em single-player**. Detectar TruckersMP / Convoy
  multiplayer e desativar (risco de banimento e de dessincronização).
- Nunca versionar binários do jogo, dumps, bancos do IDA/Ghidra, tabelas do Cheat Engine
  ou trechos de código descompilado do jogo (`re/local/` é ignorado pelo git). Documentar
  achados com offsets, assinaturas e descrições em `docs/re/`.
- Nunca versionar o SCS SDK (baixar em `sdk/`, já ignorado).

---

## 2. Padrões de engenharia

### 2.1 Código
- C++17, MSVC (VS 2022), CMake ≥ 3.20, `/W4 /permissive-`, **zero warnings**.
- Formatação: `.clang-format` do repo (rodar antes de commitar). `.editorconfig` vale para o resto.
- Nomes: tipos `PascalCase`, funções/variáveis `camelCase`, membros privados com `_` no
  final (`registry_`), constantes `kPascalCase`, namespace `tc`. Identificadores em inglês;
  mensagens de log e textos para o jogador em PT-BR.
- RAII sempre; nada de `new`/`delete` cru; sem variáveis globais mutáveis fora de
  singletons já existentes (`Controller`, backends).
- Funções pequenas, uma responsabilidade. Arquivos `.h/.cpp` por componente.
- Sem exceções atravessando callbacks do SDK ou fronteiras da DLL.
- Comentários explicam o **porquê**, não o quê. Nada de código comentado/morto.
- Não inventar offsets, assinaturas ou convenções de chamada: todo valor de memória
  precisa de evidência registrada em `docs/re/`.

### 2.2 Threads (crítico)
- Callbacks de telemetria (`frame_start`, `configuration`, canais) e de entrada rodam na
  **thread do jogo**.
- A thread de atalhos (`HotkeyListener`) **só enfileira** ações (`Controller::enqueue`).
- Toda escrita/chamada em memória do jogo acontece **somente** na thread do jogo,
  dentro de `Controller::tick()` (`frame_start`).
- `TrailerRegistry` é protegido por mutex; não segurar o lock ao chamar backends.

### 2.3 Acesso à memória do jogo
- Validar todo ponteiro antes de ler (`VirtualQuery` / helper `safeRead<T>`), com guarda SEH.
- Assinaturas AOB únicas (o scanner rejeita padrão ambíguo), com versão do jogo travada.
- Falha em resolver qualquer assinatura → backend fica desativado e loga o motivo. Nunca
  "tentar mesmo assim".
- Tabela de assinaturas por versão em arquivo de dados (`signatures/<versao>.ini`), não no código.

### 2.4 Testes
- Lógica pura (classificação de reboques, montagem das articulações, parser de `.ini`,
  parser de padrão AOB, seleção de backend) tem teste unitário em `tests/` (alvo CMake
  separado, sem depender do jogo; usar doctest/Catch2 via FetchContent).
- Toda mudança no backend de memória precisa de roteiro de teste manual no PR (composição,
  versão do jogo, resultado) — ver §4.

### 2.5 Pull requests
- PR de `feature/*`, `fix/*`, `re/*` → `dev`. Descrição: o quê, por quê, como testou.
- Build Release limpa + testes unitários passando antes de abrir.

---

## 3. Arquitetura atual (v0.1)

```
src/
  plugin.cpp            exports scs_telemetry_init/shutdown e scs_input_init/shutdown
  trailer_registry.*    estado do cavalo/reboques (telemetria), classificação carreta/dolly, articulações
  controller.*          fila de ações, seleção de articulação, trava de velocidade, escolha de backend
  coupling_backend.h    interface ICouplingBackend
  input_backend.*       dispositivo virtual (Input SDK) -> controle nativo; só articulação 0
  memory_backend.*      backend experimental; scanner AOB + trava de versão; detach/attach = TODO
  hotkeys.*             teclado com o jogo em foco
  status_writer.*       Documentos\ETS2TrailerControl\status.json
  config.* / log.*      .ini ao lado da DLL e log do jogo ([TrailerControl])
config/trailer_control.ini
scripts/promote-to-main.ps1
```

Modelo: unidade 0 = cavalo, unidade k = `trailer.(k-1)`. Articulação i liga a unidade i à i+1.
Backends em ordem de preferência: `InputBackend` (oficial) → `MemoryBackend`.

---

## 4. Ambiente de testes (PC do Kelvin)

- ETS2 instalado com a versão **travada** (Steam → Propriedades → Betas, escolher a versão
  alvo) para os offsets não mudarem durante o trabalho. Registrar a versão em `docs/re/README.md`.
- Ferramentas: x64dbg, Ghidra ou IDA, Cheat Engine, ReClass.NET, Process Hacker.
- SCS SDK em `sdk/`. Build: `cmake -S . -B build -A x64` e `cmake --build build --config Release`.
- Deploy: copiar `trailer_control.dll` + `.ini` para `<ETS2>\bin\win_x64\plugins\`.
- Rodar o jogo com `-nointro -developer` (console e log mais verbosos).
- Saves de teste versionados em `tests/saves/` (perfil com: carreta simples, bitrem,
  rodotrem com dolly, rodotrês, HCT, e um reboque de mod popular).

### Matriz de teste manual (preencher no PR)
| Composição | Articulação | Desengatar | Engatar | Save/Load ok | Obs |
|---|---|---|---|---|---|
| Simples | cavalo | | | | |
| Bitrem | cavalo / quinta roda bitrem | | | | |
| Rodotrem + dolly | cavalo / lança dolly / quinta roda dolly | | | | |
| Rodotrês | todas | | | | |
| HCT | todas | | | | |
| Mod | todas | | | | |

---

## 5. Roadmap — o que precisa ser feito

Status: `[ ]` a fazer, `[~]` em andamento, `[x]` feito. Atualizar aqui a cada entrega.

### Fase 0 — Infraestrutura do repositório
- [x] Estrutura CMake, plugin com telemetria + input, detecção de reboques
- [x] Branches `main`/`dev`, regras, script de promoção
- [x] `CHANGELOG.md`
- [ ] Alvo `tests/` com doctest + testes do registry, config e parser AOB
- [ ] GitHub Actions: build Windows x64 + testes (SDK baixado no CI, não versionado)
- [ ] Validar no jogo: nomes dos canais `trailer.N.connected`, atributos de config,
      controle semântico `trailer`; corrigir o que divergir

### Fase 1 — Mapeamento (engenharia reversa, só leitura)
Objetivo: entender como o jogo representa a corrente de reboques.
- [ ] **RE-1** Ponto de entrada: o próprio despachante de telemetria do exe lê o estado de
      cada reboque para preencher `trailer.N.*`. Achar a string `"connected"`/o código que
      escreve o canal → chegar ao objeto do cavalo e à lista de reboques.
- [ ] **RE-2** Estrutura da corrente: cavalo → reboque 0 → (dolly) → reboque 1 …
      Mapear no ReClass: ponteiro "próximo reboque", ponteiro "reboque anterior/trator",
      flag de engatado, objeto da junta física (hook/fifth wheel), posição do engate.
- [ ] **RE-3** Diferenciar tipos de engate na estrutura (quinta roda × lança/drawbar) e
      confirmar como o dolly aparece (unidade própria na corrente?).
- [ ] **RE-4** Documentar tudo em `docs/re/structures.md` com offsets por versão.
- [ ] Implementar `MemoryReader` seguro (`safeRead<T>`, validação de ponteiro) e um
      comando de debug que despeja a corrente lida da memória no log, comparando com a
      telemetria (as duas visões precisam bater).

### Fase 2 — Desengate de qualquer articulação
- [ ] **RE-5** Rotina nativa de desengate: partir da ação do controle "trailer" (ou de um
      breakpoint de escrita na flag de engatado ao apertar T) até a função que desfaz a
      junta. Registrar assinatura AOB, convenção de chamada e parâmetros.
- [ ] **RE-6** Verificar se a mesma rotina aceita uma junta intermediária
      (reboque 0 ↔ dolly, dolly ↔ reboque 1, bitrem). Se não, achar a rotina da junta
      do reboque (a que o jogo usa ao montar/desmontar composições) ou reproduzir a
      sequência: remover junta física → atualizar ponteiros da corrente → baixar pés de
      apoio / aplicar freio no reboque solto → notificar telemetria/UI.
- [ ] `MemoryBackend::detach(joint)` chamando a rotina na thread do jogo, com validação
      pré/pós (a telemetria precisa refletir a mudança no frame seguinte).
- [ ] Reboque solto fica estável (pés de apoio, freio, sem cair/explodir física).
- [ ] Ações: desengatar tudo, último, selecionado — funcionando em todas as composições da matriz.

### Fase 3 — Engate em qualquer ponto livre
- [ ] **RE-7** Rotina nativa de engate e sua checagem de proximidade (distância/ângulo).
- [ ] Busca do reboque solto mais próximo de cada engate livre (posições via
      `trailer.N.world.placement` + `hook.position`, ou via memória para reboques fora da corrente).
- [ ] Engatar reboque/dolly atrás de um reboque (não só no cavalo), respeitando tipo de
      engate compatível (quinta roda × lança).
- [ ] Feedback quando não houver nada compatível ao alcance.

### Fase 4 — Robustez
- [ ] Assinaturas por versão em `signatures/<versao>.ini` + ferramenta para validar uma
      versão nova (relatório de quais padrões resolveram)
- [ ] Desativar backend de memória em multiplayer (TruckersMP/Convoy)
- [ ] Comportamento com save/load, trabalho (entrega de carga) com parte da composição
      solta, reboques próprios (trailer ownership)
- [ ] Guarda SEH em todas as chamadas internas; nenhum crash do jogo é aceitável

### Fase 5 — Interface
- [ ] Overlay in-game (D3D11 hook ou app externo lendo `status.json`) mostrando a
      composição, articulação selecionada e estado
- [ ] Mensagens no HUD do jogo, se houver rotina reutilizável

---

## 6. Como o Claude deve trabalhar aqui
- Ler este arquivo inteiro antes de começar; seguir a fase atual do roadmap.
- Trabalhar sempre em branch a partir da `dev`; marcar itens do roadmap ao concluir.
- Achados de RE: primeiro documentar em `docs/re/` (com evidência), depois codificar.
- Não afirmar que algo funciona no jogo sem teste real registrado; dizer claramente o que
  foi só compilado e o que foi testado.
- Respeitar §1.1 em **todo** commit e PR.
