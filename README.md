# ETS2 Trailer Control — v0.1 (desengate)

Plugin nativo (DLL) para Euro Truck Simulator 2 que detecta a composição de reboques e
permite engatar/desengatar cada articulação.

## O que a v0.1 faz

| Recurso | Status |
|---|---|
| Detectar cavalo + até 10 reboques (jogo original e mods) | ✅ |
| Classificar unidades: carreta / dolly (palavras-chave configuráveis p/ mods) | ✅ |
| Identificar a composição: simples, bitrem, rodotrem, rodotrês/tritrem, HCT | ✅ |
| Mapear cada articulação: quinta roda do cavalo, lança do dolly, quinta roda do dolly, quinta roda bitrem, engate de lança | ✅ |
| Desengatar/engatar **cavalo ↔ conjunto** | ✅ (controle nativo, via Input SDK) |
| Desengatar/engatar **articulações internas** (dolly, bitrem, rodotrem) | ⚠️ infraestrutura pronta, falta engenharia reversa (ver abaixo) |
| Atalhos de teclado, seleção de articulação, trava de velocidade | ✅ |
| `Documentos\ETS2TrailerControl\status.json` com o estado ao vivo | ✅ |

### Limitação importante

O SDK oficial da SCS (Telemetry + Input) **só lê** o estado dos reboques e **só consegue
apertar os controles que o jogo já tem**. O jogo só oferece "Engatar/Desengatar reboque",
que age entre o cavalo e o conjunto inteiro. Não há API para soltar o dolly ou a segunda
carreta de um bitrem.

Para isso existe o `MemoryBackend`: ele atua direto na memória do `eurotrucks2.exe` usando
assinaturas de bytes por versão do jogo. A v0.1 traz o scanner de padrões, a trava de versão
e a configuração no `.ini`, mas **as assinaturas e a chamada interna ainda precisam ser
descobertas** (IDA/Ghidra/x64dbg) — o código não inventa endereços. Enquanto isso, o backend
fica desligado e o plugin avisa no log quando uma articulação interna não pode ser acionada.

## Compilar

Requisitos: Visual Studio 2022 (C++ desktop) e CMake 3.20+.

1. Baixe o **SCS SDK** (telemetry + input) em
   <https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry> e extraia na pasta
   `sdk/` deste projeto (deve existir `sdk/include/scssdk_telemetry.h` e `sdk/include/scssdk_input.h`).
2. Compile:

```powershell
cmake -S . -B build -A x64
```

```powershell
cmake --build build --config Release
```

Saída: `build/Release/trailer_control.dll` + `trailer_control.ini`.

## Instalar

1. Copie `trailer_control.dll` e `trailer_control.ini` para
   `...\Euro Truck Simulator 2\bin\win_x64\plugins\` (crie a pasta `plugins` se não existir).
2. Abra o jogo e aceite o aviso de "SDK features".
3. **Opções → Controles**: no dispositivo **Trailer Control**, associe
   "Engatar / Desengatar reboque" (só no modo `generic`, o padrão).

## Atalhos padrão

| Atalho | Ação |
|---|---|
| `Ctrl+Shift+D` | Desengatar o conjunto inteiro do cavalo |
| `Ctrl+Shift+U` | Desengatar a última articulação engatada |
| `Ctrl+Shift+←/→` | Selecionar articulação |
| `Ctrl+Shift+X` | Desengatar a articulação selecionada |
| `Ctrl+Shift+E` | Engatar a articulação selecionada (sem reboque: engata no cavalo) |
| `Ctrl+Shift+L` | Listar a composição no `game.log.txt` |

Todos configuráveis em `trailer_control.ini`. Mensagens aparecem no console do jogo e em
`Documentos\Euro Truck Simulator 2\game.log.txt` com o prefixo `[TrailerControl]`.

## Mods

A detecção usa os atributos que o próprio jogo publica para cada reboque (`id`, `name`,
`body.type`, `chain.type`, marca), então funciona com reboques de mods sem configuração.
Se um dolly de mod não for reconhecido, adicione a palavra do id dele em
`[deteccao] palavras_dolly`. Use `Ctrl+Shift+L` para ver os ids.

## Estrutura

```
src/
  plugin.cpp            entradas scs_telemetry_* e scs_input_*
  trailer_registry.*    estado do cavalo/reboques, classificação, articulações
  controller.*          fila de comandos, seleção, trava de velocidade, escolha do backend
  coupling_backend.h    interface comum dos backends
  input_backend.*       dispositivo virtual -> controle nativo de engate (articulação 0)
  memory_backend.*      backend experimental p/ articulações internas (desligado)
  hotkeys.*             leitura de teclado com o jogo em foco
  status_writer.*       status.json
  config.* / log.*      .ini e log do jogo
config/trailer_control.ini
```

## Próximos passos

1. Engenharia reversa do desacoplamento interno → completar `MemoryBackend::detach/attach`.
2. Engate "em qualquer ponto": localizar o reboque solto mais próximo de cada engate livre
   (posição via canal `trailer.N.world.placement` + `hook.position`).
3. Overlay in-game lendo o `status.json`.
