#import <AVFoundation/AVFoundation.h>

#include "Engine/Audio/Audio.hpp"
#include "Platform/Mac/Platform.hpp"

#include <algorithm>
#include <map>
#include <string>

namespace {

class MacOutput final : public ld::audio::Output {
public:
    MacOutput() : players([NSMutableDictionary dictionary]) {}

    bool play(const std::string& id, const ld::audio::Playback& playback) override {
        stop(id);
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:playback.file.c_str()]];
        NSError* error = nil;
        AVAudioPlayer* player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&error];
        if (!player || error) return false;
        player.enableRate = YES;
        volumes[id] = playback.volume;
        apply(player, playback);
        if (![player prepareToPlay] || ![player play]) return false;
        players[key(id)] = player;
        return true;
    }

    void update(const std::string& id, const ld::audio::Playback& playback) override {
        AVAudioPlayer* player = players[key(id)];
        volumes[id] = playback.volume;
        if (player) apply(player, playback);
    }

    bool playing(const std::string& id) override {
        AVAudioPlayer* player = players[key(id)];
        if (player && !player.playing) {
            [players removeObjectForKey:key(id)];
            volumes.erase(id);
        }
        return player && player.playing;
    }

    void stop(const std::string& id) override {
        AVAudioPlayer* player = players[key(id)];
        if (!player) return;
        [player stop];
        [players removeObjectForKey:key(id)];
        volumes.erase(id);
    }

    void setMaster(double volume) override {
        master = volume;
        for (NSString* name in players) {
            auto found = volumes.find(name.UTF8String);
            if (found != volumes.end()) players[name].volume = static_cast<float>(found->second * master);
        }
    }

private:
    static NSString* key(const std::string& id) { return [NSString stringWithUTF8String:id.c_str()]; }

    void apply(AVAudioPlayer* player, const ld::audio::Playback& playback) const {
        player.volume = static_cast<float>(playback.volume * master);
        player.rate = static_cast<float>(std::min(2.0, std::max(0.5, playback.speed)));
        player.numberOfLoops = playback.looped ? -1 : 0;
    }

    NSMutableDictionary<NSString*, AVAudioPlayer*>* players;
    std::map<std::string, double> volumes;
    double master = 1;
};

}

namespace platform {

void installAudio() { ld::audio::setOutput(std::make_shared<MacOutput>()); }

}
