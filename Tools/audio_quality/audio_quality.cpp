#include "UI/Born2FlapAudioEngine.h"
#include "Audio/Born2FlapAeroAudio.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace born2flap;
constexpr int Rate=44100;
void Wav(const std::filesystem::path& path,const std::vector<float>& samples)
{
    std::ofstream out(path,std::ios::binary);
    auto w32=[&](uint32_t x){out.write(reinterpret_cast<char*>(&x),4);};
    auto w16=[&](uint16_t x){out.write(reinterpret_cast<char*>(&x),2);};
    out.write("RIFF",4);w32(36+uint32_t(samples.size()*2));out.write("WAVEfmt ",8);
    w32(16);w16(1);w16(2);w32(Rate);w32(Rate*4);w16(4);w16(16);
    out.write("data",4);w32(uint32_t(samples.size()*2));
    for(float v:samples) w16(uint16_t(int16_t(v*32767)));
}
int main(int argc,char** argv)
{
    std::filesystem::path dir=argc>1 ? argv[1] : "audio-quality";
    std::filesystem::create_directories(dir);
    bool pass=true;
    for(const std::string name:{"wing","servo","wind","mix","wing_slow","wing_fast","servo_light","servo_loaded"})
    {
        audio::FAudioEngine engine(Rate);std::vector<float> data;
        for(int tick=0;tick<60*12;++tick)
        {
            aeroaudio::FTelemetry t;
            t.airspeed=12+4*std::sin(tick*.018);t.wingbeat_hz=4+.7*std::sin(tick*.027);
            t.servo_load_l=.45;t.servo_load_r=.35;t.sweep_rate=250+130*std::sin(tick*.055);
            t.stall_margin=.7;t.altitude=18;t.listener_distance=1;
            if(name=="wing_slow" || name=="wing_fast") {
                t.airspeed=name=="wing_slow" ? 5 : 25;t.wingbeat_hz=4;
            }
            if(name=="servo_light" || name=="servo_loaded") {
                t.servo_load_l=name=="servo_light" ? .1 : .9;t.sweep_rate=300;
            }
            if(name=="mix")
            {
                ui::FProps voices[5];aeroaudio::Mix(t,voices);
                for(int i=0;i<5;++i)engine.SetParams(aeroaudio::VoiceNames()[i],voices[i]);
            }
            else
            {
                const std::string voice=name.rfind("servo",0)==0 ? "servo_l" : (name.rfind("wing",0)==0 ? "wing" : name);
                auto p=aeroaudio::Compute(voice,t);
                aeroaudio::SetNum(p,"gain",tick<60 ? 0 : .65);
                engine.SetParams(name,p);
            }
            for(int frame=0;frame<Rate/60;++frame)
            {
                float l,r;engine.RenderFrame(l,r);
                pass &= std::isfinite(l)&&std::isfinite(r)&&std::abs(l)<.8&&std::abs(r)<.8;
                data.push_back(l);data.push_back(r);
            }
        }
        Wav(dir/(name+".wav"),data);
        // Reset must clear oscillator, filters and RNG, not merely the clock.
        engine.Reset();std::vector<float> reset;
        for(int i=0;i<4000;++i){float l,r;engine.RenderFrame(l,r);reset.push_back(l);}
        engine.Reset();for(float expected:reset){float l,r;engine.RenderFrame(l,r);pass &= l==expected;}
    }
    std::cout << "AudioQuality " << (pass?"PASS":"FAIL") << ": finite samples, headroom, deterministic restart\n";
    return pass ? 0 : 1;
}
