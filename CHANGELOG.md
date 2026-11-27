# Changelog

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/),
versionamento [SemVer](https://semver.org/lang/pt-BR/).

## [Unreleased]

## [0.3.2] - 2026-11-27
### Corrigido
- Fechamento do jogo ao soltar uma 2ª articulação com a 1ª já solta e longe: o plugin recriava a
  mangueira entre reboques já separados (cabo de dezenas de metros). Agora só liga o par que
  continua engatado; o resto fica com a mangueira em repouso. Hipótese a partir do `game.log` —
  **não testado no jogo**.
- Engatar uma articulação não força mais o engate de articulações de trás que continuam soltas.
- Câmera continuava recuada/centrada como se os reboques soltos ainda estivessem engatados: o
  comprimento da cadeia (`FUN_140775120`) agora para no primeiro reboque solto. **Não testado no
  jogo**.

## [0.3.1] - 2026-11-26
### Corrigido
- Desengate de articulação interna (dolly, bitrem, rodotrem) agora remove as mangueiras entre os
  dois reboques, como o jogo faz ao soltar a carreta do cavalo; ao engatar de novo, recria.
  Pode corrigir o fechamento do jogo ao se afastar do reboque solto (mangueira esticada) —
  **não testado no jogo**; a causa do crash é hipótese. Análise em `docs/re/structures-1.62.3.3.md`.

## [0.3.0] - 2026-11-25
### Adicionado
- Alarme sonoro externo da seta (`[som]` no `.ini`): apita junto com cada acendida da luz do
  pisca enquanto houver reboque engatado. Na tela, faixa "Som da seta" com liga/desliga e barra
  de volume de 0 a 100 (clique ou arraste; toca um apito de prévia). Toca pelo Windows (não
  segue o volume do ETS2, sem 3D). Usa os canais `truck.light.lblinker` / `truck.light.rblinker`.
  Testado sem o jogo (lógica, geração do som e status); falta ouvir no jogo.
- O plugin vigia o `trailer_control.ini` e recarrega sozinho em até 1 s quando ele muda (edição
  à mão ou pela tela), sem precisar reiniciar o jogo nem mandar comando.
- `LICENSE` (PolyForm Noncommercial 1.0.0, a mesma do Airbrake) em nome de Kelvin Henrique
  Ferreira: uso pessoal e não comercial liberado; uso comercial só com autorização do autor.


## [0.2.0] - 2026-11-25
### Adicionado
- App externo **TrailerControl.exe**: desenho da composição, botão de engate/desengate por
  articulação, status do jogo, sempre no topo, modo demonstração (`--demo`).
- Canal de comandos app → plugin (`command.txt`) e `status.json` com batimento, versão do jogo,
  estado real das articulações e resultado de cada comando (`src/shared/ipc.h`).
- `MemoryBackend` para o ETS2 **1.62.3.3**: desengate/engate das articulações internas (dolly,
  bitrem, rodotrem) pelas rotinas do próprio jogo, com checagem de versão, bytes das funções,
  vtables e chamadas protegidas. Engate só com os engates a até `distancia_max_engate_m`.
- Estado das articulações internas lido da memória.
- Diagnóstico da memória (botão no app), só leitura.
- Atalho único **Ctrl + -** no jogo: abre/mostra a tela (`TrailerControl.exe` na pasta `plugins`);
  Esc ou Ctrl + - na tela voltam ao jogo.
- Tela de **Configurações** no app para escolher o atalho que abre a tela (captura a combinação
  apertada); grava `abrir_tela` no `trailer_control.ini` preservando comentários e o plugin
  aplica na hora (comando `recarregar`).
- Comandos de engate dados com o jogo pausado ficam na fila e rodam ao voltar ao jogo.
- Backend de memória funciona em single-player e no **Convoy** (multiplayer oficial da SCS,
  controlado por `[memoria] permitir_convoy`, padrão `1`); no **TruckersMP** se desliga sempre
  (`core_ets2mp.dll` no processo).
- Documentação em inglês: `README.en.md` e `docs/TESTING-IN-GAME.md`.
- Release só com o zip para extrair direto na pasta `plugins` (`scripts/build.ps1 -GameTag ETS2-1.62`;
  `-TestPackage` gera à parte o pacote com o jogo falso).
- Testes sem o jogo: `tc_tests` e o "jogo falso" (`fake_game --selftest`) que carrega a DLL real.
- `scripts/build.ps1`: compila, testa e gera o pacote `dist/TrailerControl-<versão>.zip`.
- Engenharia reversa do 1.62.3.3 em `docs/re/` e scripts headless do Ghidra em `tools/ghidra/`.

### Corrigido
- Plugin não compilava contra o SCS SDK real (`scs_input_event_t::value_bool`).
- Ao soltar o conjunto do cavalo, as articulações internas apareciam como soltas.

### Alterado
- `[memoria]` no `.ini`: agora `habilitado`, `versao_jogo` (opcional) e
  `distancia_max_engate_m`; as chaves de assinatura saíram.

### Removido
- Atalhos de engate/desengate/seleção/listar (`Ctrl+Shift+...`): tudo passou para a tela.
  `[atalhos]` agora só tem `abrir_tela`.

## [0.1.0] - 2026-11-24
### Adicionado
- Plugin DLL com SCS Telemetry SDK + Input SDK.
- Detecção de cavalo e até 10 reboques (original e mods), classificação carreta/dolly.
- Identificação da composição (simples, bitrem, rodotrem, rodotrês, HCT) e das articulações.
- Desengate/engate cavalo ↔ conjunto pelo controle nativo (dispositivo virtual).
- Atalhos configuráveis, trava de velocidade, `status.json`.
- Backend de memória experimental (scanner AOB e trava de versão; desengate interno pendente).
