# Engenharia reversa

Achados sobre o `eurotrucks2.exe`, por versão do jogo. Só offsets, assinaturas e descrições —
nunca binários, dumps, bancos do Ghidra/IDA ou código descompilado do jogo (isso fica em
`re/local/`, ignorado pelo git).

| Versão do jogo | Arquivo | Estado |
|---|---|---|
| **1.62.3.3** (Steam, x64) | [structures-1.62.3.3.md](structures-1.62.3.3.md) | mapeado por análise estática; **falta validar no jogo** |

## Como reproduzir

1. Copie o `eurotrucks2.exe` da versão alvo para `re/local/`.
2. Ghidra 12.2 (JDK 25), análise headless — os scripts estão em [`tools/ghidra/`](../../tools/ghidra):

```powershell
$env:GHIDRA_HEADLESS_MAXMEM = '12G'
analyzeHeadless.bat re\local\ghidra_proj ets2 -import re\local\eurotrucks2.exe -scriptPath tools\ghidra -preScript DisableHeavyAnalyzers.java -postScript DumpFuncs.java re\local\dump.c 0x14096982f
```

- `DisableHeavyAnalyzers.java`: o analisador "Create Address Tables" estoura a memória neste exe.
- `DumpFuncs.java`: descompila as funções que contêm os endereços passados (+ chamadores/chamados).
- `VtableStarts.java` / `FindVtables.java`: o motor da SCS não tem RTTI; acha o início real das
  vtables pelos construtores que gravam o ponteiro.
- `FindFieldWrites.java`: lista todas as escritas em `[reg + deslocamento]` (ex.: quem grava `+0x1058`).

Pontos de partida que sobrevivem a updates (procure as strings e siga as referências):
`[actor] Trailer disconnection disabled`, `[phys_trailer] No slave hook locator on trailer`,
`@@trailer_decoupling@@`.

## Ao sair uma versão nova do jogo

1. Repetir a análise e conferir cada endereço da tabela em `src/memory_backend.cpp` (`kLayouts`).
2. Criar `structures-<versão>.md` e acrescentar a linha na tabela acima.
3. Acrescentar a versão em `kLayouts` (RVAs + primeiros 16 bytes de cada função).
