// Batuta - "Exportar para o celular": pacote .batpack, importacao e envio por Wi-Fi
#pragma once
#include "Engine.h"

namespace bt
{
constexpr int tcpPort = 47800, udpPort = 47801;

struct PackResult { bool ok = false; juce::String message; };

// Renderiza cada musica em um arquivo OGG "split stereo" (L = guias, R = VS em mono),
// converte os one-shots e empacota tudo em um unico arquivo .batpack (zip).
PackResult exportPack (const Project& project, const juce::File& dest,
                       std::function<void (float, juce::String)> progress);

// Descompacta um .batpack na biblioteca do aparelho; devolve o project.json resultante.
PackResult importPack (const juce::File& pack, const juce::File& libraryDir, juce::File& projectFile);

struct Peer { juce::String name, ip; };
std::vector<Peer> discoverPeers (int timeoutMs = 1500);
PackResult sendPack (const juce::String& ip, const juce::File& pack, std::function<void (float)> progress);

// No celular: responde a busca na rede e recebe pacotes.
class PackReceiver : private juce::Thread
{
public:
    PackReceiver (juce::String deviceName, juce::File tempDir);
    ~PackReceiver() override;
    std::function<void (juce::File)> onReceived;      // thread de mensagens
    std::function<void (float)> onProgress;           // thread de mensagens
private:
    void run() override;
    struct Responder;
    std::unique_ptr<Responder> responder;
    juce::StreamingSocket server;
    juce::String name; juce::File dir;
};

juce::String localIpAddress();
}
