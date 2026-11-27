# Trailer Control 0.4.0-beta.1 — guia de teste no jogo

**🇧🇷 Português** · [🇺🇸 English](TESTING-IN-GAME.md)

Esta versão foi montada e testada **sem o jogo** (com um "jogo falso" que carrega o plugin de
verdade). O que falta confirmar é o comportamento dentro do ETS2 — principalmente o desengate
do dolly / bitrem, que mexe na memória do jogo. Se puder, rode o roteiro abaixo e conte o
resultado.

> **Antes de começar:** salve o jogo. O desengate interno só liga no ETS2 **1.62.3.3**, em
> single-player ou no **Convoy**; em outra versão ou no TruckersMP ele se desliga sozinho e só o
> desengate do cavalo funciona.

## Arquivos

| Arquivo | Para quê |
|---|---|
| `trailer_control.dll` | o plugin |
| `trailer_control.ini` | configurações (o atalho dá para trocar pela tela) |
| `TrailerControl.exe` | a tela de engate — o atalho no jogo abre ela |

## 1. Instalar

1. Coloque **`trailer_control.dll`, `trailer_control.ini` e `TrailerControl.exe`** em
   `...\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins\`
   (crie a pasta `plugins` se não existir). O zip `...-plugins.zip` da release já vem com os três:
   é só extrair dentro dessa pasta.
2. Abra o ETS2. Na primeira vez aparece o aviso de **"SDK features"** — aceite.
3. **Opções → Controles**: procure o dispositivo **Trailer Control** e associe a entrada dele
   a **"Engatar / Desengatar reboque"**. Sem isso o desengate do cavalo (articulação 0) não age.

## 2. Como usar

- No jogo, aperte **Ctrl + -** (o `-` do teclado principal ou do numérico): o painel abre **dentro
  do jogo**, por cima da imagem, sem tirar o jogo de primeiro plano. Precisa do ETS2 em **DirectX 11**
  (o padrão). Se o plugin não conseguir se pendurar no vídeo, ele abre o `TrailerControl.exe`.
- Clique **Desengatar** / **Engatar** na articulação que quiser. Com o painel aberto o mouse e o
  teclado vão só para ele (o caminhão não recebe nada); o jogo continua rodando atrás.
- **Esc**, **Fechar** ou o próprio atalho fecham o painel. Sem mouse: **↑ ↓** escolhem a
  articulação e **Enter** engata/desengata.
- **Som da seta** e **Volume** ficam na faixa de baixo do painel; **Diagnóstico** escreve no `game.log.txt`.
- O atalho que abre o painel e as demais opções ficam no `trailer_control.ini`.
  `[tela] modo = externo` volta para a janela separada (`TrailerControl.exe`).

> **Beta do painel:** o painel dentro do jogo é novo e foi testado só com um "jogo falso" em DirectX 11.
> Se o jogo travar ao abrir o painel, ou o mouse/teclado se comportarem de forma estranha, anote e
> use `modo = externo` até a correção.

## 3. Roteiro de teste

| # | Faça | Esperado | Deu certo? |
|---|---|---|---|
| 1 | Entre no jogo com o caminhão | No console (`~`): `[TrailerControl] Backend de memoria ativo para 1.62.3.3` | |
| 2 | Aperte **Ctrl + -** | A tela abre na frente com **Jogo conectado** | |
| 3 | Aperte **Esc** | Volta para o jogo | |
| 4 | Pegue uma carga **simples** (1 carreta) e abra a tela | Cavalo + Carreta 1, articulação 0 "Engatada" | |
| 5 | Parado, clique **Desengatar** na articulação 0 | A tela some, o jogo volta e solta a carreta | |
| 6 | Encoste de novo, abra a tela e clique **Engatar** | O jogo engata | |
| 7 | Pegue uma carga de **rodotrem / double com dolly** (ou bitrem) | A tela mostra todas as unidades; articulações 1, 2… com "estado lido da memória" | |
| 8 | Clique **Diagnóstico** | No `game.log.txt` aparece `=== Diagnostico de memoria ===` com os reboques e `vtable ... OK` | |
| 9 | Parado em lugar plano, **Desengatar** a articulação 1 (lança do dolly) | Dolly + carreta de trás ficam soltos; a tela mostra "Solta" | |
| 10 | Sem mexer no caminhão, **Engatar** a articulação 1 | Engata de novo (engates a menos de 1,5 m) | |
| 11 | Ande um pouco, pare e tente **Engatar** a 1 com os reboques afastados | A tela recusa: "engates a X m (maximo 1.50 m)" | |
| 12 | Num bitrem, faça o mesmo com a articulação 1 | Igual aos passos 9–10 | |
| 13 | Em **Configurações**, troque o atalho (ex.: Ctrl + F9), salve, volte ao jogo e use o atalho novo | A tela abre com o atalho novo; o cabeçalho mostra "Esc ou Ctrl + F9" | |
| 14 | (Opcional) Num comboio **Convoy**, repita os passos 9–10 | Funciona igual; anote como os outros jogadores veem o reboque solto | |
| 15 | Com carreta engatada, ligue a seta (e o pisca-alerta) | Um apito a cada acendida da luz; ao desligar a seta, para | |
| 16 | Na tela, clique **Som da seta** (some o ✓) e ligue a seta | Sem som; clicar de novo volta o apito | |
| 17 | Sem reboque engatado, ligue a seta | Sem apito | |
| 18 | Arraste a barra de volume (0–100) | Toca uma prévia; o apito da seta muda de volume na hora; 0 = mudo | |
| 19 | Edite `volume` em `[som]` no `.ini` com o jogo aberto | Vale em até 1 s, sem reiniciar | |
| 20 | Desengatar uma articulação interna (dolly/bitrem) e olhar as mangueiras entre os reboques | As mangueiras somem do reboque solto (ficam penduradas no da frente); ao engatar de novo voltam | |
| 21 | Com a articulação solta, dirigir o caminhão para longe (centenas de metros) | O jogo não fecha; se fechar, anote a distância e envie o `game.log.txt` | |
| 22 | Com o painel aberto, mexa o mouse e aperte teclas do jogo (W, A, S, D, setas do jogo) | O cursor do painel aparece e segue o mouse; o caminhão e a câmera não reagem | |
| 23 | Clique **Desengatar** na articulação 0 pelo painel | Funciona igual ao app externo; o painel continua aberto e a linha vira "Solta" | |
| 24 | Feche com **Esc**, **Fechar** e com **Ctrl + -** (uma vez cada) | O painel some e o mouse volta a girar a câmera; nenhuma tecla fica "presa" | |
| 25 | Abra o painel com o jogo em **tela cheia**, **janela** e **sem bordas**; troque a resolução | Aparece nos três; o tamanho acompanha a resolução | |

Anote também o que **não** for esperado: reboque que "pula", física estranha, travamento,
reboque sumindo, aviso na entrega da carga, tela que não aparece por cima do jogo etc.

## 4. Como relatar

Abra uma [issue](https://github.com/khenrique99/ETS2---Trailer-Control/issues) com:

1. A tabela acima preenchida (ou só os passos que deram errado) e a versão exata do jogo.
2. As linhas `[TrailerControl]` de `Documentos\Euro Truck Simulator 2\game.log.txt`
   (principalmente o bloco do **Diagnóstico**).
3. O arquivo `Documentos\ETS2TrailerControl\status.json`, se algo estiver errado na tela.
4. Se o jogo fechar sozinho: o que você clicou e, se existir, o `crash.log` mais recente em
   `Documentos\Euro Truck Simulator 2\`.

## 5. Se der problema

- **O jogo trava/fecha ao desengatar o dolly:** no `trailer_control.ini` (pasta `plugins`),
  coloque `habilitado = 0` em `[memoria]`. O resto continua funcionando.
- **Ctrl + - não abre a tela:** procure "Painel no jogo" no `game.log.txt`. Se ele cair no app externo, confira se o `TrailerControl.exe` está na pasta `plugins` junto da
  DLL. Dá para abrir o exe direto também; ele conecta sozinho. Se outra coisa do jogo usa
  Ctrl + -, troque `abrir_tela` no `.ini`. Se o painel dentro do jogo não abrir (log: "Painel no jogo"), use `[tela] modo = externo`.
- **Não quer o desengate interno no Convoy:** `permitir_convoy = 0` em `[memoria]` no `.ini`.
- **Tela fica em "Aguardando o jogo":** a DLL está em `bin\win_x64\plugins`? Aceitou o aviso de SDK?
- **Articulações internas "indisponível":** a mensagem ao lado diz o motivo (versão diferente,
  bytes do exe não conferem, TruckersMP, desligado no `.ini`).
- **Engatar recusado mesmo com os reboques encostados:** aumente `distancia_max_engate_m` no
  `.ini` (ou `0` para não checar) e informe o valor que a tela mostrou.
- **"Não consegui gravar" ao salvar Configurações:** a pasta do jogo está protegida; abra o
  `TrailerControl.exe` como administrador uma vez ou edite `abrir_tela` no `.ini` à mão.

## 6. Teste sem o jogo (opcional)

Para quem compila o projeto: `scripts\build.ps1 -TestPackage` gera `build\...-teste.zip`. Extraia e,
em PowerShell, na pasta dele:

```powershell
.\teste-sem-jogo\fake_game.exe .\plugin\trailer_control.dll 60
```

Isso carrega o plugin num ETS2 "de mentira" (rodotrem parado) por 60 s. Abra o
`plugin\TrailerControl.exe` ao mesmo tempo: ele conecta e o botão da articulação 0 solta/engata o
conjunto. As internas ficam indisponíveis aqui (o backend de memória só existe no jogo real).

Autoteste automático: `.\teste-sem-jogo\fake_game.exe .\plugin\trailer_control.dll --selftest`
