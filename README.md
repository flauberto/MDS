# Batuta

DAW de direção musical: sessão por cenas, click com mapa de andamento, VS/stems com roteamento
multicanal (Windows) e módulo de palco portátil em split stereo (Android), ligados por Ableton Link
e por exportação de setlists.

- `Source/Engine` — motor de áudio compartilhado (C++/JUCE)
- `Source/Desktop` — aplicativo Windows
- `Source/Mobile` — aplicativo Android
- `Tests` — testes offline do motor
- `Android` — projeto Projucer e projeto Gradle gerado

## Compilar

O GitHub Actions (`.github/workflows/build.yml`) gera `Batuta.exe` e `Batuta.apk` a cada push;
baixe-os em **Actions → execução mais recente → Artifacts**.

Local (Windows): `cmake -B build -G "Visual Studio 17 2022" -A x64` e
`cmake --build build --config Release --target Batuta`. Para ASIO, descompacte o ASIO SDK da
Steinberg em `third_party/asiosdk`.

Local (Android): abra `Android/Builds/Android` no Android Studio. Se alterar `Android/Batuta.jucer`,
regenere com `Projucer --resave Android/Batuta.jucer`.

## Licença

GPLv3. Usa JUCE (AGPLv3), Ableton Link (GPLv2+) e, no Windows, o ASIO SDK (GPLv3).
ASIO é marca registrada da Steinberg Media Technologies GmbH. A chave `Android/batuta.keystore`
é pública (senha `batuta`) e serve apenas para builds de uso próprio.
