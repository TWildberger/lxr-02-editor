#include "DeviceSession.h"

namespace kitforge
{

using Outcome = ext::RequestQueue::Outcome;
using RQ = ext::RequestQueue;

bool DeviceSession::Ring::write(const uint8_t* p, int n)
{
    if (fifo.getFreeSpace() < n)
        return false;
    const auto scope = fifo.write(n);
    if (scope.blockSize1 > 0)
        std::memcpy(data.data() + scope.startIndex1, p, (size_t) scope.blockSize1);
    if (scope.blockSize2 > 0)
        std::memcpy(data.data() + scope.startIndex2, p + scope.blockSize1, (size_t) scope.blockSize2);
    return true;
}

int DeviceSession::Ring::read(uint8_t* p, int max)
{
    const int n = std::min(max, fifo.getNumReady());
    const auto scope = fifo.read(n);
    if (scope.blockSize1 > 0)
        std::memcpy(p, data.data() + scope.startIndex1, (size_t) scope.blockSize1);
    if (scope.blockSize2 > 0)
        std::memcpy(p + scope.blockSize1, data.data() + scope.startIndex2, (size_t) scope.blockSize2);
    return n;
}

int DeviceSession::Ring::peekFrameSize() const
{
    if (fifo.getNumReady() < 2)
        return -1;
    int start1, size1, start2, size2;
    fifo.prepareToRead(2, start1, size1, start2, size2);
    const uint8_t lo = data[(size_t) start1];
    const uint8_t hi = size1 > 1 ? data[(size_t) start1 + 1] : data[(size_t) start2];
    return lo | (hi << 8);
}

DeviceSession::DeviceSession(ParamSync& sync, Listener& listener)
    : juce::Thread("KitForge device"), sync_(sync), listener_(listener)
{
    lastTick_ = nowMs();
    startThread(juce::Thread::Priority::high);
}

DeviceSession::~DeviceSession()
{
    stopThread(2000);
    queue_.cancelAll();
    std::lock_guard<std::mutex> g(portLock_);
    if (input_)
        input_->stop();
    input_.reset();
    output_.reset();
}

int64_t DeviceSession::nowMs() const { return (int64_t) juce::Time::getMillisecondCounterHiRes(); }

int DeviceSession::devByte() const
{
    const int id = deviceId_.load();
    return id >= 1 && id <= 16 ? id - 1 : ext::kAllDevices;
}

void DeviceSession::setMode(Mode m)
{
    if (mode_.exchange(m) == m)
        return;
    listener_.deviceModeChanged(m, identity());
}

ext::Identity DeviceSession::identity() const
{
    std::lock_guard<std::mutex> g(identityLock_);
    return identity_;
}

juce::String DeviceSession::describe() const
{
    switch (mode_.load())
    {
        case Mode::Disconnected: return "Not connected";
        case Mode::Probing: return "Detecting firmware...";
        case Mode::Stock: return "Stock firmware (CC/NRPN)";
        case Mode::Extended: return "Extended firmware: " + juce::String(identity().describe());
    }
    return {};
}

void DeviceSession::setRoute(Route r)
{
    route_.store(r);
    queue_.cancelAll();
    encoderReset_.store(true);
    if (r == Route::PluginMidi || directOutputOpen())
        detect();
    else
        setMode(Mode::Disconnected);
}

void DeviceSession::setDirectPorts(const juce::String& outputId, const juce::String& inputId)
{
    {
        std::lock_guard<std::mutex> g(portLock_);
        if (outputId != outputId_ || (outputId.isNotEmpty() && output_ == nullptr))
        {
            output_.reset();
            outputId_ = outputId;
            if (outputId.isNotEmpty())
                output_ = juce::MidiOutput::openDevice(outputId);
        }
        if (inputId != inputId_ || (inputId.isNotEmpty() && input_ == nullptr))
        {
            if (input_)
                input_->stop();
            input_.reset();
            inputId_ = inputId;
            if (inputId.isNotEmpty())
            {
                input_ = juce::MidiInput::openDevice(inputId, this);
                if (input_)
                    input_->start();
            }
        }
    }
    if (outputId.isNotEmpty() && !directOutputOpen())
        log("Could not open MIDI output");
    if (outputId.isNotEmpty() && inputId.isEmpty())
        log("No MIDI input selected: replies from the device can't be read, and over USB it drops its output when no one reads it");
    if (route_.load() == Route::DirectPort)
        setRoute(Route::DirectPort);
}

juce::String DeviceSession::directOutputId() const
{
    std::lock_guard<std::mutex> g(portLock_);
    return outputId_;
}

juce::String DeviceSession::directInputId() const
{
    std::lock_guard<std::mutex> g(portLock_);
    return inputId_;
}

bool DeviceSession::directOutputOpen() const
{
    std::lock_guard<std::mutex> g(portLock_);
    return output_ != nullptr;
}

void DeviceSession::setChannel(int ch)
{
    channel_.store(juce::jlimit(1, 16, ch));
    encoderReset_.store(true);
}

void DeviceSession::setDeviceId(int id) { deviceId_.store(juce::jlimit(0, 16, id)); }

void DeviceSession::setAllowExtended(bool b)
{
    if (allowExtended_.exchange(b) != b && mode_.load() != Mode::Disconnected)
        detect();
}

void DeviceSession::detect()
{
    queue_.cancelAll();
    if (route_.load() == Route::DirectPort && !directOutputOpen())
    {
        setMode(Mode::Disconnected);
        return;
    }
    if (!allowExtended_.load())
    {
        setMode(Mode::Stock);
        return;
    }
    setMode(Mode::Probing);
    enqueue("identity", ext::identityRequest(ext::kAllDevices), RQ::replyTo(ext::Command::IdentityRequest, ext::Command::IdentityReply),
            [this](Outcome o, const std::vector<ext::Message>& replies) {
                if (o == Outcome::Cancelled)
                    return;
                if (o != Outcome::Ok || replies.empty())
                {
                    log("No extended-firmware reply: using CC/NRPN (stock firmware)");
                    setMode(Mode::Stock);
                    return;
                }
                const auto id = replies.back().identity;
                {
                    std::lock_guard<std::mutex> g(identityLock_);
                    identity_ = id;
                }
                if (!id.layoutMatchesEditor() || !id.has(ext::CapKits) || !id.has(ext::CapParams))
                {
                    log("Firmware answered but its parameter layout differs from this editor (" + juce::String(id.describe())
                        + "): using CC/NRPN");
                    setMode(Mode::Stock);
                    return;
                }
                log("Detected " + juce::String(id.describe()));
                setMode(Mode::Extended);
                if (id.has(ext::CapEcho))
                    setEcho(true);
            });
}

void DeviceSession::enqueue(const juce::String& label, Bytes frame, RQ::Matcher m,
                            std::function<void(Outcome, const std::vector<ext::Message>&)> done, int timeoutMs, int group)
{
    RQ::Request r;
    r.label = label.toStdString();
    r.frame = std::move(frame);
    r.matcher = std::move(m);
    r.done = std::move(done);
    r.timeoutMs = timeoutMs;
    r.group = group;
    queue_.push(std::move(r));
}

juce::String DeviceSession::outcomeText(Outcome o, const std::vector<ext::Message>& replies)
{
    switch (o)
    {
        case Outcome::Ok: return "OK";
        case Outcome::Timeout: return "no reply from device";
        case Outcome::Cancelled: return "cancelled";
        case Outcome::Nak:
            if (!replies.empty() && replies.back().command == ext::Command::Nak)
                return "device error: " + juce::String(ext::errorName(replies.back().error));
            return "transfer failed";
    }
    return {};
}

bool DeviceSession::requireExtended(const Done& done)
{
    if (isExtended())
        return true;
    if (done)
        done(false, "needs the extended firmware (not detected)");
    return false;
}

void DeviceSession::requestKit(int slot, std::function<void(std::optional<Kit>, const juce::String&)> done)
{
    if (!requireExtended([&](bool, const juce::String& msg) { done(std::nullopt, msg); }))
        return;
    enqueue("kit request", ext::kitRequest(devByte(), slot), RQ::replyTo(ext::Command::KitRequest, ext::Command::KitDump),
            [done](Outcome o, const std::vector<ext::Message>& replies) {
                if (o != Outcome::Ok)
                    return done(std::nullopt, outcomeText(o, replies));
                done(Kit::fromSnd(replies.back().data), "OK");
            });
}

void DeviceSession::sendKit(int slot, const Kit& kit, int flags, Done done)
{
    if (!requireExtended(done))
        return;
    enqueue("kit dump", ext::kitDump(devByte(), slot, flags, kit.toSnd()), RQ::ackFor(ext::Command::KitDump),
            [done](Outcome o, const std::vector<ext::Message>& replies) {
                if (done)
                    done(o == Outcome::Ok, outcomeText(o, replies));
            });
}

void DeviceSession::storeKit(int slot, int flags, Done done)
{
    if (!requireExtended(done))
        return;
    enqueue("kit store", ext::kitStore(devByte(), slot, flags), RQ::ackFor(ext::Command::KitStore),
            [done](Outcome o, const std::vector<ext::Message>& replies) {
                if (done)
                    done(o == Outcome::Ok, outcomeText(o, replies));
            });
}

void DeviceSession::loadKitSlot(int slot, Done done)
{
    if (isExtended())
    {
        enqueue("kit load", ext::kitLoad(devByte(), slot), RQ::ackFor(ext::Command::KitLoad),
                [done](Outcome o, const std::vector<ext::Message>& replies) {
                    if (done)
                        done(o == Outcome::Ok, outcomeText(o, replies));
                });
        return;
    }
    StockMidiEncoder enc(channel_.load());
    const auto m = enc.encodeKitSelect(slot);
    sendRaw(Bytes(m.bytes, m.bytes + m.size));
    if (done)
        done(true, "sent bank select");
}

void DeviceSession::requestGlobals(std::function<void(std::optional<Globals>, const juce::String&)> done)
{
    if (!requireExtended([&](bool, const juce::String& msg) { done(std::nullopt, msg); }))
        return;
    enqueue("globals request", ext::globalsRequest(devByte()), RQ::replyTo(ext::Command::GlobalsRequest, ext::Command::GlobalsDump),
            [done](Outcome o, const std::vector<ext::Message>& replies) {
                if (o != Outcome::Ok)
                    return done(std::nullopt, outcomeText(o, replies));
                const auto& m = replies.back();
                Globals g;
                for (size_t i = 0; i < m.data.size(); ++i)
                    g.set(m.first + (int) i, m.data[i]);
                done(g, "OK");
            });
}

void DeviceSession::sendGlobals(const Globals& g, int flags, Done done)
{
    if (!requireExtended(done))
        return;
    Bytes values;
    for (int i = kFirstGlobal; i < kNumParams; ++i)
        values.push_back((uint8_t) g.get(i));
    enqueue("globals dump", ext::globalsDump(devByte(), flags, kFirstGlobal, values), RQ::ackFor(ext::Command::GlobalsDump),
            [done](Outcome o, const std::vector<ext::Message>& replies) {
                if (done)
                    done(o == Outcome::Ok, outcomeText(o, replies));
            });
}

void DeviceSession::requestPattern(int slot, std::function<void(std::optional<Bytes>, const juce::String&)> done)
{
    if (!requireExtended([&](bool, const juce::String& msg) { done(std::nullopt, msg); }))
        return;
    auto rx = std::make_shared<ext::PatternReceiver>();
    enqueue("pattern request", ext::patternRequest(devByte(), slot), RQ::patternDump(rx),
            [done, rx](Outcome o, const std::vector<ext::Message>& replies) {
                if (o != Outcome::Ok)
                    return done(std::nullopt, rx->error().empty() ? outcomeText(o, replies) : juce::String(rx->error()));
                done(rx->data(), "OK");
            },
            ext::kPatternTimeoutMs);
}

void DeviceSession::sendPattern(int slot, const Bytes& raw, int flags, Done done)
{
    if (!requireExtended(done))
        return;
    const auto chunks = ext::patternChunks(devByte(), slot, flags, raw);
    const int group = queue_.newGroup();
    auto reported = std::make_shared<std::atomic<bool>>(false);
    for (size_t i = 0; i < chunks.size(); ++i)
    {
        const bool last = i + 1 == chunks.size();
        enqueue("pattern chunk", chunks[i], RQ::ackFor(ext::Command::PatternChunk, (int) i),
                [done, last, reported](Outcome o, const std::vector<ext::Message>& replies) {
                    if ((o != Outcome::Ok || last) && !reported->exchange(true) && done)
                        done(o == Outcome::Ok, outcomeText(o, replies));
                },
                ext::kPatternTimeoutMs, group);
    }
}

void DeviceSession::setEcho(bool on)
{
    if (!isExtended() && mode_.load() != Mode::Probing)
        return;
    enqueue("echo", ext::echoSet(devByte(), on), RQ::ackFor(ext::Command::EchoSet), [this, on](Outcome o, const std::vector<ext::Message>& r) {
        if (o != Outcome::Ok)
            log("Parameter echo " + juce::String(on ? "on" : "off") + ": " + outcomeText(o, r));
    });
}

void DeviceSession::sendRaw(const Bytes& msg)
{
    std::lock_guard<std::mutex> g(rawLock_);
    rawOut_.push_back(msg);
}

bool DeviceSession::transmit(const Bytes& msg)
{
    if (msg.empty())
        return true;
    if (route_.load() == Route::PluginMidi)
    {
        if (pluginOut_.fifo.getFreeSpace() < (int) msg.size() + 2)
            return false;
        const uint8_t len[2] { (uint8_t) (msg.size() & 0xFF), (uint8_t) (msg.size() >> 8) };
        pluginOut_.write(len, 2);
        pluginOut_.write(msg.data(), (int) msg.size());
        return true;
    }
    std::lock_guard<std::mutex> g(portLock_);
    if (!output_)
        return false;
    output_->sendMessageNow(juce::MidiMessage(msg.data(), (int) msg.size()));
    return true;
}

void DeviceSession::processBlock(const juce::MidiBuffer& in, juce::MidiBuffer& out)
{
    if (route_.load() != Route::PluginMidi)
        return;
    for (const auto meta : in)
        if (meta.numBytes > 0 && meta.data[0] == 0xF0)
            pluginIn_.write(meta.data, meta.numBytes);

    uint8_t buf[ext::kMaxFrameLength + 64];
    for (;;)
    {
        const int n = pluginOut_.peekFrameSize();
        if (n < 0 || pluginOut_.fifo.getNumReady() < n + 2)
            break;
        uint8_t len[2];
        pluginOut_.read(len, 2);
        if (n > (int) sizeof buf)
        {
            pluginOut_.fifo.finishedRead(n); // cannot happen: frames are limited upstream
            continue;
        }
        pluginOut_.read(buf, n);
        out.addEvent(buf, n, 0);
    }
}

void DeviceSession::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isSysEx())
        portIn_.write(m.getRawData(), m.getRawDataSize());
}

void DeviceSession::handleFrame(const Bytes& frame, int64_t now)
{
    if (!ext::isOurs(frame))
        return;
    std::string err;
    const auto m = ext::parse(frame, &err);
    if (!m)
    {
        log("Ignored malformed SysEx: " + juce::String(err));
        return;
    }
    if (deviceId_.load() != 0 && m->device != devByte() && m->device != ext::kAllDevices)
        return;
    if (queue_.onMessage(*m, now))
        return;
    if (m->command == ext::Command::ParamNotify || m->command == ext::Command::ParamValue)
        listener_.deviceParamsChanged(m->pairs);
    else if (m->command == ext::Command::Event)
    {
        if (m->event == ext::EventType::KitChanged)
            listener_.deviceKitChanged();
    }
    else if (m->command == ext::Command::StatusReply)
        listener_.deviceStatus(m->status);
    else if (m->command == ext::Command::Nak)
        log(juce::String("Device rejected ") + ext::commandName(m->answered) + " (" + ext::errorName(m->error) + ", "
            + juce::String(m->arg) + ")");
}

void DeviceSession::run()
{
    std::vector<Bytes> out;
    std::vector<uint8_t> buf(4096);
    while (!threadShouldExit())
    {
        wait(2);
        const int64_t now = nowMs();

        for (auto* ring : { &pluginIn_, &portIn_ })
        {
            auto& assembler = ring == &pluginIn_ ? pluginAsm_ : portAsm_;
            while (const int n = ring->read(buf.data(), (int) buf.size()))
                for (const auto& f : assembler.feed(buf.data(), (size_t) n))
                    handleFrame(f, now);
        }

        const double bytesPerMs = rate_.load() == Rate::DinSafe ? 2.8 : 20.0;
        credit_ = std::min(256.0, credit_ + (double) (now - lastTick_) * bytesPerMs);
        lastTick_ = now;

        const bool plugin = route_.load() == Route::PluginMidi;
        if (!plugin && !directOutputOpen())
            continue;
        auto room = [&] { return plugin ? pluginOut_.fifo.getFreeSpace() - 8 : 1 << 20; };

        if (encoderReset_.exchange(false))
        {
            encoder_.setChannel(channel_.load());
            encoder_.resetNrpnState();
        }

        {
            std::lock_guard<std::mutex> g(rawLock_);
            while (!rawOut_.empty() && credit_ > 0 && room() > (int) rawOut_.front().size() + 2)
            {
                transmit(rawOut_.front());
                credit_ -= (double) rawOut_.front().size();
                rawOut_.erase(rawOut_.begin());
            }
        }

        if (credit_ > 0 && room() > ext::kMaxFrameLength + 2)
            if (auto f = queue_.poll(now))
            {
                transmit(*f);
                credit_ -= (double) f->size();
            }

        const auto m = mode_.load();
        if ((m == Mode::Stock || m == Mode::Extended) && credit_ > 0 && sync_.hasPending())
        {
            const int budget = std::min((int) credit_, room() * 3 / 5);
            if (budget <= 0)
                continue;
            out.clear();
            const auto r = sync_.collect(encoder_, { m == Mode::Extended, devByte() }, budget, out);
            for (const auto& msg : out)
                transmit(msg);
            credit_ -= r.bytes;
            if (!r.unsendable.empty())
                listener_.deviceUnsendable(r.unsendable);
        }
    }
}

} // namespace kitforge
