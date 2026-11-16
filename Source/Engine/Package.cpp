#include "Package.h"

namespace bt
{
using namespace juce;

static std::unique_ptr<AudioFormatWriter> oggWriter (const File& f, double sr, int channels)
{
    f.deleteFile();
    OggVorbisAudioFormat ogg;
    std::unique_ptr<OutputStream> os (f.createOutputStream());
    if (os == nullptr) return {};
    return ogg.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (channels)
                                        .withBitsPerSample (16).withQualityOptionIndex (6));
}

static bool renderSong (const Project& project, int songIdx, const File& out, int& totalBars)
{
    const double sr = 44100.0; const int block = 1024;
    Engine e (true);
    e.exportLinear = true; e.forceSplit = true;
    e.project = project;
    e.project.songs = { project.songs[(size_t) songIdx] };
    e.songIndex = 0;
    e.sampleRate = sr;
    e.prepare (sr, block);

    totalBars = 0;
    for (auto& s : e.song().scenes) totalBars += s.bars;

    auto w = oggWriter (out, sr, 2);
    if (w == nullptr) return false;
    AudioBuffer<float> buf (2, block);
    e.launchScene (0);
    const int64 maxSamples = (int64) (sr * 60.0 * 30.0);
    for (int64 done = 0; done < maxSamples; done += block)
    {
        e.process (buf.getArrayOfWritePointers(), 2, block);
        w->writeFromAudioSampleBuffer (buf, 0, block);
        if (! e.playing.load()) break;
    }
    return true;
}

PackResult exportPack (const Project& project, const File& dest, std::function<void (float, String)> progress)
{
    auto tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("mds_pack_" + String::toHexString (Random::getSystemRandom().nextInt()));
    tmp.createDirectory();
    tmp.getChildFile ("audio").createDirectory();
    struct Cleanup { File d; ~Cleanup() { d.deleteRecursively(); } } cleanup { tmp };

    Project mobile = project;
    mobile.midi.clear();
    AudioFormatManager fm; fm.registerBasicFormats();
    const int n = (int) project.songs.size();

    for (int i = 0; i < n; ++i)
    {
        if (progress) progress ((float) i / (float) (n + 1), "Renderizando: " + project.songs[(size_t) i].name);
        auto& src = project.songs[(size_t) i];
        auto& dst = mobile.songs[(size_t) i];
        dst.tracks.clear(); dst.scenes.clear(); dst.clips.clear();

        bool hasAudio = false, hasCue = false;
        for (size_t s = 0; s < src.clips.size(); ++s)
            for (size_t t = 0; t < src.clips[s].size() && t < src.tracks.size(); ++t)
                if (! src.clips[s][t].empty()) { hasAudio = true; if (src.tracks[t].isCue) hasCue = true; }
        if (! hasAudio) continue;

        const String rel = "audio/song_" + String (i + 1) + ".ogg";
        int bars = 0;
        if (! renderSong (project, i, tmp.getChildFile (rel), bars))
            return { false, String::fromUTF8 ("Falha ao renderizar a m\xc3\xbasica: ") + src.name };

        Scene sc; sc.name = String::fromUTF8 ("M\xc3\xbasica"); sc.bars = jmax (1, bars); sc.follow = followStop;
        dst.scenes.push_back (sc);
        std::vector<Clip> row;
        if (hasCue) { Track t; t.name = "Guias"; t.isCue = true; t.outL = 0; t.outR = -1; dst.tracks.push_back (t); Clip c; c.file = rel; c.srcChannel = 0; row.push_back (c); }
        { Track t; t.name = "VS"; t.outL = 1; t.outR = -1; dst.tracks.push_back (t); Clip c; c.file = rel; c.srcChannel = 1; row.push_back (c); }
        dst.clips.push_back (row);
    }

    if (progress) progress ((float) n / (float) (n + 1), "Convertendo one-shots");
    for (int i = 0; i < numPads; ++i)
    {
        auto& d = mobile.pads[i];
        if (d.file.isEmpty()) continue;
        std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (File (d.file)));
        if (r == nullptr) { d.file.clear(); continue; }
        const String rel = "audio/pad_" + String (i + 1) + ".ogg";
        auto mono = synth::loadMono (r.get(), 44100.0);
        if (auto w = oggWriter (tmp.getChildFile (rel), 44100.0, 1))
        {
            const float* ch[1] = { mono.data() };
            w->writeFromFloatArrays (ch, 1, (int) mono.size());
        }
        d.file = rel;
    }
    for (int b = 0; b < numBuses; ++b) { mobile.buses[b].outL = b <= busVoice ? 0 : 1; mobile.buses[b].outR = -1; }

    tmp.getChildFile ("project.json").replaceWithText (JSON::toString (mobile.toVar()));

    ZipFile::Builder zip;
    zip.addFile (tmp.getChildFile ("project.json"), 9, "project.json");
    for (auto& f : tmp.getChildFile ("audio").findChildFiles (File::findFiles, false))
        zip.addFile (f, 0, "audio/" + f.getFileName());
    dest.deleteFile();
    FileOutputStream os (dest);
    if (! os.openedOk() || ! zip.writeToStream (os, nullptr)) return { false, String::fromUTF8 ("N\xc3\xa3o foi poss\xc3\xadvel gravar o pacote.") };
    if (progress) progress (1.0f, "Pronto");
    return { true, dest.getFullPathName() };
}

PackResult importPack (const File& pack, const File& libraryDir, File& projectFile)
{
    ZipFile zip (pack);
    if (zip.getNumEntries() == 0 || zip.getIndexOfFileName ("project.json") < 0)
        return { false, String::fromUTF8 ("Arquivo inv\xc3\xa1lido: n\xc3\xa3o \xc3\xa9 um pacote do MDS.") };
    auto dir = libraryDir.getChildFile (File::createLegalFileName (pack.getFileNameWithoutExtension()));
    dir.deleteRecursively();
    dir.createDirectory();
    auto res = zip.uncompressTo (dir);
    if (res.failed()) return { false, res.getErrorMessage() };
    projectFile = dir.getChildFile ("project.json");
    return { true, {} };
}

//======================================================================================
String localIpAddress()
{
    for (auto& a : IPAddress::getAllAddresses (false))
        if (a != IPAddress::local (false) && ! a.toString().startsWith ("169.254")) return a.toString();
    return "?";
}

std::vector<Peer> discoverPeers (int timeoutMs)
{
    std::vector<Peer> found;
    DatagramSocket sock (true);
    if (! sock.bindToPort (0)) return found;
    const char q[] = "BATUTA?";
    StringArray targets { "255.255.255.255" };
    for (auto& a : IPAddress::getAllAddresses (false))
        targets.addIfNotAlreadyThere (IPAddress::getInterfaceBroadcastAddress (a).toString());
    for (auto& t : targets) sock.write (t, udpPort, q, (int) sizeof (q));

    const auto end = Time::getMillisecondCounter() + (uint32) timeoutMs;
    while (Time::getMillisecondCounter() < end)
    {
        if (sock.waitUntilReady (true, 150) != 1) continue;
        char buf[256] {}; String ip; int port = 0;
        const int got = sock.read (buf, 255, false, ip, port);
        if (got > 7 && String (buf, 7) == "BATUTA!")
        {
            Peer p { String::fromUTF8 (buf + 7, got - 7).trim(), ip };
            bool dup = false; for (auto& f : found) dup = dup || f.ip == ip;
            if (! dup) found.push_back (p);
        }
    }
    return found;
}

PackResult sendPack (const String& ip, const File& pack, std::function<void (float)> progress)
{
    StreamingSocket s;
    if (! s.connect (ip, tcpPort, 4000))
        return { false, String::fromUTF8 ("N\xc3\xa3o foi poss\xc3\xadvel conectar em ") + ip + String::fromUTF8 (". Confira se o app est\xc3\xa1 aberto no celular e se os dois est\xc3\xa3o na mesma rede Wi-Fi.") };
    FileInputStream in (pack);
    if (! in.openedOk()) return { false, "Pacote ilegivel." };
    const auto nameUtf = pack.getFileName().toStdString();
    const int32 nameLen = (int32) nameUtf.size(); const int64 size = in.getTotalLength();
    bool ok = s.write ("BTPK", 4) == 4 && s.write (&nameLen, 4) == 4 && s.write (nameUtf.data(), nameLen) == nameLen && s.write (&size, 8) == 8;
    HeapBlock<char> buf (65536);
    int64 sent = 0;
    while (ok && sent < size)
    {
        const int got = in.read (buf, 65536);
        if (got <= 0) break;
        ok = s.write (buf, got) == got;
        sent += got;
        if (progress) progress ((float) sent / (float) jmax ((int64) 1, size));
    }
    char ack = 0;
    if (ok && sent == size && s.waitUntilReady (true, 15000) == 1 && s.read (&ack, 1, true) == 1 && ack == 'K')
        return { true, {} };
    return { false, String::fromUTF8 ("A transfer\xc3\xaancia foi interrompida.") };
}

struct PackReceiver::Responder : public Thread
{
    Responder (String n) : Thread ("MDS descoberta"), name (std::move (n)) { startThread(); }
    ~Responder() override { signalThreadShouldExit(); sock.shutdown(); stopThread (1500); }
    void run() override
    {
        if (! sock.bindToPort (udpPort)) return;
        while (! threadShouldExit())
        {
            if (sock.waitUntilReady (true, 300) != 1) continue;
            char buf[64] {}; String ip; int port = 0;
            const int got = sock.read (buf, 63, false, ip, port);
            if (got >= 7 && String (buf, 7) == "BATUTA?")
            {
                const auto reply = ("BATUTA!" + name).toStdString();
                sock.write (ip, port, reply.data(), (int) reply.size());
            }
        }
    }
    DatagramSocket sock { false };
    String name;
};

PackReceiver::PackReceiver (String deviceName, File tempDir) : Thread ("MDS recepcao"), name (std::move (deviceName)), dir (std::move (tempDir))
{
    responder.reset (new Responder (name));
    startThread();
}

PackReceiver::~PackReceiver()
{
    signalThreadShouldExit();
    server.close();
    stopThread (2000);
}

void PackReceiver::run()
{
    if (! server.createListener (tcpPort)) return;
    while (! threadShouldExit())
    {
        std::unique_ptr<StreamingSocket> c (server.waitForNextConnection());
        if (c == nullptr) { if (! threadShouldExit()) wait (200); continue; }
        char magic[4] {}; int32 nameLen = 0; int64 size = 0;
        if (c->read (magic, 4, true) != 4 || memcmp (magic, "BTPK", 4) != 0) continue;
        if (c->read (&nameLen, 4, true) != 4 || nameLen <= 0 || nameLen > 512) continue;
        HeapBlock<char> nm (nameLen + 1, true);
        if (c->read (nm, nameLen, true) != nameLen || c->read (&size, 8, true) != 8 || size <= 0) continue;
        dir.createDirectory();
        auto f = dir.getChildFile (File::createLegalFileName (String::fromUTF8 (nm, nameLen)));
        f.deleteFile();
        bool ok = false;
        {
            FileOutputStream out (f);
            HeapBlock<char> buf (65536);
            int64 got = 0; int lastPct = -1;
            while (out.openedOk() && got < size && ! threadShouldExit())
            {
                if (c->waitUntilReady (true, 10000) != 1) break;
                const int r = c->read (buf, (int) jmin ((int64) 65536, size - got), false);
                if (r <= 0) break;
                out.write (buf, (size_t) r); got += r;
                const int pct = (int) (100 * got / size);
                if (pct != lastPct) { lastPct = pct; MessageManager::callAsync ([this, pct] { if (onProgress) onProgress ((float) pct / 100.0f); }); }
            }
            ok = got == size;
        }
        if (ok)
        {
            c->write ("K", 1);
            MessageManager::callAsync ([this, f] { if (onReceived) onReceived (f); });
        }
    }
}
}
