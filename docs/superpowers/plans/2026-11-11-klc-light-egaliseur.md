# klc-light : égaliseur musical (spectre) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `klc-light visualizer on|off` affiche sur le clavier le spectre du son joué par l'ordinateur, aux couleurs du profil appliqué, et rend ce profil à l'arrêt.

**Architecture:** Le binaire gagne un mode `visualizer run`, lancé par une unité systemd utilisateur. Une boucle à 30 images par seconde enchaîne `Spectrum` (FFT et niveaux par bande), `SpectrumRenderer` (niveaux et profil → couleur par touche) et `StreamSession` (mode direct de GG sur le contrôleur, tenu attaché par `Device`). Un socket local sert à `apply` pour prévenir le service et à l'éditeur pour recevoir les images.

**Tech Stack:** C++17, Qt 6 (Core, Gui, Network, DBus, Quick, Test), qmake6, hidapi, libpipewire-0.3, libsystemd.

**Spec:** `docs/superpowers/specs/2026-11-11-klc-light-egaliseur-design.md` (mesures : `docs/origins.md`, section du 2026-11-11)

## Global Constraints

- Aucun envoi au vrai clavier pendant le développement : tests avec `FakeTransport` / `FakeBinder`, essais manuels avec `KLC_LIGHT_FAKE_DEVICE=1`. Seule la tâche 11 touche le GS76, avec l'utilisateur.
- Une image `0C` porte **57 touches au plus** par paquet ; `0D 00 02` ne part **qu'une fois**, à l'entrée en mode direct ; 10 ms entre deux paquets, 30 ms entre les paquets d'entrée.
- Chaque paquet est envoyé une seule fois. Au premier envoi refusé : plus aucun envoi, détachement, sortie en erreur.
- Un seul cycle d'attache par séance d'égaliseur (et un par reprise après veille) ; `Device::MinIntervalMs` (5000) est respecté avant chaque attache.
- Codes de sortie : 0 succès, 1 erreur, 2 contrôleur absent du bus (comme `apply`).
- Le comportement de `apply`, `off`, `restore`, `list` est inchangé quand le service ne tourne pas ; `state.json` garde son format.
- Rapports feature de 524 octets, rapports de sortie de 64 octets, identifiant de commande en premier octet (convention de `protocol.cpp`).
- Tests : `bin/test` ; les 247 tests existants restent verts à chaque tâche.
- Libellés et messages en anglais dans le code, traduits dans `src/i18n/fr.json` (et `i18n/` du plugin).
- Commits en anglais, impératif, terminés par `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`. Rien n'est poussé ni tagué par ce plan.

## Review Focus

1. **Aucun profil appliqué, ou profil appliqué supprimé** : `visualizer on` doit refuser avant d'attacher le clavier, avec un message clair, et non streamer puis laisser le clavier sans profil à rendre. Test en tâche 6.
2. **Silence prolongé puis reprise** : le clavier reste noir sans rien envoyer sur l'USB, et le spectre repart dès le retour du son ; le bruit de fond ne doit pas être amplifié jusqu'à remplir le clavier. Tests en tâches 2 et 4.
3. **Profil invalide passé à `apply` pendant le flux** : l'erreur de validation est rapportée comme d'habitude, `state.json` n'est pas modifié et le service n'est pas prévenu. Test en tâche 7.
4. **Service tué pendant que l'éditeur écoute** : l'éditeur doit revenir à l'aperçu normal et le bouton à « éteint », sans rester figé sur la dernière image. Test en tâche 9.
5. **Fichier de socket orphelin après un plantage** : `apply` ne doit pas croire le service vivant (il enverrait rien), et le service suivant doit pouvoir rouvrir le socket. Tests en tâches 6 et 7.

## File Structure

| Fichier | Responsabilité |
|---|---|
| `src/protocol.{h,cpp}` (modif.) | Paquets d'entrée en mode direct et paquets d'une image |
| `src/spectrum.{h,cpp}` (nouveau) | Échantillons → niveaux par bande |
| `src/spectrumrenderer.{h,cpp}` (nouveau) | Niveaux + profil → image |
| `src/device.{h,cpp}` (modif.) | Séance ouverte : `begin` / `send` / `end` ; `applyLatest` bâti dessus |
| `src/streamsession.{h,cpp}` (nouveau) | Entrée en mode direct, images, retour au profil |
| `src/printdevice.h` (nouveau) | `PrintTransport` et `NullBinder` du mode simulé |
| `src/audiosource.h` (nouveau) | Interface `AudioSource` |
| `src/pipewiresource.{h,cpp}` (nouveau) | Capture du moniteur de la sortie par défaut |
| `src/visualizer.{h,cpp}` (nouveau) | `VisualizerService` : boucle, socket, signaux, veille ; `visualizerPing` / `visualizerRequest` côté client |
| `src/cli.{h,cpp}` (modif.) | `visualizer on|off`, champ `visualizer` de `list`, règle d'`apply` pendant le flux, `resolveProfile` |
| `src/main.cpp` (modif.) | Aiguillage de `visualizer run` |
| `src/backend.{h,cpp}`, `src/ProfileBar.qml`, `src/KeyboardView.qml` (modif.) | Bouton et aperçu en direct |
| `pkgbuild/klc-light-visualizer.service` (nouveau) | Unité utilisateur |
| `omarchy-plugin/swald.klc-light/` (modif.) | Bouton du widget |
| `klc-light.pro`, `tests/klc-light_tests.pro`, `aur/PKGBUILD`, `pkgbuild/PKGBUILD`, `.github/workflows/*.yml`, `README.md` (modif.) | Construction, paquet, CI, documentation |

---

### Task 1 : paquets du mode direct

**Files:**
- Modify: `src/protocol.h`, `src/protocol.cpp`
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct KeyColor { quint8 hid; QColor color; };
  using Frame = QList<KeyColor>;
  constexpr int kStreamKeysPerPacket = 57;
  // Every code the stream addresses, in the order of the enter packets.
  QList<quint8> streamHids(const Layout &layout);
  // Four region packets (Alphanum, Enter, Modifiers, Numpad) with every key black in
  // mask 2, then the output report 0D 00 02.
  QList<Packet> streamEnterPackets(const Layout &layout);
  // Feature reports 0C 00 <n> 00 + n × [hid R G B], unused bytes FF.
  QList<Packet> streamFramePackets(const Frame &frame);
  ```

- [ ] **Step 1 : écrire les tests qui échouent**

```cpp
void streamEnterPackets_matchStaticWithMaskTwo()
{
    const Layout &layout = Layout::gs76();
    const QList<Packet> enter = streamEnterPackets(layout);
    Profile black; black.base.color = Qt::black;
    const QList<Packet> stat = buildPackets(black, layout).packets; // 4 regions + output 09
    QCOMPARE(enter.size(), 5);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(enter[i].kind, Packet::Feature);
        QCOMPARE(enter[i].data.size(), 524);
        QByteArray expected = stat[i].data;
        for (int k = 0; k < quint8(expected.at(2)); ++k)
            expected[4 + k * 12 + 9] = 2; // host-stream
        QCOMPARE(enter[i].data, expected);
    }
    QCOMPARE(enter[4].kind, Packet::Output);
    QCOMPARE(enter[4].data.size(), 64);
    QCOMPARE(enter[4].data.left(3), QByteArray::fromHex("0d0002"));
    QCOMPARE(enter[4].data.mid(3), QByteArray(61, '\0'));
}

void streamFramePackets_splitAt57()
{
    const QList<quint8> hids = streamHids(Layout::gs76());
    QCOMPARE(hids.size(), 113);
    QCOMPARE(QSet<quint8>(hids.begin(), hids.end()).size(), 113);
    Frame frame;
    for (int i = 0; i < hids.size(); ++i)
        frame.append({hids[i], QColor(i, 255 - i, 7)});
    const QList<Packet> packets = streamFramePackets(frame);
    QCOMPARE(packets.size(), 2);
    QCOMPARE(packets[0].data.left(4), QByteArray::fromHex("0c003900")); // 57
    QCOMPARE(packets[1].data.left(4), QByteArray::fromHex("0c003800")); // 56
    for (const Packet &p : packets) {
        QCOMPARE(p.kind, Packet::Feature);
        QCOMPARE(p.data.size(), 524);
    }
    QCOMPARE(packets[0].data.mid(4, 4), QByteArray(1, char(hids[0])) + QByteArray::fromHex("00ff07"));
    QCOMPARE(packets[1].data.mid(4 + 56 * 4), QByteArray(524 - 4 - 56 * 4, '\xff'));
    QCOMPARE(streamFramePackets({}).size(), 0);
}
```

- [ ] **Step 2 : lancer `bin/test`** — attendu : échec de compilation (`streamEnterPackets` non déclaré).
- [ ] **Step 3 : implémenter les trois fonctions dans `src/protocol.cpp`**, en réutilisant `fragment`, `regionPacket(…, true)` et `Layout::regionHids` comme la branche statique de `buildPackets` (codes non nuls, sans doublon) ; ajouter `kMaskHostStream = 2` à l'énumération des masques.
- [ ] **Step 4 : lancer `bin/test`** — attendu : 249 tests réussis.
- [ ] **Step 5 : commit** — `Add the host-stream enter and frame packets`

---

### Task 2 : Spectrum

**Files:**
- Create: `src/spectrum.h`, `src/spectrum.cpp`
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class Spectrum {
  public:
      static constexpr int FftSize = 2048;
      static constexpr double LowHz = 40, HighHz = 16000;
      static constexpr double RangeDb = 30;        // a band 30 dB under the reference reads 0
      static constexpr double FloorRefDb = -30;    // the reference never falls below this (dBFS)
      static constexpr double RefDecayDbPerFrame = 0.2;
      static constexpr double FallPerFrame = 0.08; // most a level drops in one frame
      Spectrum(int sampleRate, int bands);
      void push(const float *mono, int count);     // keeps the last FftSize samples
      QList<double> frame();                       // one level 0..1 per band, low to high
      double bandLowHz(int band) const;            // lower edge; band `bands` gives HighHz
  };
  ```

Décisions que les tests ne fixent pas :
- FFT radix-2 écrite dans `spectrum.cpp` (`std::complex<float>`), fenêtre de Hann, amplitude normalisée pour qu'un sinus pleine échelle lise 0 dBFS.
- Bandes à bords géométriques entre `LowHz` et `HighHz` ; la valeur d'une bande est le maximum des raies qu'elle contient (au moins la raie la plus proche de son centre).
- Référence = le plus fort niveau de bande vu, qui redescend de `RefDecayDbPerFrame` par image, sans passer sous `FloorRefDb`. Niveau brut = `(dB − (référence − RangeDb)) / RangeDb`, borné à 0..1.
- Sortie = niveau brut s'il monte, sinon `max(brut, précédent − FallPerFrame)`.

- [ ] **Step 1 : écrire les tests qui échouent**

```cpp
static QList<double> spectrumOf(Spectrum &s, double hz, double amplitude, int frames, int rate = 48000)
{
    QList<double> levels;
    qint64 n = 0;
    for (int f = 0; f < frames; ++f) {
        QList<float> block(rate / 30);
        for (float &v : block) v = float(amplitude * std::sin(2 * M_PI * hz * double(n++) / rate));
        s.push(block.constData(), block.size());
        levels = s.frame();
    }
    return levels;
}
static int bandOf(const Spectrum &s, int bands, double hz)
{
    for (int b = 0; b < bands; ++b)
        if (hz >= s.bandLowHz(b) && hz < s.bandLowHz(b + 1)) return b;
    return -1;
}

void spectrumBandsAreGeometric()
{
    Spectrum s(48000, 18);
    QCOMPARE(s.bandLowHz(0), 40.0);
    QVERIFY(qAbs(s.bandLowHz(18) - 16000.0) < 1e-6);
    QVERIFY(qAbs(s.bandLowHz(9) - 800.0) < 1e-6); // sqrt(40 × 16000)
}
void spectrumBassToneLightsLowBandOnly()
{
    Spectrum s(48000, 18);
    const QList<double> l = spectrumOf(s, 100, 0.5, 30);
    QCOMPARE(l.size(), 18);
    QVERIFY(l[bandOf(s, 18, 100)] >= 0.9);
    for (int b = bandOf(s, 18, 1000); b < 18; ++b) QVERIFY2(l[b] <= 0.1, qPrintable(QString::number(b)));
}
void spectrumTrebleToneLightsHighBandOnly()
{
    Spectrum s(48000, 18);
    const QList<double> l = spectrumOf(s, 5000, 0.5, 30);
    QVERIFY(l[bandOf(s, 18, 5000)] >= 0.9);
    for (int b = 0; b <= bandOf(s, 18, 500); ++b) QVERIFY(l[b] <= 0.1);
}
void spectrumSilenceIsZero()
{
    Spectrum s(48000, 18);
    for (double v : spectrumOf(s, 100, 0.0, 30)) QCOMPARE(v, 0.0);
}
void spectrumFallsGradually()
{
    Spectrum s(48000, 18);
    const int b = bandOf(s, 18, 100);
    double previous = spectrumOf(s, 100, 0.5, 30)[b];
    bool reachedZero = false;
    for (int f = 0; f < 60; ++f) {
        const double now = spectrumOf(s, 100, 0.0, 1)[b];
        QVERIFY(now <= previous);
        QVERIFY(previous - now <= Spectrum::FallPerFrame + 1e-9);
        previous = now;
        reachedZero = reachedZero || now == 0.0;
    }
    QVERIFY(reachedZero);
}
void spectrumQuietToneStillReads()       // amplitude 0.01 = −40 dBFS
{
    Spectrum s(48000, 18);
    QVERIFY(spectrumOf(s, 100, 0.01, 30)[bandOf(s, 18, 100)] >= 0.5);
}
void spectrumNoiseFloorStaysDark()       // Review Focus 2: amplitude 0.0005 = −66 dBFS
{
    Spectrum s(48000, 18);
    for (double v : spectrumOf(s, 100, 0.0005, 300)) QCOMPARE(v, 0.0);
}
void spectrumResumesAfterLongSilence()   // Review Focus 2
{
    Spectrum s(48000, 18);
    spectrumOf(s, 100, 0.5, 30);
    spectrumOf(s, 100, 0.0, 1800);       // one minute
    QVERIFY(spectrumOf(s, 100, 0.5, 3)[bandOf(s, 18, 100)] >= 0.9);
}
```

- [ ] **Step 2 : lancer `bin/test`** — attendu : échec de compilation (`spectrum.h` absent).
- [ ] **Step 3 : implémenter `Spectrum`** selon l'interface et les décisions ci-dessus.
- [ ] **Step 4 : lancer `bin/test`** — attendu : 257 tests réussis.
- [ ] **Step 5 : commit** — `Add Spectrum: band levels from audio samples`

---

### Task 3 : SpectrumRenderer

**Files:**
- Create: `src/spectrumrenderer.h`, `src/spectrumrenderer.cpp`
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes: `Frame`, `KeyColor`, `streamHids` (tâche 1) ; `Preview::colorAt` ; `Layout::keys()`, `Layout::byHid`.
- Produces:
  ```cpp
  class SpectrumRenderer {
  public:
      static constexpr int Columns = 18, Rows = 6;
      explicit SpectrumRenderer(const Layout &layout);
      void setProfile(const Profile &profile);
      // One entry per streamHids() code, same order. levels.size() == Columns.
      Frame render(const QList<double> &levels) const;
      int columnOf(const QString &keyId) const; // 0 left .. 17 right; -1 if not visible
      int rowOf(const QString &keyId) const;    // 0 top .. 5 bottom; -1 if not visible
  };
  ```

Décisions :
- Sur les touches visibles : `colonne = min(17, floor((x − xmin) / (xmax − xmin) × 18))`, `rangée = min(5, floor((y − ymin) / (ymax − ymin) × 6))`, avec `x`, `y` les centres de `Key`.
- Hauteur de barre `h = niveau × 6`, comptée depuis le bas ; pour une touche à `d = 5 − rangée` : couleur pleine si `h ≥ d + 1`, couleur × `(h − d)` si `d < h < d + 1`, noir sinon.
- Couleur d'une touche = `Preview(profile, layout).colorAt(id, 0)` (la luminosité du profil y est déjà appliquée).
- Les codes de `streamHids()` sans touche visible sont toujours noirs.

- [ ] **Step 1 : écrire les tests qui échouent**

```cpp
static Profile plainProfile(const QColor &c) { Profile p; p.base.color = c; return p; }
static QColor frameColor(const Frame &f, const QString &id)
{
    const quint8 hid = Layout::gs76().byId(id)->hid;
    for (const KeyColor &k : f) if (k.hid == hid) return k.color;
    return QColor();
}

void rendererGridCoversTheKeyboard()
{
    SpectrumRenderer r(Layout::gs76());
    QList<int> perColumn(18, 0), perRow(6, 0);
    for (const Key &k : Layout::gs76().keys()) {
        QVERIFY(r.columnOf(k.id) >= 0 && r.columnOf(k.id) < 18);
        QVERIFY(r.rowOf(k.id) >= 0 && r.rowOf(k.id) < 6);
        ++perColumn[r.columnOf(k.id)];
        ++perRow[r.rowOf(k.id)];
    }
    for (int n : perColumn) QVERIFY(n >= 3);
    for (int n : perRow) QVERIFY(n >= 14);
    QVERIFY(r.columnOf("KEY_ESC") < r.columnOf("KEY_F12"));
    QCOMPARE(r.rowOf("KEY_ESC"), 0);
    QCOMPARE(r.rowOf("KEY_SPACE"), 5);
}
void rendererSilenceIsBlack()
{
    SpectrumRenderer r(Layout::gs76());
    r.setProfile(plainProfile(QColor(10, 200, 30)));
    const Frame f = r.render(QList<double>(18, 0.0));
    QCOMPARE(f.size(), streamHids(Layout::gs76()).size());
    for (int i = 0; i < f.size(); ++i) {
        QCOMPARE(f[i].hid, streamHids(Layout::gs76())[i]);
        QCOMPARE(f[i].color, QColor(Qt::black));
    }
}
void rendererFullLevelShowsTheProfile()
{
    Profile p = plainProfile(QColor(10, 200, 30));
    p.keys.insert("KEY_Q", QColor(255, 0, 0));
    SpectrumRenderer r(Layout::gs76());
    r.setProfile(p);
    const Frame f = r.render(QList<double>(18, 1.0));
    QCOMPARE(frameColor(f, "KEY_Q"), QColor(255, 0, 0));
    QCOMPARE(frameColor(f, "KEY_ESC"), QColor(10, 200, 30));
    for (const KeyColor &k : f) {
        const Key *key = Layout::gs76().byHid(k.hid);
        if (!key || !key->visible) QCOMPARE(k.color, QColor(Qt::black));
    }
}
void rendererPartialLevelDimsTheTop()
{
    SpectrumRenderer r(Layout::gs76());
    r.setProfile(plainProfile(QColor(200, 100, 0)));
    QList<double> levels(18, 0.0);
    levels[r.columnOf("KEY_SPACE")] = 0.25;         // h = 1.5 rows
    const Frame f = r.render(levels);
    for (const Key &k : Layout::gs76().keys()) {
        if (r.columnOf(k.id) != r.columnOf("KEY_SPACE")) { QCOMPARE(frameColor(f, k.id), QColor(Qt::black)); continue; }
        if (r.rowOf(k.id) == 5) QCOMPARE(frameColor(f, k.id), QColor(200, 100, 0));
        else if (r.rowOf(k.id) == 4) QCOMPARE(frameColor(f, k.id), QColor(100, 50, 0));
        else QCOMPARE(frameColor(f, k.id), QColor(Qt::black));
    }
}
void rendererHonoursBrightness()
{
    Profile p = plainProfile(QColor(200, 100, 0));
    p.brightness = 50;
    SpectrumRenderer r(Layout::gs76());
    r.setProfile(p);
    QCOMPARE(frameColor(r.render(QList<double>(18, 1.0)), "KEY_SPACE"), Preview(p, Layout::gs76()).colorAt("KEY_SPACE", 0));
}
```

- [ ] **Step 2 : lancer `bin/test`** — attendu : échec de compilation.
- [ ] **Step 3 : implémenter `SpectrumRenderer`.**
- [ ] **Step 4 : lancer `bin/test`** — attendu : 262 tests réussis.
- [ ] **Step 5 : commit** — `Add SpectrumRenderer: band levels to key colors`

---

### Task 4 : séance ouverte dans Device, StreamSession, mode simulé

**Files:**
- Modify: `src/device.h`, `src/device.cpp`
- Create: `src/streamsession.h`, `src/streamsession.cpp`, `src/printdevice.h`
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes: `streamEnterPackets`, `streamFramePackets`, `Frame` (tâche 1).
- Produces:
  ```cpp
  // Device
  ApplyResult begin();   // lock (LockWaitMs), wait out MinIntervalMs, bind, waitAndOpen, recordSend.
                         // On failure everything is undone (unbind, lock released).
  ApplyResult send(const QList<Packet> &packets, int gapMs = PacketGapMs); // stops at the first refusal
  void end();            // close, unbind, recordSend, release the lock; no-op when not begun
  bool begun() const;
  void setLockWaitMs(int ms);   // LockWaitMs by default; tests set 0

  class StreamSession {
  public:
      static constexpr int EnterGapMs = 30;
      StreamSession(Device &device, const Layout &layout);
      ApplyResult start();                               // begin + enter packets
      ApplyResult show(const Frame &frame);              // nothing sent if equal to the last frame shown
      ApplyResult stop(const QList<Packet> &restore);    // restore packets (if any), then end
      bool running() const;
  };

  // printdevice.h — KLC_LIGHT_FAKE_DEVICE=1
  class PrintTransport : public Transport;  // waitAndOpen true; prints "F <hex>" / "O <hex>" to the QTextStream given
  class NullBinder : public Binder;         // bind/unbind succeed, do nothing
  ```

Décisions :
- `applyLatest` devient `begin` → `build` → `send` → `end`, sous `SignalHold` comme aujourd'hui ; `begin`/`end` seuls ne retiennent aucun signal (le service s'arrête par `SIGTERM`).
- Les erreurs de `begin` gardent les `ApplyError` et les messages actuels.
- Après un `send` refusé dans `StreamSession` : `end()` immédiat, `running()` faux, plus aucun envoi ; `stop` ne renvoie alors rien.
- `end()` écrit `klc-light.last` : l'intervalle court depuis la fin de la séance.

- [ ] **Step 1 : écrire les tests qui échouent**

```cpp
void deviceSessionKeepsTheLockUntilEnd()
{
    QTemporaryDir dir; FakeLog log; FakeTransport t(log); FakeBinder b(log);
    Device d = fakeDevice(t, b, dir, log);
    QCOMPARE(d.begin().error, ApplyError::None);
    QVERIFY(d.begun());
    QCOMPARE(log.events, QStringList({"bind", "open"}));
    FakeLog log2; FakeTransport t2(log2); FakeBinder b2(log2);
    Device other = fakeDevice(t2, b2, dir, log2);
    other.setLockWaitMs(0);
    QCOMPARE(other.apply({Packet{Packet::Output, QByteArray(64, '\0')}}).error, ApplyError::Locked);
    QVERIFY(log2.events.isEmpty());
    QCOMPARE(d.send({Packet{Packet::Output, QByteArray(64, '\0')}}).error, ApplyError::None);
    d.end(); d.end();
    QCOMPARE(log.events, QStringList({"bind", "open", "send", "close", "unbind"}));
    QVERIFY(!d.begun());
}
void deviceBeginFailureReleasesEverything()
{
    QTemporaryDir dir; FakeLog log; FakeTransport t(log); FakeBinder b(log);
    t.openOk = false;
    Device d = fakeDevice(t, b, dir, log);
    QCOMPARE(d.begin().error, ApplyError::DeviceMissing);
    QVERIFY(!d.begun());
    QCOMPARE(log.events.last(), QString("unbind"));
    t.openOk = true;
    QCOMPARE(d.begin().error, ApplyError::None);   // the lock was released
    d.end();
}
void deviceEndStartsTheInterval()
{
    QTemporaryDir dir; FakeLog log; FakeTransport t(log); FakeBinder b(log);
    Device d = fakeDevice(t, b, dir, log);
    d.begin(); log.now += 60'000; d.end();
    log.sleeps.clear();
    d.begin();
    QCOMPARE(log.sleeps.value(0), Device::MinIntervalMs);
    d.end();
}
void streamSessionOrder()
{
    QTemporaryDir dir; FakeLog log; FakeTransport t(log); FakeBinder b(log);
    Device d = fakeDevice(t, b, dir, log);
    StreamSession s(d, Layout::gs76());
    QCOMPARE(s.start().error, ApplyError::None);
    QCOMPARE(log.packets.size(), 5);
    QCOMPARE(log.packets[4].left(3), QByteArray::fromHex("0d0002"));
    QCOMPARE(log.sleeps.mid(log.sleeps.size() - 5), QList<int>(5, StreamSession::EnterGapMs));
    Frame red; for (quint8 h : streamHids(Layout::gs76())) red.append({h, QColor(Qt::red)});
    QCOMPARE(s.show(red).error, ApplyError::None);
    QCOMPARE(log.packets.size(), 7);
    QCOMPARE(s.show(red).error, ApplyError::None);        // identical: nothing sent
    QCOMPARE(log.packets.size(), 7);
    Profile black; black.base.color = Qt::black;
    const QList<Packet> restore = buildPackets(black, Layout::gs76()).packets;
    QCOMPARE(s.stop(restore).error, ApplyError::None);
    QCOMPARE(log.packets.size(), 7 + restore.size());
    QCOMPARE(log.events.mid(log.events.size() - 2), QStringList({"close", "unbind"}));
    QCOMPARE(log.events.count("bind"), 1);
    for (const QByteArray &p : log.packets.mid(5, 2)) QCOMPARE(quint8(p.at(0)), quint8(0x0c));
    QVERIFY(!s.running());
}
void streamSessionStopsAtTheFirstRefusal()
{
    QTemporaryDir dir; FakeLog log; FakeTransport t(log); FakeBinder b(log);
    Device d = fakeDevice(t, b, dir, log);
    StreamSession s(d, Layout::gs76());
    s.start();
    t.failAt = 6;                                         // first frame packet
    Frame red; for (quint8 h : streamHids(Layout::gs76())) red.append({h, QColor(Qt::red)});
    QCOMPARE(s.show(red).error, ApplyError::SendFailed);
    QVERIFY(!s.running());
    QCOMPARE(log.events.last(), QString("unbind"));
    const int sent = log.packets.size();
    Frame blue; for (quint8 h : streamHids(Layout::gs76())) blue.append({h, QColor(Qt::blue)});
    QVERIFY(s.show(blue).error != ApplyError::None);
    s.stop(buildPackets(Profile(), Layout::gs76()).packets);
    QCOMPARE(log.packets.size(), sent);                   // nothing more was sent
}
void streamSessionSilenceSendsNothing()                   // Review Focus 2
{
    QTemporaryDir dir; FakeLog log; FakeTransport t(log); FakeBinder b(log);
    Device d = fakeDevice(t, b, dir, log);
    StreamSession s(d, Layout::gs76());
    s.start();
    Frame dark; for (quint8 h : streamHids(Layout::gs76())) dark.append({h, QColor(Qt::black)});
    for (int i = 0; i < 100; ++i) s.show(dark);
    QCOMPARE(log.packets.size(), 5);                      // the enter packets already left every key black
    s.stop({});
}
```

- [ ] **Step 2 : lancer `bin/test`** — attendu : échec de compilation.
- [ ] **Step 3 : implémenter `Device::begin/send/end/begun` et rebâtir `applyLatest` dessus.** Le verrou devient un membre `std::unique_ptr<QLockFile>`.
- [ ] **Step 4 : lancer `bin/test`** — attendu : les 262 tests antérieurs réussissent toujours (le comportement d'`apply` n'a pas changé), les nouveaux tests de `Device` aussi.
- [ ] **Step 5 : implémenter `StreamSession` et `printdevice.h`.** `StreamSession` considère « toutes les touches noires » comme la dernière image après `start()`.
- [ ] **Step 6 : lancer `bin/test`** — attendu : 268 tests réussis.
- [ ] **Step 7 : commit** — `Add an open device session and StreamSession`

---

### Task 5 : AudioSource et PipewireSource

**Files:**
- Create: `src/audiosource.h`, `src/pipewiresource.h`, `src/pipewiresource.cpp`
- Modify: `klc-light.pro`, `tests/klc-light_tests.pro` (`PKGCONFIG += libpipewire-0.3`), `.github/workflows/*.yml` (paquet `libpipewire` dans la ligne `pacman`)

**Interfaces:**
- Produces:
  ```cpp
  class AudioSource {
  public:
      virtual ~AudioSource() = default;
      virtual bool start(QString *error) = 0;
      virtual void stop() = 0;
      virtual int sampleRate() const = 0;
      // Non-blocking: copies up to `max` mono samples captured since the last call.
      virtual int read(float *out, int max) = 0;
  };
  class PipewireSource : public AudioSource { /* 48000 Hz, float, mono */ };
  ```

Décisions :
- Flux PipeWire de capture avec `PW_KEY_STREAM_CAPTURE_SINK = "true"` et sans cible nommée, pour suivre le moniteur de la sortie par défaut, y compris quand elle change.
- `pw_thread_loop` ; le rappel `process` recopie les échantillons dans un tampon circulaire d'une seconde protégé par un mutex ; `read` le vide depuis le fil principal. Si le tampon déborde, les plus anciens échantillons sont perdus.
- `start` échoue, avec le message de PipeWire, si la connexion au serveur échoue.
- **Avant d'écrire** : lire l'exemple `audio-capture.c` et les en-têtes installés (`/usr/include/pipewire-0.3`, `/usr/include/spa-0.2`) pour les signatures exactes ; ne pas les supposer.

- [ ] **Step 1 : ajouter `libpipewire-0.3` aux deux `.pro` et `libpipewire` à la CI ; créer `audiosource.h`.**
- [ ] **Step 2 : implémenter `PipewireSource`.**
- [ ] **Step 3 : lancer `bin/build` et `bin/test`** — attendu : compilation sans avertissement nouveau, 268 tests réussis.
- [ ] **Step 4 : vérifier à la main, sans clavier.** Ajouter provisoirement dans `tests/` un petit programme (non commité) qui démarre `PipewireSource`, alimente un `Spectrum(48000, 18)` toutes les 33 ms pendant 5 s et imprime les niveaux. Avec `speaker-test -t sine -f 100 -l 1` en cours : la bande de 100 Hz domine. Sans son : tous les niveaux à 0.
- [ ] **Step 5 : commit** — `Add PipewireSource: capture the default output's monitor`

Si l'API se révèle impraticable, s'arrêter et le signaler : le repli prévu par la spec (Qt Multimedia derrière `AudioSource`) est une décision de l'utilisateur.

---

### Task 6 : VisualizerService et `visualizer run`

**Files:**
- Create: `src/visualizer.h`, `src/visualizer.cpp`
- Modify: `src/cli.h`, `src/cli.cpp` (`resolveProfile`), `src/main.cpp`, les deux `.pro` (`QT += network dbus`, `PKGCONFIG += libsystemd`)
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes: `Spectrum`, `SpectrumRenderer`, `StreamSession`, `AudioSource`, `Device`.
- Produces:
  ```cpp
  // cli.h — what `klc-light restore` would send, shared with the service.
  struct ResolvedProfile {
      std::optional<Profile> profile;  // the applied profile (theme's if the applied one is gone); empty if none
      bool off = false;                // state.json says the keyboard is turned off
      QString name;
      QStringList errors;
  };
  ResolvedProfile resolveProfile(const QString &libraryDir, const ThemePaths &paths);

  // visualizer.h
  QString visualizerSocketPath(const QString &runtimeDir);   // <runtimeDir>/klc-light-visualizer.sock
  // Sends one line and returns the one-line answer; empty if nothing is listening (500 ms at most).
  QString visualizerRequest(const QString &socketPath, const QString &line);

  class VisualizerService : public QObject {
      Q_OBJECT
  public:
      static constexpr int FrameMs = 33;
      VisualizerService(Device &device, AudioSource &audio, const Layout &layout, QString libraryDir,
                        ThemePaths paths, QString socketPath);
      ApplyResult start();   // refuses without a profile; audio first, then the keyboard, then the socket
      void tick();           // one frame: read audio, Spectrum, render, show; called by the timer
      void stop();           // restore the profile (black if off), detach, close the socket
      bool running() const;
  signals:
      void failed(const ApplyResult &result);   // a send was refused; the service has already detached
  };
  int runVisualizer(int argc, char *argv[]);    // `klc-light visualizer run`; the exit codes of apply
  ```

Protocole du socket (une ligne par message, UTF-8) :

| Reçu | Effet | Réponse |
|---|---|---|
| `ping` | aucun | `ok` |
| `reload` | relit `resolveProfile`, recolore, met à jour le profil à rendre | `ok` |
| `frames` | la connexion reçoit ensuite chaque image affichée | `ok`, puis `frame <hex>` par image, `<hex>` = suite de `hid R V B` |
| autre | aucun | `error` |

Décisions :
- `start()` sans profil résolu : `ApplyError::Invalid`, message traduit `"Apply a profile before starting the visualizer."`, rien d'attaché.
- Ordre de `start()` : profil → `audio.start` (échec : `ApplyError::Invalid` avec le message de la source) → `StreamSession::start` → `QLocalServer::removeServer(socketPath)` puis `listen`.
- `stop()` rend `buildPackets(profil)` ou, si `off`, le profil tout noir de `klc-light off`.
- `runVisualizer` : `QCoreApplication` ; `HidapiTransport` + `SysfsBinder`, ou `PrintTransport` + `NullBinder` si `KLC_LIGHT_FAKE_DEVICE=1` ; `PipewireSource` ; `QTimer` de `FrameMs` ; `SIGTERM`/`SIGINT` relayés par un tube vers la boucle Qt, qui appelle `stop()` puis sort avec 0 ; `sd_notify(0, "READY=1")` une fois `start()` réussi ; sur `failed` ou échec de `start()` : message sur stderr et code de sortie d'`apply` (2 pour `KeyboardAbsent`, 1 sinon).
- `resolveProfile` reprend la résolution que `runCli` fait aujourd'hui pour `restore` (profil appliqué, sinon celui du thème actif) ; `runCli` l'appelle pour ce cas au lieu de la dupliquer.

- [ ] **Step 1 : écrire les tests qui échouent.** Source de test :

```cpp
class SineSource : public AudioSource {
public:
    double hz = 100, amplitude = 0.5; bool startOk = true; int started = 0; qint64 n = 0;
    bool start(QString *error) override { ++started; if (!startOk) *error = "no audio server"; return startOk; }
    void stop() override {}
    int sampleRate() const override { return 48000; }
    int read(float *out, int max) override {
        const int count = qMin(max, 1600);
        for (int i = 0; i < count; ++i) out[i] = float(amplitude * std::sin(2 * M_PI * hz * double(n++) / 48000));
        return count;
    }
};
```

Tests (chacun bâtit une bibliothèque dans un `QTemporaryDir` avec `CliRun`, applique un profil uni par `apply`, puis crée le service sur `run.device`) :

```cpp
void visualizerStreamsAndRestores()
// start() == None ; après 30 tick() : au moins un paquet 0x0c dans log.packets, et une touche de la rangée
// du bas, dans la colonne de la bande de 100 Hz, y porte la couleur du profil ;
// stop() : les derniers paquets sont exactement buildPackets(profil).packets ; un seul "bind".
void visualizerRefusesWithoutAProfile()      // Review Focus 1
// bibliothèque vide : start().error == Invalid ; log.events vide ; audio.started == 0.
void visualizerRefusesWhenTheAppliedProfileIsGone()   // Review Focus 1
// profil appliqué puis supprimé, aucun thème lié : même attente.
void visualizerFailsBeforeAttachingWithoutAudio()
// audio.startOk = false : start().error == Invalid, message contenant "no audio server", log.events vide.
void visualizerReloadRecolours()
// start, 30 tick ; `apply` d'un second profil par CliRun n'est pas utilisé ici : écrire state.json via
// Library::setApplied, puis visualizerRequest(socket, "reload") == "ok" ; 30 tick : les images portent la
// nouvelle couleur ; stop() rend le second profil.
void visualizerStopRestoresBlackWhenOff()
// Library::setOff(true) + "reload" ; les images gardent la couleur du profil ; stop() rend le noir.
void visualizerSocketAnswers()
// "ping" -> "ok" ; "nonsense" -> "error" ; après stop() : visualizerRequest(...) est vide.
void visualizerReplacesAStaleSocket()        // Review Focus 5
// un fichier ordinaire existe déjà au chemin du socket : start() réussit et "ping" répond "ok".
void visualizerStreamsFramesToSubscribers()
// QLocalSocket abonné par "frames" : après un tick qui change l'image, une ligne "frame <hex>" arrive,
// de 113 × 8 caractères hexadécimaux.
void visualizerEmitsFailedAndDetaches()
// transport.failAt sur le premier paquet d'image : le signal failed part une fois avec SendFailed,
// running() est faux, log.events finit par "unbind", les tick() suivants n'envoient rien.
void resolveProfileMatchesRestore()
// pour : profil appliqué ; off ; appliqué supprimé avec thème lié ; rien du tout — resolveProfile donne le
// profil dont buildPackets égale les paquets que `klc-light restore` envoie (ou aucun profil).
```

Ces tests font tourner la boucle d'événements Qt (`QTRY_COMPARE`, `QSignalSpy`) pour le socket.

- [ ] **Step 2 : lancer `bin/test`** — attendu : échec de compilation.
- [ ] **Step 3 : extraire `resolveProfile` de `runCli`** ; lancer `bin/test` : les tests antérieurs restent verts.
- [ ] **Step 4 : implémenter `VisualizerService`, `visualizerRequest`, `runVisualizer`, et l'aiguillage dans `main.cpp`** (`argv[1] == "visualizer" && argv[2] == "run"` avant `runCommand`).
- [ ] **Step 5 : lancer `bin/test`** — attendu : 279 tests réussis.
- [ ] **Step 6 : essai manuel simulé.** `KLC_LIGHT_FAKE_DEVICE=1 build/klc-light visualizer run` avec de la musique : des lignes `F 0c00…` défilent, s'arrêtent pendant un silence ; `Ctrl+C` imprime les paquets du profil puis sort avec 0.
- [ ] **Step 7 : commit** — `Add the visualizer service and its socket`

---

### Task 7 : ligne de commande et unité systemd

**Files:**
- Modify: `src/cli.h`, `src/cli.cpp`, `src/main.cpp`
- Create: `pkgbuild/klc-light-visualizer.service`
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes: `visualizerRequest`, `visualizerSocketPath` (tâche 6).
- Produces:
  ```cpp
  // CliEnv gains:
  QString visualizerSocket;                              // empty: no visualizer check (tests of older behaviour)
  std::function<int(const QStringList &)> systemctl;     // runs `systemctl --user <args>`, returns its exit code
  constexpr char kVisualizerUnit[] = "klc-light-visualizer.service";
  ```
- `klc-light visualizer on [--json]`, `klc-light visualizer off [--json]` ; champ `"visualizer": <bool>` dans `list --json`.

Décisions :
- `on` : `systemctl({"start", unit})` ; code 0 → succès. Sinon lire `systemctl --user show -p ExecMainStatus --value` par un second membre de `CliEnv`, `std::function<QString(const QStringList &)> systemctlOutput` : `2` → code 2 et le message « contrôleur absent » d'`apply` ; autre → code 1 et `"The visualizer could not start; see journalctl --user -u klc-light-visualizer."`.
- `off` : `systemctl({"stop", unit})`, succès même si l'unité était arrêtée.
- `list` : `visualizer` vrai si `systemctl({"is-active", "--quiet", unit}) == 0`.
- `apply` et `off` quand `visualizerRequest(socket, "ping") == "ok"` : construire et valider comme d'habitude ; en cas d'erreur, la rapporter sans rien changer ; sinon mettre `state.json` à jour comme après un envoi réussi, envoyer `reload`, finir en succès sans appeler `device`. `--check` est inchangé. `restore` dans ce cas : succès sans rien faire.
- Usage affiché par `--help` complété de la ligne `klc-light visualizer on|off [--json]`.
- Unité :
  ```ini
  [Unit]
  Description=KLC Light music visualizer

  [Service]
  Type=notify
  ExecStart=/usr/bin/klc-light visualizer run
  # Only after a crash or a kill: an orderly exit has already put the profile back, and
  # after a refused send nothing more may be sent.
  ExecStopPost=/bin/sh -c '[ "$EXIT_CODE" = exited ] || exec /usr/bin/klc-light restore'
  Restart=no
  TimeoutStartSec=30
  TimeoutStopSec=30
  ```
  Aucune section `[Install]` : l'unité ne s'active jamais au démarrage.

- [ ] **Step 1 : écrire les tests qui échouent** (avec `CliRun`, `systemctl` et `systemctlOutput` remplacés par des lambdas qui enregistrent leurs arguments ; un `QLocalServer` de test qui répond `ok` à chaque ligne et les enregistre) :

```cpp
void cliVisualizerOnStartsTheUnit()          // args {"start","klc-light-visualizer.service"} ; code 0
void cliVisualizerOnReportsAMissingKeyboard()// start → 1, ExecMainStatus "2" : code 2, message d'apply
void cliVisualizerOnReportsOtherFailures()   // start → 1, ExecMainStatus "1" : code 1, message journalctl ; --json : {"ok":false,…}
void cliVisualizerOffStopsTheUnit()          // args {"stop",unit} ; code 0
void cliVisualizerRejectsUnknownWords()      // "visualizer", "visualizer maybe" : code 1, usage sur stderr
void cliListReportsTheVisualizer()           // is-active 0 → "visualizer": true ; 3 → false
void cliApplyDuringTheStreamOnlyRecords()
// serveur de test à l'écoute : `apply B` → code 0, log.events vide, state.json applied == "B",
// le serveur a reçu "ping" puis "reload".
void cliOffDuringTheStreamOnlyRecords()      // idem : off == true, applied inchangé, rien d'envoyé
void cliRestoreDuringTheStreamDoesNothing()  // code 0, rien d'envoyé, pas de "reload"
void cliApplyInvalidDuringTheStreamChangesNothing()   // Review Focus 3
// fichier de profil invalide : code 1, erreur rapportée, state.json inchangé, pas de "reload".
void cliApplyIgnoresAStaleSocket()           // Review Focus 5
// un fichier ordinaire au chemin du socket, personne à l'écoute : `apply` envoie normalement (log.events non vide).
void cliApplyWithoutVisualizerIsUnchanged()  // visualizerSocket vide : comportement actuel
```

- [ ] **Step 2 : lancer `bin/test`** — attendu : échec de compilation.
- [ ] **Step 3 : implémenter.** Dans `main.cpp`, `systemctl` et `systemctlOutput` lancent `systemctl --user` par `QProcess` ; `visualizerSocket` vient de `visualizerSocketPath(runtime)`.
- [ ] **Step 4 : lancer `bin/test`** — attendu : 291 tests réussis.
- [ ] **Step 5 : vérifier l'unité** — `systemd-analyze --user verify pkgbuild/klc-light-visualizer.service` : aucune erreur (un avertissement sur `/usr/bin/klc-light` absent est acceptable si le paquet n'est pas installé).
- [ ] **Step 6 : commit** — `Add klc-light visualizer on/off and the user unit`

---

### Task 8 : veille et réveil

**Files:**
- Modify: `src/visualizer.h`, `src/visualizer.cpp`
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Produces, sur `VisualizerService` :
  ```cpp
  public slots:
      void prepareForSleep(bool sleeping);   // true: stop the stream as stop() does, keep the socket;
                                             // false: start the stream again
  ```

Décisions :
- `runVisualizer` relie le signal `PrepareForSleep(bool)` de `org.freedesktop.login1.Manager` (bus système, `/org/freedesktop/login1`) à ce slot, et tient un verrou d'inhibition `delay` sur `sleep` (méthode `Inhibit`) pendant le flux : il est relâché une fois le contrôleur détaché, repris après la reprise.
- Pendant la veille, `tick()` n'envoie rien ; `reload` reste servi.
- Au réveil, la reprise passe par `StreamSession::start`, donc par l'intervalle de `Device`. Si elle échoue : signal `failed`, comme un envoi refusé.

- [ ] **Step 1 : écrire les tests qui échouent**

```cpp
void visualizerSleepRestoresAndDetaches()
// start, 30 tick, prepareForSleep(true) : les derniers paquets sont ceux du profil, log.events finit par
// "unbind" ; 30 tick de plus n'envoient rien ; "ping" répond toujours "ok".
void visualizerWakeResumes()
// puis prepareForSleep(false) : un second "bind", cinq paquets d'entrée, et les tick suivants renvoient des 0x0c ;
// log.sleeps contient l'attente de Device::MinIntervalMs avant ce second "bind".
void visualizerWakeFailureIsReported()
// transport.openOk = false avant prepareForSleep(false) : failed part avec DeviceMissing, running() est faux.
void visualizerSleepTwiceIsHarmless()
// prepareForSleep(true) deux fois, puis false deux fois : un seul cycle d'attache de plus.
```

- [ ] **Step 2 : lancer `bin/test`** — attendu : échec de compilation.
- [ ] **Step 3 : implémenter le slot, la liaison D-Bus et l'inhibiteur.**
- [ ] **Step 4 : lancer `bin/test`** — attendu : 295 tests réussis.
- [ ] **Step 5 : commit** — `Stop the stream across suspend and resume it after`

---

### Task 9 : éditeur

**Files:**
- Modify: `src/backend.h`, `src/backend.cpp`, `src/main.cpp`, `src/ProfileBar.qml`, `src/KeyboardView.qml`, `src/i18n/fr.json`
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes: protocole du socket (tâche 6), `klc-light visualizer on|off` (tâche 7).
- Produces, sur `Backend` :
  ```cpp
  Q_PROPERTY(bool visualizer READ visualizer NOTIFY visualizerChanged)
  Q_PROPERTY(int liveRevision READ liveRevision NOTIFY liveFrame)   // bumps with each frame received
  Q_INVOKABLE void setVisualizer(bool on);
  // Constructor gains, after colorsPath:
  //   QString visualizerSocket, std::function<int(bool on, QString *error)> visualizerSwitch
  ```

Décisions :
- `setVisualizer` appelle `visualizerSwitch` hors du fil d'interface (comme `apply` le fait pour l'envoi) ; son erreur va dans `error`.
- `visualizer` est vrai tant qu'une connexion abonnée par `frames` est ouverte. Le backend tente la connexion à l'ouverture de l'éditeur, après un `setVisualizer(true)` réussi, et la laisse tomber à la déconnexion : `visualizer` repasse alors à faux et l'aperçu normal revient.
- `previewColor(id, ms)` rend la couleur de la dernière image reçue pour cette touche quand `visualizer` est vrai, l'aperçu du profil sinon.
- `KeyboardView.qml` : la liaison de couleur dépend aussi de `backend.liveRevision`.
- `ProfileBar.qml` : un bouton à bascule `qsTr("Visualizer")`, coché selon `backend.visualizer`, désactivé pendant `backend.busy`. `Apply` appliqué pendant le flux passe par la ligne de commande existante du backend ; si le backend envoie par `Sender` sans passer par `runCli`, il doit d'abord faire le même test `ping` que la tâche 7 et, si le service répond, enregistrer l'état et envoyer `reload` au lieu d'appeler `Sender`.
- Traductions à ajouter dans `fr.json` : `"Visualizer": "Égaliseur"`, et les deux messages des tâches 6 et 7.

- [ ] **Step 1 : écrire les tests qui échouent**

```cpp
void backendShowsLiveFrames()
// serveur de test : à "frames" répond "ok" puis une ligne "frame" où KEY_Q est rouge.
// QTRY_VERIFY(backend.visualizer()) ; previewColor("KEY_Q", 0) == rouge ; liveRevision a augmenté.
void backendFallsBackWhenTheServiceDies()    // Review Focus 4
// le serveur ferme la connexion : QTRY_VERIFY(!backend.visualizer()) ; previewColor("KEY_Q", 0) redevient
// celle du profil ouvert.
void backendSwitchesTheVisualizer()
// setVisualizer(true) appelle visualizerSwitch(true) ; s'il rend 1 avec une erreur, backend.error la porte
// et visualizer reste faux.
void backendApplyDuringTheStreamOnlyRecords()
// serveur de test à l'écoute : apply() n'appelle pas Sender, le serveur reçoit "reload", appliedName est à jour.
```

- [ ] **Step 2 : lancer `bin/test`** — attendu : échec de compilation.
- [ ] **Step 3 : implémenter** ; dans `main.cpp`, `visualizerSwitch` lance `klc-light visualizer on|off --json` (`QCoreApplication::applicationFilePath()`) et en lit le message d'erreur.
- [ ] **Step 4 : lancer `bin/test`** — attendu : 299 tests réussis.
- [ ] **Step 5 : essai manuel simulé.** Dans un terminal `KLC_LIGHT_FAKE_DEVICE=1 build/klc-light visualizer run`, dans un autre `KLC_LIGHT_FAKE_DEVICE=1 build/klc-light` : l'aperçu montre le spectre ; arrêter le service ramène l'aperçu du profil et décoche le bouton.
- [ ] **Step 6 : commit** — `Editor: visualizer toggle and live preview`

---

### Task 10 : widget, paquet, documentation

**Files:**
- Modify: `omarchy-plugin/swald.klc-light/Panel.qml`, `omarchy-plugin/swald.klc-light/i18n/en.json`, `omarchy-plugin/swald.klc-light/i18n/fr.json`, `aur/PKGBUILD`, `pkgbuild/PKGBUILD`, `README.md`, `src/version.h`

Décisions :
- `Panel.qml` : un bouton sous « Turn off » / « Turn on », libellé `visualizer_on` (« Start visualizer » / « Lancer l'égaliseur ») ou `visualizer_off` (« Stop visualizer » / « Arrêter l'égaliseur ») selon `library.visualizer`, qui lance `klc-light visualizer on|off --json` par le `actionProc` existant. L'icône du widget prend une teinte d'accent quand `library.visualizer` est vrai.
- Les deux `PKGBUILD` : `libpipewire` dans `depends`, `pkgver=0.6.0`, et
  `install -Dm644 pkgbuild/klc-light-visualizer.service "$pkgdir/usr/lib/systemd/user/klc-light-visualizer.service"`. La somme `sha256sums` de `aur/PKGBUILD` reste à mettre à jour par l'utilisateur quand il taguera.
- `src/version.h` : `0.6.0`.
- `README.md` : une puce dans la liste d'en-tête, et une section « Music visualizer » après « Bar widget » : les deux commandes, le fait que le profil revient à l'arrêt, que `apply` et les thèmes recolorent sans interrompre, que le service s'arrête pendant la veille, et qu'il n'est jamais lancé au démarrage.

- [ ] **Step 1 : modifier le plugin et ses traductions.**
- [ ] **Step 2 : vérifier les JSON** — `python3 -m json.tool` sur les deux fichiers `i18n` : aucune erreur, mêmes clés dans les deux.
- [ ] **Step 3 : modifier les `PKGBUILD`, `version.h`, `README.md`.**
- [ ] **Step 4 : lancer `bin/build` et `bin/test`** — attendu : 299 tests réussis ; `build/klc-light --version` imprime `klc-light 0.6.0`.
- [ ] **Step 5 : commit** — `Bar widget, package and README for the visualizer`

---

### Task 11 : mesures sur le GS76 (avec l'utilisateur)

**Files:**
- Modify: `docs/origins.md`

Cette tâche envoie au vrai clavier. Elle ne se fait qu'avec l'utilisateur présent, après son accord explicite, travail sauvegardé (une erreur USB -71 impose une extinction complète). Après chaque mesure : `journalctl -k` depuis le début de la mesure ne montre aucune erreur sur le port du contrôleur, et `/sys/bus/usb/devices/*/idProduct` contient toujours `113a`.

- [ ] **Step 1 : installer la version locale** — `makepkg -si` dans `pkgbuild/` (demander avant : installation système).
- [ ] **Step 2 : flux court.** `klc-light visualizer on`, une minute de musique, `klc-light visualizer off`. Attendu : graves à gauche, aigus à droite, noir pendant un silence, profil de retour à l'arrêt.
- [ ] **Step 3 : changement de thème pendant le flux.** Attendu : le spectre change de couleurs, rien dans `journalctl --user -t klc-light`, le nouveau profil s'affiche à l'arrêt.
- [ ] **Step 4 : `kill -9` du service.** Attendu : le profil revient en une dizaine de secondes (`ExecStopPost`), contrôleur détaché (`authorized` à 0).
- [ ] **Step 5 : veille et réveil avec le service actif.** Attendu : profil affiché avant la veille, spectre de retour après le réveil.
- [ ] **Step 6 : flux d'une heure.** Attendu : aucune erreur, service toujours actif, arrêt propre.
- [ ] **Step 7 : consigner les cinq mesures** dans une section datée de `docs/origins.md`, avec ce qui a été vu, y compris tout écart.
- [ ] **Step 8 : commit** — `Record the visualizer measurements on the GS76`

Si une mesure échoue, s'arrêter : ne pas enchaîner les suivantes, rapporter ce qui a été observé.
