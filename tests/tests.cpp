// Testes que rodam sem o jogo: protocolo, JSON e o modelo do app.
// Uso: ctest -C Release  (ou rodar tc_tests.exe direto)
#include "blinker_sound.h"
#include "command_inbox.h"
#include "json.h"
#include "model.h"
#include "hotkey_text.h"
#include "shared/ipc.h"
#include "shared/ini_edit.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int g_failures = 0;
int g_checks = 0;

void check(bool cond, const char *what, int line) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("FALHOU (linha %d): %s\n", line, what);
    }
}
#define CHECK(c) check((c), #c, __LINE__)

void testJson() {
    using tc::json::Value;
    Value v;
    const std::string doc = R"({"a": 1.5, "b": [true, false, null], "c": {"d": "x\"yç"}})";
    CHECK(tc::json::parse(doc, v));
    CHECK(v["a"].asNumber() == 1.5);
    CHECK(v["b"].size() == 3);
    CHECK(v["b"][0].asBool() == true);
    CHECK(v["b"][2].isNull());
    CHECK(v["c"]["d"].asString() == "x\"y\xC3\xA7");
    CHECK(v["nao_existe"]["nem_isso"].isNull());
    CHECK(!tc::json::parse("{\"a\": ", v));       // arquivo pela metade
    CHECK(!tc::json::parse("{\"a\": 1} lixo", v));
    CHECK(tc::json::parse("[]", v) && v.size() == 0);
}

void testCommands() {
    tc::ExternalCommand c;
    CHECK(tc::parseCommandLine("17 desengatar 2", c) && c.seq == 17 && c.action == "desengatar 2");
    CHECK(tc::parseCommandLine("18 desengatar_tudo", c) && c.action == "desengatar_tudo");
    CHECK(tc::parseCommandLine("1732540000000 engatar 0", c) && c.seq == 1732540000000LL);
    CHECK(!tc::parseCommandLine("", c));
    CHECK(!tc::parseCommandLine("abc desengatar 1", c));
    CHECK(!tc::parseCommandLine("5 Desengatar 1", c));  // so minusculas
    CHECK(!tc::parseCommandLine("5 desengatar -1", c)); // argumento so digitos
    CHECK(!tc::parseCommandLine("5 rm;rf 1", c));
}

const char *kStatus = R"JSON({
  "protocol": 1, "plugin_version": "0.2.0", "exe_version": "1.62.3.3",
  "timestamp_ms": TS, "paused": false, "speed_kmh": 0.40, "require_stopped": true, "max_speed_kmh": 3.00,
  "input_backend": true, "input_active": false, "memory_backend": true, "memory_status": "ativo (1.62.3.3)",
  "combination": "Rodotrem / Double com dolly", "chain_type": "double", "selected_joint": 1,
  "last_message": "ok", "last_ok": true,
  "last_command": {"seq": 42, "ok": false, "result": "engates a 3.10 m"},
  "units": [
    {"index": 0, "kind": "cavalo", "name": "Scania", "id": "", "body": "", "connected": true},
    {"index": 1, "kind": "carreta", "name": "Grain", "id": "x", "body": "", "connected": true},
    {"index": 2, "kind": "dolly", "name": "Dolly", "id": "y", "body": "", "connected": true}
  ],
  "joints": [
    {"index": 0, "front": 0, "rear": 1, "kind": "Quinta roda do cavalo", "coupled": true, "source": "telemetria", "backend": "entrada"},
    {"index": 1, "front": 1, "rear": 2, "kind": "Lanca do dolly", "coupled": false, "source": "memoria", "backend": "memoria"}
  ]
})JSON";

std::string statusWithTs(unsigned long long ts) {
    std::string s = kStatus;
    s.replace(s.find("TS"), 2, std::to_string(ts));
    return s;
}

void testStatus() {
    const unsigned long long now = tc::ipc::nowUnixMs();
    tc::json::Value v;
    CHECK(tc::json::parse(statusWithTs(now - 500), v));
    tc::app::GameState s;
    CHECK(tc::app::stateFromJson(v, now, s));
    CHECK(s.connected);
    CHECK(!s.paused);
    CHECK(s.memoryReady);
    CHECK(s.units.size() == 3);
    CHECK(s.joints.size() == 2);
    CHECK(s.joints[1].fromMemory && !s.joints[1].coupled);
    CHECK(s.lastCommandSeq == 42 && !s.lastCommandOk);
    CHECK(s.exeVersion == L"1.62.3.3");

    CHECK(tc::json::parse(statusWithTs(now - 10000), v));
    CHECK(tc::app::stateFromJson(v, now, s));
    CHECK(!s.connected); // batimento velho = jogo fechado

    CHECK(tc::app::heartbeatFresh(now + 500, now));    // relogio adiantado
    CHECK(!tc::app::heartbeatFresh(now + 60000, now)); // absurdo
    CHECK(!tc::app::heartbeatFresh(0, now));
}

void testIniEdit() {
    const std::string src = "; topo\r\n[geral]\r\nexigir_parado = 1\r\nvelocidade_max_kmh = 3   ; comentario\r\n\r\n"
                            "[atalhos]\r\nabrir_tela = CTRL+MINUS\r\n";
    const std::string out = tc::ini::apply(src, {{"geral", "velocidade_max_kmh", "5"},
                                                  {"geral", "nova", "x;y"},
                                                  {"memoria", "habilitado", "0"},
                                                  {"ATALHOS", "abrir_tela", "CTRL+F9"}});
    CHECK(tc::ini::get(out, "geral", "velocidade_max_kmh") == "5");
    CHECK(out.find("; comentario") != std::string::npos); // comentario preservado
    CHECK(out.find("; topo") == 0);
    CHECK(tc::ini::get(out, "geral", "nova") == "xy");      // ';' removido
    CHECK(tc::ini::get(out, "memoria", "habilitado") == "0"); // secao nova
    CHECK(tc::ini::get(out, "atalhos", "abrir_tela") == "CTRL+F9");
    CHECK(tc::ini::get(out, "geral", "exigir_parado") == "1");
    CHECK(out.find("\r\n") != std::string::npos && out.find("\n\n") == std::string::npos); // mantem CRLF
    // a chave nova fica dentro de [geral], antes de [atalhos]
    CHECK(out.find("nova = xy") < out.find("[atalhos]"));
}

void testHotkeyText() {
    using namespace tc::app;
    CHECK(makeCombo(true, false, false, VK_OEM_MINUS) == "CTRL+MINUS");
    CHECK(makeCombo(true, false, false, VK_SUBTRACT) == "CTRL+MINUS"); // "-" do numerico
    CHECK(makeCombo(true, true, false, VK_F9) == "CTRL+SHIFT+F9");
    CHECK(makeCombo(false, false, true, 'T') == "ALT+T");
    CHECK(makeCombo(true, false, false, VK_SHIFT).empty()); // so modificador
    CHECK(makeCombo(true, false, false, VK_VOLUME_UP).empty()); // nao suportada
    CHECK(prettyCombo("CTRL+MINUS") == L"Ctrl + -");
    CHECK(prettyCombo("alt+numpad3") == L"Alt + Num 3");
    CHECK(prettyCombo("CTRL+SHIFT+F9") == L"Ctrl + Shift + F9");
}

void testDemo() {
    tc::app::DemoSource demo;
    tc::app::GameState s;
    CHECK(demo.poll(s));
    CHECK(s.joints.size() == 3 && s.joints[1].coupled);
    const long long seq = demo.send("desengatar 1");
    Sleep(450);
    CHECK(demo.poll(s));
    CHECK(!s.joints[1].coupled);
    CHECK(s.lastCommandSeq == seq && s.lastCommandOk);
    demo.send("desengatar 1"); // ja solta
    Sleep(450);
    CHECK(demo.poll(s));
    CHECK(!s.lastCommandOk);
}
} // namespace

void testBlinkerSound() {
    using tc::BlinkerSoundAction;
    using tc::decideBlinkerSound;
    // (apagada antes, acesa agora, ligado, pausado, engatado)
    CHECK(decideBlinkerSound(false, true, true, false, true) == BlinkerSoundAction::Play);
    CHECK(decideBlinkerSound(true, true, true, false, true) == BlinkerSoundAction::None);   // ja tocou
    CHECK(decideBlinkerSound(true, false, true, false, true) == BlinkerSoundAction::Stop);  // luz apagou
    CHECK(decideBlinkerSound(false, false, true, false, true) == BlinkerSoundAction::None);
    CHECK(decideBlinkerSound(false, true, false, false, true) == BlinkerSoundAction::None); // desligado
    CHECK(decideBlinkerSound(true, true, false, false, true) == BlinkerSoundAction::Stop);  // desligou tocando
    CHECK(decideBlinkerSound(false, true, true, true, true) == BlinkerSoundAction::None);   // jogo pausado
    CHECK(decideBlinkerSound(false, true, true, false, false) == BlinkerSoundAction::None); // sem reboque
    CHECK(decideBlinkerSound(true, true, true, false, false) == BlinkerSoundAction::Stop);  // soltou o reboque

    const auto wav = tc::makeBlinkerBeepWav(60);
    CHECK(wav.size() > 44 && std::string(wav.begin(), wav.begin() + 4) == "RIFF");
    CHECK(std::string(wav.begin() + 8, wav.begin() + 12) == "WAVE");
    const std::uint32_t dataBytes = wav[40] | (wav[41] << 8) | (wav[42] << 16) | (static_cast<std::uint32_t>(wav[43]) << 24);
    CHECK(dataBytes + 44 == wav.size());
    // Volume 0 gera silencio; volume maior gera amostras maiores.
    auto peak = [](const std::vector<std::uint8_t> &w) {
        int p = 0;
        for (size_t i = 44; i + 1 < w.size(); i += 2) {
            const int s = static_cast<std::int16_t>(w[i] | (w[i + 1] << 8));
            p = std::max(p, s < 0 ? -s : s);
        }
        return p;
    };
    CHECK(peak(tc::makeBlinkerBeepWav(0)) == 0);
    CHECK(peak(tc::makeBlinkerBeepWav(100)) > peak(tc::makeBlinkerBeepWav(30)));
    CHECK(peak(tc::makeBlinkerBeepWav(100)) < 32767);
}

int main() {
    testBlinkerSound();
    testJson();
    testCommands();
    testStatus();
    testIniEdit();
    testHotkeyText();
    testDemo();
    std::printf("%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
