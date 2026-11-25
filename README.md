# ETS2 Trailer Control

**🇧🇷 Português** · [🇺🇸 English](README.en.md)

Plugin para **Euro Truck Simulator 2** + uma tela (**TrailerControl.exe**) para engatar e
desengatar **cada articulação** da composição: cavalo, carretas e dollies — bitrem, rodotrem,
rodotrês, HCT, do jogo original e de mods.

| | |
|---|---|
| **Versão** | 0.2.0 (experimental) |
| **Jogo** | ETS2 **1.62.x** — desengate de dolly/bitrem mapeado para **1.62.3.3** |
| **Sistema** | Windows 10/11 64 bits |
| **Modo** | single-player e **Convoy**; no **TruckersMP** o desengate interno se desliga sozinho |

## Download e instalação

1. Baixe **`TrailerControl-...-plugins.zip`** na página de
   [Releases](https://github.com/khenrique99/ETS2---Trailer-Control/releases)2. Extraia o conteúdo **dentro da pasta `plugins`** do jogo:
   `...\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins\`
   (crie a pasta `plugins` se ela não existir). Devem ficar lá:
   `trailer_control.dll`, `trailer_control.ini` e `TrailerControl.exe`.
3. Abra o jogo e aceite o aviso de **"SDK features"**.
4. **Opções → Controles**: no dispositivo **Trailer Control**, associe
   **"Engatar / Desengatar reboque"** (necessário para soltar o conjunto do cavalo).
5. No jogo, aperte **Ctrl + -**: a tela abre.

> Salve o jogo antes de usar. O desengate de dolly/bitrem ainda está em teste.

## Como usar

- **Ctrl + -** (o `-` do teclado principal ou do numérico) abre a tela por cima do jogo.
- Clique **Desengatar** / **Engatar** na articulação desejada. A tela some e o jogo volta; se o
  jogo pausou com a tela na frente, o comando roda assim que você voltar.
- **Esc** ou o próprio atalho voltam ao jogo.
- **Configurações** (rodapé da tela): escolha a tecla de atalho — clique em **Alterar…**,
  aperte a combinação e **Salvar**. Vale na hora.
- **Diagnóstico**: grava no `game.log.txt` o que o plugin leu da memória (só leitura) — útil para
  relatar problemas.
- Para engatar caminhão parado: limite de 3 km/h. Para engatar uma articulação interna, os
  engates precisam estar a até 1,5 m.

Outras opções (velocidade máxima, liga/desliga do desengate interno, distância, palavras que
identificam dolly de mod) ficam no `trailer_control.ini`, com comentários explicando cada uma.

## O que funciona

| Recurso | Status |
|---|---|
| Detectar cavalo + até 10 reboques (jogo original e mods) | ✅ |
| Classificar carreta / dolly (palavras-chave configuráveis para mods) | ✅ |
| Identificar a composição: simples, bitrem, rodotrem, rodotrês, HCT | ✅ |
| Tela com o desenho da composição e botão por articulação | ✅ testado sem o jogo |
| Desengatar/engatar **cavalo ↔ conjunto** | ✅ controle nativo do jogo (Input SDK) |
| Desengatar/engatar **articulações internas** (dolly, bitrem, rodotrem) | 🧪 só **1.62.3.3**, falta testar no jogo |
| Estado real das articulações internas (lido da memória) | 🧪 idem |
| Atalho configurável pela tela | ✅ testado sem o jogo |
| Desengate interno no **Convoy** (`permitir_convoy` no `.ini`) | 🧪 liberado, testado sem o jogo |
| Desligar o desengate interno no **TruckersMP** | ✅ testado sem o jogo |

### Como funciona o desengate interno

O SDK oficial da SCS só consegue "apertar" o controle de engate que o jogo já tem, e ele só age
entre o cavalo e o conjunto. Para as articulações internas, o plugin chama as rotinas do próprio
jogo, achadas por engenharia reversa do `eurotrucks2.exe` 1.62.3.3
([docs/re/](docs/re/README.md)):

- **Desengatar a articulação k**: rotina "desengatar" do reboque k (desfaz a junta física com o
  reboque da frente). O jogo continua dono do objeto — nada é liberado.
- **Engatar a articulação k**: a mesma rotina que o jogo usa ao montar a composição, só com os
  engates próximos.

Travas: só liga se a versão do exe for exatamente uma das mapeadas, se os bytes de cada função
conferirem e se a vtable de cada reboque apontar para as funções esperadas; toda leitura e chamada
é protegida contra exceções; desliga no TruckersMP. Em qualquer falha, essa parte se desliga e o
resto continua funcionando. Num update do jogo (1.62.4, 1.63…), o desengate interno fica
indisponível até a tabela da versão nova ser adicionada.

## Para desenvolvedores

### Compilar

Requisitos: Visual Studio 2022 ou Build Tools (C++ desktop); o CMake vem junto.

1. Baixe o **SCS SDK** em <https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry>
   e extraia em `sdk/` (deve existir `sdk/include/scssdk_telemetry.h`). O SDK não é versionado.
2. Compile, teste e empacote:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

Saída em `dist\`: só o zip para a pasta `plugins` (o que vai na release). Com `-TestPackage`,
gera também `build\...-teste.zip` (plugin + jogo falso). Sem o SDK, o CMake compila só a tela.

### Testes (sem o jogo)

- `tc_tests` — JSON, protocolo de comandos, edição do `.ini`, nomes de teclas, modelo da tela.
- `plugin_selftest` — o **jogo falso** (`tests/fake_game.cpp`) carrega a DLL de verdade pelo SDK,
  simula um rodotrem e manda comandos como a tela faria, conferindo o `status.json`.

Roteiro de teste no jogo: [docs/TESTE-NO-JOGO.md](docs/TESTE-NO-JOGO.md).

### Comunicação plugin ↔ tela

Arquivos em `Documentos\ETS2TrailerControl\`: `status.json` (plugin → tela, batimento a cada 1 s)
e `command.txt` (tela → plugin). Protocolo em [src/shared/ipc.h](src/shared/ipc.h).

### Estrutura

```
src/
  plugin.cpp            entradas scs_telemetry_* e scs_input_*
  trailer_registry.*    estado do cavalo/reboques, classificação, articulações, posições
  controller.*          fila de comandos, seleção, trava de velocidade, TruckersMP/Convoy
  input_backend.*       dispositivo virtual -> controle nativo de engate (articulação 0)
  memory_backend.*      articulações internas via memória do jogo (tabela por versão)
  command_inbox.*       lê command.txt da tela
  app_launcher.*        abre/mostra a tela (atalho)
  status_writer.*       status.json
  shared/               protocolo plugin <-> tela, edição de .ini
  hotkeys.* config.* log.*
app/                    TrailerControl.exe (Win32 + GDI+, sem dependências)
tests/                  testes + jogo falso
docs/re/                engenharia reversa por versão do jogo
tools/ghidra/           scripts headless do Ghidra
```

## Aviso

Projeto de fã, sem vínculo com a SCS Software. Euro Truck Simulator 2 é marca da SCS Software.
Use por sua conta e risco; o desengate interno altera o comportamento do jogo em tempo real.
