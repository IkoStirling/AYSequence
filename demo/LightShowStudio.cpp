#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include <AYRenderer.h>
#include <AYRenderer/PbrShaderSources.h>
#include <AYSequence/LightShowPresenter.h>
#include <AYSequence/LightShowAssetIO.h>
#include <AYSequence/LightShowAuthoring.h>
#if defined(AY_LIGHTSHOW_AUDIO)
#include <AYAudio/AudioEngine.h>
#include <AYResource/Converter/AudioDecoder.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <numbers>
#include <set>
#include <unordered_map>

using namespace ayt;
namespace {
constexpr int toolbarHeight=218;
enum Command { Play=100,Restart,Open,Save,Undo,Redo,AddKey,Circle,Sphere,Palette,Target,Property,Value,Time,ViewBoth,ViewStage,ViewSky,CheckRoutes,LoadGobo,SmokeBox,ToggleShadow,LoadMusic,ClearMusic,PlanRoute,ExportBundle,ToggleFogMotion,KeyLane,KeyDestination,KeyInterpolation,MoveKey,CopyKey,RemoveKey,ApplyInterpolation,PreviousKey,NextKey,TogglePointLights,ToggleFlux };
struct ShowRevision { std::shared_ptr<const sequence::CompiledLightShow> show; std::string path; };
#if defined(AY_LIGHTSHOW_AUDIO)
// Studio-only bridge: the decoded clip's playback head is the show clock.
// The runtime LightShowPlayer remains independent of audio and devices.
struct MusicTransport {
    audio::AudioEngine engine;
    audio::AudioClipId clip=audio::InvalidClipId;
    audio::VoiceHandle voice=audio::InvalidVoice;
    std::string path;
    std::array<float,1024> waveform{};
    double duration=0;
    bool loop=false;
    ~MusicTransport(){clear();engine.shutdown();}
    bool loaded() const{return clip!=audio::InvalidClipId;}
    void clear(){
        if(voice!=audio::InvalidVoice){engine.stop(voice);voice=audio::InvalidVoice;}
        if(clip!=audio::InvalidClipId){engine.releaseClip(clip);clip=audio::InvalidClipId;}
        path.clear();duration=0;waveform.fill(0);
    }
    bool load(const std::string& selected,bool repeat,double start,bool playing,std::string& error){
        std::error_code ec;
        const auto bytes=std::filesystem::file_size(std::filesystem::path(selected),ec);
        if(ec||bytes>128ull*1024*1024){error="Music file is missing or exceeds 128 MiB";return false;}
        resource::PcmBuffer pcm;
        if(!resource::decodeAudioFile(selected,pcm)||pcm.empty()||pcm.bitsPerSample!=16
            ||pcm.frameCount>UINT32_MAX||pcm.bytes.size()>256ull*1024*1024){
            error="Cannot decode WAV, MP3 or OGG music (256 MiB PCM limit)";return false;
        }
        std::array<float,1024> peaks{};
        const auto stride=std::max<uint64_t>(1,pcm.frameCount/(peaks.size()*256));
        for(uint64_t frame=0;frame<pcm.frameCount;frame+=stride){
            const auto bin=std::min<size_t>(peaks.size()-1,size_t(frame*peaks.size()/pcm.frameCount));
            for(uint16_t channel=0;channel<pcm.channels;++channel){
                const auto offset=size_t(frame*pcm.channels+channel)*2;
                const auto sample=static_cast<int16_t>(uint16_t(pcm.bytes[offset])|(uint16_t(pcm.bytes[offset+1])<<8));
                peaks[bin]=std::max(peaks[bin],float(std::abs(int(sample)))/32768);
            }
        }
        if(!engine.isInitialized()){
            audio::AudioSettings settings;settings.backend=audio::AudioBackendKind::Miniaudio;
            if(!engine.initialize(settings)){error="Cannot open the computer audio output";return false;}
        }
        audio::AudioClipDesc desc;desc.sampleRate=pcm.sampleRate;desc.channels=pcm.channels;
        desc.bitsPerSample=16;desc.format=audio::AudioSampleFormat::S16;
        desc.frameCount=static_cast<uint32_t>(pcm.frameCount);desc.pcmData=pcm.bytes.data();
        const auto nextClip=engine.registerClip(desc);
        if(nextClip==audio::InvalidClipId){error="Cannot register the music clip";return false;}
        const auto nextVoice=engine.play(nextClip,audio::AudioBus::Music,{},1,repeat);
        if(nextVoice==audio::InvalidVoice){engine.releaseClip(nextClip);error="Cannot start the music clip";return false;}
        const auto nextDuration=double(pcm.frameCount)/pcm.sampleRate;
        const auto frame=static_cast<uint64_t>(std::clamp(start,0.0,nextDuration)*engine.outputSampleRate());
        engine.setVoicePositionFrames(nextVoice,frame);
        engine.setVoicePaused(nextVoice,!playing);
        clear();clip=nextClip;voice=nextVoice;path=selected;duration=nextDuration;loop=repeat;waveform=peaks;
        error.clear();return true;
    }
    void pause(bool paused){if(voice!=audio::InvalidVoice)engine.setVoicePaused(voice,paused);}
    void seek(double seconds){
        if(voice!=audio::InvalidVoice){
            const auto frame=static_cast<uint64_t>(std::clamp(seconds,0.0,duration)*engine.outputSampleRate());
            engine.setVoicePositionFrames(voice,frame);
        }
    }
    void restart(bool playing){
        if(!loaded())return;
        if(voice!=audio::InvalidVoice)engine.stop(voice);
        voice=engine.play(clip,audio::AudioBus::Music,{},1,loop);
        if(voice!=audio::InvalidVoice)engine.setVoicePaused(voice,!playing);
    }
    bool setLoop(bool repeat,double seconds,bool playing){
        if(!loaded())return true;
        if(loop==repeat){seek(seconds);pause(!playing);return true;}
        const auto next=engine.play(clip,audio::AudioBus::Music,{},1,repeat);
        if(next==audio::InvalidVoice)return false;
        const auto frame=static_cast<uint64_t>(std::clamp(seconds,0.0,duration)*engine.outputSampleRate());
        engine.setVoicePositionFrames(next,frame);engine.setVoicePaused(next,!playing);
        if(voice!=audio::InvalidVoice)engine.stop(voice);
        voice=next;loop=repeat;return true;
    }
    void tick(float dt,sequence::LightShowPlayer& player){
        if(!loaded())return;
        engine.submitFrame(dt);
        audio::VoiceRuntime state;
        if(!engine.voiceState(voice,state)||!state.playing){
            if(player.playing()){player.synchronize(duration);player.pause();}
            return;
        }
        player.synchronize(double(engine.voicePositionFrames(voice))/engine.outputSampleRate());
        if(!player.playing())pause(true);
    }
};
#endif
struct Studio {
    render::Renderer renderer;
    sequence::LightShowPlayer player;
    sequence::LightShowPresenter presenter;
    sequence::LightShowFrame frame;
    std::shared_ptr<const sequence::CompiledLightShow> show;
    std::deque<ShowRevision> undo,redo;
    std::vector<std::vector<size_t>> targetMembers;
    int cameraMode=0;
    HWND window=nullptr,target=nullptr,property=nullptr,value=nullptr,time=nullptr,valueReadout=nullptr,timeline=nullptr;
    std::array<uint8_t,1024> timelineBins{};
    bool running=true,initialized=false,hidden=false;
    uint16_t width=1280,height=800;
    std::string path,error;
#if defined(AY_LIGHTSHOW_AUDIO)
    MusicTransport music;
#endif
    std::unordered_map<std::string,render::TextureHandle> goboTextures;
    HWND keyLane=nullptr,keyDestination=nullptr,keyInterpolation=nullptr;
    render::EmissivePointLightingSettings pointLighting{0};
    bool constantFlux=false;
    float minimumSeparation=.5f;
    std::vector<uint32_t> selectedFixtureIds() const {
        std::vector<uint32_t> result;
        const auto selected=SendMessageW(target,CB_GETCURSEL,0,0);
        if(selected>=0&&size_t(selected)<targetMembers.size())for(auto index:targetMembers[size_t(selected)])
            result.push_back(show->data().fixtures[index].id);
        return result;
    }
    void adjacentKey(bool next){
        const bool formations=SendMessageW(keyLane,CB_GETCURSEL,0,0)==1;
        const auto ids=selectedFixtureIds();double best=next?show->data().duration:0;bool found=false;
        const auto scan=[&](const auto& keys){for(const auto& key:keys){
            if((next&&key.time>player.time()+1e-6&&(!found||key.time<best))
                ||(!next&&key.time<player.time()-1e-6&&(!found||key.time>best))){best=key.time;found=true;}
        }};
        if(formations)scan(show->data().formations);
        else for(const auto& track:show->data().fixtures)if(std::find(ids.begin(),ids.end(),track.id)!=ids.end())scan(track.keys);
        if(found)seekPreview(best);
    }
    void editKeys(sequence::LightShowKeyEdit operation){
        error.clear();
        wchar_t text[64]{};GetWindowTextW(keyDestination,text,64);wchar_t* end=nullptr;
        const double destination=std::wcstod(text,&end);
        const bool timed=operation==sequence::LightShowKeyEdit::Move||operation==sequence::LightShowKeyEdit::Copy;
        if(timed&&(end==text||*end!=0||!std::isfinite(destination))){
            MessageBoxW(window,L"Enter a destination time in seconds.",L"Key edit",MB_OK|MB_ICONWARNING);return;
        }
        const bool formations=SendMessageW(keyLane,CB_GETCURSEL,0,0)==1;
        const auto ids=formations?std::vector<uint32_t>{}:selectedFixtureIds();sequence::LightShow edited;
        const auto mode=static_cast<sequence::ShowInterpolation>(SendMessageW(keyInterpolation,CB_GETCURSEL,0,0));
        if(sequence::editLightShowKeys(show->data(),ids,formations,player.time(),timed?destination:player.time(),operation,mode,edited,&error)){
            const auto time=timed?destination:player.time();edit(std::move(edited));seekPreview(time);
        }else MessageBoxA(window,error.c_str(),"Key edit",MB_OK|MB_ICONWARNING);
    }
    render::VolumetricSettings fog;
    void rebuildTimeline(){
        timelineBins.fill(0);if(!show)return;
        const auto mark=[&](double time,uint8_t flag){
            const auto bin=std::min<size_t>(timelineBins.size()-1,
                size_t(std::clamp(time/show->data().duration,0.0,1.0)*(timelineBins.size()-1)));
            timelineBins[bin]|=flag;
        };
        for(const auto& fixture:show->data().fixtures)for(const auto& key:fixture.keys)mark(key.time,1);
        for(const auto& key:show->data().formations)mark(key.time,2);
        if(timeline)InvalidateRect(timeline,nullptr,FALSE);
    }
    void seekPreview(double seconds){
        player.pause();player.seek(std::clamp(seconds,0.0,show->data().duration));
#if defined(AY_LIGHTSHOW_AUDIO)
        music.seek(player.time());music.pause(true);
#endif
        if(timeline)InvalidateRect(timeline,nullptr,FALSE);
    }
    void updateReadout() {
        if(!valueReadout||!value||!property)return;
        const double v=double(SendMessageW(value,TBM_GETPOS,0,0))/1000;
        const int selected=int(SendMessageW(property,CB_GETCURSEL,0,0));double number=v;const wchar_t* suffix=L"";
        switch(selected){
            case 0:number=v*2000;break;case 1:number=(v*2-1)*180;suffix=L" deg";break;
            case 2:number=(v*2-1)*1.4*180/std::numbers::pi;suffix=L" deg";break;
            case 3:number=.5+29.5*v;suffix=L" deg";break;case 4:number=v*360;suffix=L" deg";break;
            case 8:number=std::min(5,int(v*6));break;case 9:number=v*20;suffix=L" Hz";break;
            case 10:number=v*20;break;case 11:number=.1+29.9*v;suffix=L" deg";break;
            case 12:number=v*.25;break;
            case 13:number=v;break;case 14:number=.01+.99*v;break;
            case 15:number=.25+3.75*v;break;
            case 16:number=4*v;suffix=L" px";break;
        }
        wchar_t text[64];swprintf_s(text,L"%.3g%ls",number,suffix);SetWindowTextW(valueReadout,text);
    }
    void targets() {
        const auto selected=SendMessageW(target,CB_GETCURSEL,0,0);std::wstring previous;
        if(selected>=0){const auto length=SendMessageW(target,CB_GETLBTEXTLEN,selected,0);if(length>=0){previous.resize(size_t(length)+1);SendMessageW(target,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(previous.data()));previous.resize(size_t(length));}}
        SendMessageW(target,CB_RESETCONTENT,0,0);targetMembers.clear();
        const auto add=[&](const std::wstring& label,std::vector<size_t> members){SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));targetMembers.push_back(std::move(members));};
        std::vector<size_t> all;std::set<std::string> groups;
        for(size_t i=0;i<show->data().fixtures.size();++i){all.push_back(i);if(!show->data().fixtures[i].group.empty())groups.insert(show->data().fixtures[i].group);}
        add(L"All fixtures",std::move(all));
        for(const auto& group:groups){std::vector<size_t> members;for(size_t i=0;i<show->data().fixtures.size();++i)if(show->data().fixtures[i].group==group)members.push_back(i);add(L"Group: "+std::filesystem::path(group).wstring(),std::move(members));}
        for(size_t i=0;i<show->data().fixtures.size();++i)add(L"Fixture "+std::to_wstring(show->data().fixtures[i].id),{i});
        const auto restored=SendMessageW(target,CB_FINDSTRINGEXACT,WPARAM(-1),reinterpret_cast<LPARAM>(previous.c_str()));
        SendMessageW(target,CB_SETCURSEL,restored>=0?restored:0,0);
    }
    bool prepareGobos(const sequence::CompiledLightShow& next,const std::string& sourcePath) {
        std::vector<std::pair<uint32_t,render::TextureHandle>> bindings;
        for(const auto& fixture:next.data().fixtures) {
            if(std::none_of(fixture.keys.begin(),fixture.keys.end(),[](const auto& key){return key.value.gobo==sequence::ShowGobo::Texture;}))continue;
            if(sourcePath.empty()){error="Save the .lshow file before choosing a Gobo image";return false;}
            const auto resolved=(std::filesystem::absolute(std::filesystem::path(sourcePath)).parent_path()
                /std::filesystem::u8path(fixture.goboTexturePath)).lexically_normal();
            const auto key=resolved.string();
            auto found=goboTextures.find(key);
            if(found==goboTextures.end()) {
                const auto texture=renderer.createTextureFromFile(key);
                if(!texture.isValid()){error="Cannot load Gobo image: "+key;return false;}
                found=goboTextures.emplace(key,texture).first;
            }
            bindings.emplace_back(fixture.id,found->second);
        }
        presenter.clearGoboTextures();
        for(const auto& [id,texture]:bindings)presenter.bindGoboTexture(id,texture);
        error.clear();return true;
    }
    bool install(std::shared_ptr<const sequence::CompiledLightShow> next,bool keepHistory,const std::string& sourcePath) {
        if(!next||!prepareGobos(*next,sourcePath))return false;
        const auto t=player.time();const bool playing=player.playing();
        if(keepHistory){undo.push_back({show,path});redo.clear();while(undo.size()>8)undo.pop_front();}
        show=std::move(next);player.bind(show);
        path=sourcePath;player.seek(std::min(t,show->data().duration));if(playing)player.play();
#if defined(AY_LIGHTSHOW_AUDIO)
        if(music.loaded()&&!music.setLoop(show->data().loop,player.time(),player.playing())){
            music.clear();error="Music stopped while changing show loop mode";
        }
#endif
        targets();rebuildTimeline();return true;
    }
    void edit(sequence::LightShow data) {
        auto next=sequence::CompiledLightShow::compile(std::move(data),&error);
        if(next)install(std::move(next),true,path);
    }
    void key() {
        if(!show->evaluate(player.time(),frame,false))return;
        auto data=show->data();const auto selected=SendMessageW(target,CB_GETCURSEL,0,0);
        const auto propertyIndex=SendMessageW(property,CB_GETCURSEL,0,0);
        const float v=float(SendMessageW(value,TBM_GETPOS,0,0))/1000;
        for(size_t i=0;i<data.fixtures.size();++i) {
            if(selected<0||size_t(selected)>=targetMembers.size())continue;
            const auto& members=targetMembers[size_t(selected)];if(std::find(members.begin(),members.end(),i)==members.end())continue;
            auto& track=data.fixtures[i];auto state=frame.fixtures[i].value;
            switch(propertyIndex) {
                case 0:state.intensity=v*2000;break;
                case 1:state.pan=(v*2-1)*float(std::numbers::pi);break;
                case 2:state.tilt=(v*2-1)*1.4f;break;
                case 3:state.outerAngle=(.5f+v*29.5f)*float(std::numbers::pi/180);state.innerAngle=std::min(state.innerAngle,state.outerAngle*.75f);break;
                case 4:state.goboRotation=v*float(2*std::numbers::pi);break;
                case 5:state.color[0]=v;break;case 6:state.color[1]=v;break;case 7:state.color[2]=v;break;
                case 8:state.gobo=static_cast<sequence::ShowGobo>(std::min(5,int(v*6)));break;
                case 9:track.strobeFrequency=v*20;break;
                case 10:state.volumetric=v*20;break;
                case 11:state.innerAngle=(.1f+v*29.9f)*float(std::numbers::pi/180);state.outerAngle=std::max(state.outerAngle,state.innerAngle+.005f);break;
                case 12:state.apertureRadius=v*.25f;break;
                case 13:state.goboFocus=v;break;
                case 14:state.iris=.01f+.99f*v;break;
                case 15:state.goboScale=.25f+3.75f*v;break;
                case 16:state.shadowSoftness=4*v;break;
            }
            const double time=player.time();
            auto found=std::lower_bound(track.keys.begin(),track.keys.end(),time,[](const auto& k,double t){return k.time<t;});
            if(found!=track.keys.end()&&std::abs(found->time-time)<1e-6)found->value=state;
            else track.keys.insert(found,{time,state});
        }
        edit(std::move(data));
    }
    void formation(bool sphere) {
        if(!player.evaluate(frame)||frame.points.empty())return;
        auto data=show->data();sequence::FormationKey key;key.time=player.time();key.points=frame.points;
        const auto count=key.points.size();
        for(size_t i=0;i<count;++i) {
            const float h=1-2*(float(i)+.5f)/float(count),r=std::sqrt(std::max(0.f,1-h*h));
            const float a=sphere?float(i*2.399963229728653):float(2*std::numbers::pi*i/count);
            key.points[i].position=sphere?sequence::ShowVec3{12*r*std::cos(a),18+12*h,12*r*std::sin(a)}
                :sequence::ShowVec3{12*std::cos(a),18+6*std::sin(a),0};
        }
        auto it=std::lower_bound(data.formations.begin(),data.formations.end(),key.time,[](const auto& k,double t){return k.time<t;});
        if(it!=data.formations.end()&&std::abs(it->time-key.time)<1e-6)*it=std::move(key);
        else data.formations.insert(it,std::move(key));edit(std::move(data));
    }
    void file(bool save) {
        wchar_t filename[32768]{};
        const auto existing=std::filesystem::path(path).wstring();
        if(existing.size()<std::size(filename))std::copy(existing.begin(),existing.end(),filename);
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
        dialog.lpstrFilter=L"Light show (*.lshow)\0*.lshow\0\0";dialog.lpstrFile=filename;
        dialog.nMaxFile=static_cast<DWORD>(std::size(filename));dialog.lpstrDefExt=L"lshow";
        dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(!(save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog)))return;
        const auto selected=std::filesystem::path(filename).string();
        if(save){
            const auto base=std::filesystem::absolute(std::filesystem::path(selected)).parent_path();
            for(const auto& fixture:show->data().fixtures) {
                if(std::none_of(fixture.keys.begin(),fixture.keys.end(),[](const auto& key){return key.value.gobo==sequence::ShowGobo::Texture;}))continue;
                if(!std::filesystem::exists(base/std::filesystem::u8path(fixture.goboTexturePath))) {
                    error="Copy Gobo images beside the new .lshow location before Save As";
                    MessageBoxA(window,error.c_str(),"Save light show",MB_OK|MB_ICONWARNING);return;
                }
            }
            if(sequence::saveLightShow(selected,show->data(),&error)&&prepareGobos(*show,selected))path=selected;
        }
        else {
            sequence::LightShow data;std::string packagedMusic;
            if(!sequence::loadLightShow(selected,data,&error)||!sequence::bundledLightShowMusic(selected,packagedMusic,&error)){
                MessageBoxA(window,error.c_str(),"Open light show",MB_OK|MB_ICONWARNING);return;
            }
            if(!install(sequence::CompiledLightShow::compile(std::move(data)),true,selected)){
                MessageBoxA(window,error.c_str(),"Open light show",MB_OK|MB_ICONWARNING);return;
            }
#if defined(AY_LIGHTSHOW_AUDIO)
            music.clear();
            if(!packagedMusic.empty()&&!music.load(packagedMusic,show->data().loop,player.time(),player.playing(),error))
                MessageBoxA(window,error.c_str(),"Packaged music",MB_OK|MB_ICONWARNING);
#endif
        }
    }
    void exportBundle(){
        if(path.empty()){
            MessageBoxW(window,L"Save the .lshow file before exporting.",L"Export show folder",MB_OK|MB_ICONINFORMATION);return;
        }
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
        dialog.lpstrFilter=L"Light show package folder (*.lshowpkg)\0*.lshowpkg\0\0";
        dialog.lpstrFile=filename;dialog.nMaxFile=static_cast<DWORD>(std::size(filename));
        dialog.lpstrDefExt=L"lshowpkg";dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&dialog))return;
        const auto destination=std::filesystem::path(filename).string();
        if(!sequence::saveLightShow(path,show->data(),&error)){
            MessageBoxA(window,error.c_str(),"Export show folder",MB_OK|MB_ICONWARNING);return;
        }
        std::string musicPath;
#if defined(AY_LIGHTSHOW_AUDIO)
        if(music.loaded())musicPath=music.path;
#endif
        if(!sequence::exportLightShowBundle(path,destination,musicPath,&error)){
            MessageBoxA(window,error.c_str(),"Export show folder",MB_OK|MB_ICONWARNING);return;
        }
        const auto message=L"Show folder created at:\n"+std::filesystem::path(destination).wstring();
        MessageBoxW(window,message.c_str(),L"Export show folder",MB_OK|MB_ICONINFORMATION);
    }
    void loadGobo() {
        if(path.empty()){MessageBoxW(window,L"Save the .lshow file first.",L"Gobo image",MB_OK|MB_ICONINFORMATION);return;}
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
        dialog.lpstrFilter=L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0\0";
        dialog.lpstrFile=filename;dialog.nMaxFile=static_cast<DWORD>(std::size(filename));
        dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(!GetOpenFileNameW(&dialog))return;
        const auto base=std::filesystem::absolute(std::filesystem::path(path)).parent_path();
        const auto relative=std::filesystem::path(filename).lexically_relative(base);
        if(relative.empty()||relative.is_absolute()||std::find(relative.begin(),relative.end(),std::filesystem::path(".."))!=relative.end()) {
            MessageBoxW(window,L"Choose an image inside the .lshow folder or a child folder.",L"Gobo image",MB_OK|MB_ICONWARNING);return;
        }
        const auto selected=SendMessageW(target,CB_GETCURSEL,0,0);
        if(selected<0||size_t(selected)>=targetMembers.size())return;
        auto data=show->data();
        for(const auto index:targetMembers[size_t(selected)]) {
            auto& track=data.fixtures[index];track.goboTexturePath=relative.generic_string();
            for(auto& key:track.keys)key.value.gobo=sequence::ShowGobo::Texture;
        }
        edit(std::move(data));
        if(!error.empty())MessageBoxA(window,error.c_str(),"Gobo image",MB_OK|MB_ICONWARNING);
    }
#if defined(AY_LIGHTSHOW_AUDIO)
    void loadMusic(){
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
        dialog.lpstrFilter=L"Music (*.wav;*.mp3;*.ogg)\0*.wav;*.mp3;*.ogg\0\0";
        dialog.lpstrFile=filename;dialog.nMaxFile=static_cast<DWORD>(std::size(filename));
        dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(!GetOpenFileNameW(&dialog))return;
        if(!music.load(std::filesystem::path(filename).string(),show->data().loop,player.time(),player.playing(),error))
            MessageBoxA(window,error.c_str(),"Music",MB_OK|MB_ICONWARNING);
    }
#endif
    void command(int id) {
        if(id==Property)updateReadout();
        if(id==PreviousKey)adjacentKey(false);if(id==NextKey)adjacentKey(true);
        if(id==MoveKey)editKeys(sequence::LightShowKeyEdit::Move);
        if(id==CopyKey)editKeys(sequence::LightShowKeyEdit::Copy);
        if(id==RemoveKey)editKeys(sequence::LightShowKeyEdit::Remove);
        if(id==ApplyInterpolation)editKeys(sequence::LightShowKeyEdit::Interpolation);
        if(id==TogglePointLights){pointLighting.maxLights=pointLighting.maxLights?0:16;presenter.setPointLighting(pointLighting);}
        if(id==ToggleFlux){constantFlux=!constantFlux;presenter.setConstantFlux(constantFlux);}
        if(id==ViewBoth)cameraMode=0;if(id==ViewStage)cameraMode=1;if(id==ViewSky)cameraMode=2;
        if(id==Play){
            if(player.playing()){
                player.pause();
#if defined(AY_LIGHTSHOW_AUDIO)
                music.pause(true);
#endif
            }else{
                bool restart=!show->data().loop&&player.time()>=show->data().duration;
#if defined(AY_LIGHTSHOW_AUDIO)
                if(music.loaded()){
                    audio::VoiceRuntime state;
                    restart|=!music.engine.voiceState(music.voice,state)||!state.playing;
                }
#endif
                if(restart){player.stop();player.play();
#if defined(AY_LIGHTSHOW_AUDIO)
                    music.restart(true);
#endif
                }else{player.play();
#if defined(AY_LIGHTSHOW_AUDIO)
                    music.pause(false);
#endif
                }
            }
        }
        if(id==Restart){player.stop();player.play();
#if defined(AY_LIGHTSHOW_AUDIO)
            music.restart(true);
#endif
        }
        if(id==Open)file(false);if(id==Save)file(true);if(id==ExportBundle)exportBundle();if(id==AddKey)key();
        if(id==LoadGobo)loadGobo();
#if defined(AY_LIGHTSHOW_AUDIO)
        if(id==LoadMusic)loadMusic();
        if(id==ClearMusic)music.clear();
#endif
        if(id==Circle)formation(false);if(id==Sphere)formation(true);
        if(id==CheckRoutes){
            sequence::FormationClearanceReport report;
            if(show->analyzeFormationClearance(minimumSeparation,2000000,64,report)){
                wchar_t message[256];
                if(report.conflicts.empty())
                    swprintf_s(message,L"No close approaches found (minimum %.2f units).%ls",
                        minimumSeparation,report.complete?L"":L" Search limit reached; result is incomplete.");
                else
                    swprintf_s(message,L"%zu close approaches found (minimum %.2f units). First: IDs %u / %u at %.3f s, distance %.3f.%ls",
                        report.conflicts.size(),minimumSeparation,report.conflicts.front().firstId,
                        report.conflicts.front().secondId,report.conflicts.front().time,
                        report.conflicts.front().distance,report.complete?L"":L" Search limit reached; result is incomplete.");
                MessageBoxW(window,message,L"Formation clearance",MB_OK|MB_ICONINFORMATION);
            }
        }
        if(id==PlanRoute){
            const auto& formations=show->data().formations;
            if(formations.size()<2){MessageBoxW(window,L"Add two formation keys before planning a route.",L"Formation route",MB_OK|MB_ICONINFORMATION);}
            else{
                const auto next=std::upper_bound(formations.begin(),formations.end(),player.time(),
                    [](double time,const auto& key){return time<key.time;});
                const auto index=std::min(formations.size()-2,
                    next==formations.begin()?size_t(0):size_t(next-formations.begin()-1));
                sequence::LightShow planned;std::string diagnostic;
                sequence::FormationRouteOptions options;options.minimumDistance=minimumSeparation;
                if(sequence::planFormationTransition(show->data(),index,options,planned,&diagnostic)){
                    const bool changed=planned.formations.size()!=formations.size();
                    error.clear();if(changed)edit(std::move(planned));
                    if(!error.empty())MessageBoxA(window,error.c_str(),"Formation route",MB_OK|MB_ICONWARNING);
                    else MessageBoxW(window,changed?L"A continuously checked detour was added."
                        :L"This transition already meets the selected separation.",
                        L"Formation route",MB_OK|MB_ICONINFORMATION);
                }else MessageBoxA(window,diagnostic.c_str(),"Formation route",MB_OK|MB_ICONWARNING);
            }
        }
        if(id==Palette){auto data=show->data();for(auto& key:data.formations)for(auto& p:key.points)std::rotate(p.color.begin(),p.color.begin()+1,p.color.end());edit(std::move(data));}
        if(id==SmokeBox){fog.local=!fog.local;if(initialized)renderer.setVolumetricSettings(fog);}
        if(id==ToggleFogMotion){fog.noiseStrength=fog.noiseStrength>0?0:.35f;if(initialized)renderer.setVolumetricSettings(fog);}
        if(id==ToggleShadow){
            const auto selected=SendMessageW(target,CB_GETCURSEL,0,0);
            if(selected>=0&&size_t(selected)<targetMembers.size()){
                auto data=show->data();
                for(const auto index:targetMembers[size_t(selected)]){
                    auto& keys=data.fixtures[index].keys;
                    const bool enabled=!keys.front().value.shadow;
                    for(auto& key:keys)key.value.shadow=enabled;
                }
                edit(std::move(data));
            }
        }
        if(id==Undo&&!undo.empty()){auto next=undo.back(),previous=ShowRevision{show,path};if(install(next.show,false,next.path)){redo.push_back(previous);undo.pop_back();}}
        if(id==Redo&&!redo.empty()){auto next=redo.back(),previous=ShowRevision{show,path};if(install(next.show,false,next.path)){undo.push_back(previous);redo.pop_back();}}
    }
};
HWND control(HWND parent,const wchar_t* type,const wchar_t* title,int id,int x,int y,int w,int h,DWORD style=0) {
    return CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
}
LRESULT CALLBACK timelineProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* s=reinterpret_cast<Studio*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){
        s=static_cast<Studio*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
    }
    if(!s)return DefWindowProcW(window,message,w,l);
    if(message==WM_PAINT){
        PAINTSTRUCT paint;const auto dc=BeginPaint(window,&paint);RECT rect;GetClientRect(window,&rect);
        SetDCBrushColor(dc,RGB(19,24,34));FillRect(dc,&rect,HBRUSH(GetStockObject(DC_BRUSH)));
        SelectObject(dc,GetStockObject(DC_PEN));SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,RGB(180,190,210));
        TextOutW(dc,8,8,L"Music",5);TextOutW(dc,8,37,L"Lights",6);TextOutW(dc,8,50,L"Formation",9);
        const int left=100,right=std::max(left+1,int(rect.right)-8),span=right-left;
        SetDCPenColor(dc,RGB(54,63,80));
        for(int i=0;i<=10;++i){const int x=left+span*i/10;MoveToEx(dc,x,2,nullptr);LineTo(dc,x,rect.bottom-2);}
#if defined(AY_LIGHTSHOW_AUDIO)
        SetDCPenColor(dc,RGB(65,165,190));
        for(size_t i=0;i<s->music.waveform.size();++i){
            const int x=left+int(i*span/(s->music.waveform.size()-1));
            const int half=int(s->music.waveform[i]*12);
            MoveToEx(dc,x,19-half,nullptr);LineTo(dc,x,20+half);
        }
#endif
        for(size_t i=0;i<s->timelineBins.size();++i){
            const int x=left+int(i*span/(s->timelineBins.size()-1));
            if(s->timelineBins[i]&1){SetDCPenColor(dc,RGB(245,153,67));MoveToEx(dc,x,36,nullptr);LineTo(dc,x,46);}
            if(s->timelineBins[i]&2){SetDCPenColor(dc,RGB(127,216,152));MoveToEx(dc,x,49,nullptr);LineTo(dc,x,59);}
        }
        if(s->show){
            const int x=left+int(std::clamp(s->player.time()/s->show->data().duration,0.0,1.0)*span);
            SetDCPenColor(dc,RGB(255,255,255));MoveToEx(dc,x,1,nullptr);LineTo(dc,x,rect.bottom-1);
        }
        EndPaint(window,&paint);return 0;
    }
    if((message==WM_LBUTTONDOWN||message==WM_MOUSEMOVE&&(w&MK_LBUTTON))&&s->show){
        RECT rect;GetClientRect(window,&rect);
        const int span=std::max(1,int(rect.right)-108),x=int(short(LOWORD(l)));
        s->seekPreview(std::clamp(double(x-100)/span,0.0,1.0)*s->show->data().duration);
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* s=reinterpret_cast<Studio*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){s=static_cast<Studio*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));s->window=window;}
    if(!s)return DefWindowProcW(window,message,w,l);
    if(message==WM_CLOSE){s->running=false;return 0;}
    if(message==WM_COMMAND){s->command(LOWORD(w));return 0;}
    if(message==WM_SIZE){s->width=static_cast<uint16_t>(std::max(1,int(LOWORD(l))));s->height=static_cast<uint16_t>(std::max(toolbarHeight+1,int(HIWORD(l))));if(s->timeline)MoveWindow(s->timeline,8,146,std::max(1,int(s->width)-16),64,TRUE);if(s->initialized)s->renderer.resize(s->width,s->height);return 0;}
    if(message==WM_HSCROLL&&reinterpret_cast<HWND>(l)==s->time){s->seekPreview(double(SendMessageW(s->time,TBM_GETPOS,0,0))/1000*s->show->data().duration);return 0;}
    if(message==WM_HSCROLL&&reinterpret_cast<HWND>(l)==s->value){s->updateReadout();return 0;}
    return DefWindowProcW(window,message,w,l);
}
}
int main(int argc,char** argv) {
    uint32_t fixtures=32,points=1000,frames=0,steps=48;double startTime=0;
    uint32_t windowWidth=1280,windowHeight=800;
    uint32_t surfaceBudget=64,volumeBudget=32,shadowBudget=4;
    uint32_t pointBudget=65536;
    bool stepsExplicit=false,volumeExplicit=false,pointExplicit=false,pointCpu=false,staticFog=false;
    render::PerformanceLightBudget lightBudget{64,32,4};
    bool patterns=false;std::string capture,preset,sceneMode="occluders",musicPath;
    render::Backend backend=render::Backend::Direct3D11;Studio s;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--hidden")s.hidden=true;
        else if(arg=="--frames"&&i+1<argc)frames=std::stoul(argv[++i]);
        else if(arg=="--points"&&i+1<argc)points=std::stoul(argv[++i]);
        else if(arg=="--fixtures"&&i+1<argc)fixtures=std::stoul(argv[++i]);
        else if(arg=="--steps"&&i+1<argc){steps=std::stoul(argv[++i]);stepsExplicit=true;}
        else if(arg=="--width"&&i+1<argc)windowWidth=std::stoul(argv[++i]);
        else if(arg=="--height"&&i+1<argc)windowHeight=std::stoul(argv[++i]);
        else if(arg=="--surface-lights"&&i+1<argc)surfaceBudget=std::stoul(argv[++i]);
        else if(arg=="--volume-lights"&&i+1<argc){volumeBudget=std::stoul(argv[++i]);volumeExplicit=true;}
        else if(arg=="--shadow-lights"&&i+1<argc)shadowBudget=std::stoul(argv[++i]);
        else if(arg=="--point-budget"&&i+1<argc){pointBudget=std::stoul(argv[++i]);pointExplicit=true;}
        else if(arg=="--point-lights"&&i+1<argc){const auto count=std::stoul(argv[++i]);if(count>64)return 1;s.pointLighting.maxLights=uint16_t(count);}
        else if(arg=="--point-light-cell"&&i+1<argc)s.pointLighting.cellSize=std::stof(argv[++i]);
        else if(arg=="--point-light-range"&&i+1<argc)s.pointLighting.range=std::stof(argv[++i]);
        else if(arg=="--point-light-scale"&&i+1<argc)s.pointLighting.intensityScale=std::stof(argv[++i]);
        else if(arg=="--constant-flux")s.constantFlux=true;
        else if(arg=="--point-cpu")pointCpu=true;
        else if(arg=="--static-fog")staticFog=true;
        else if(arg=="--smoke-box"&&i+6<argc){s.fog.local=true;s.fog.center={std::stof(argv[++i]),std::stof(argv[++i]),std::stof(argv[++i])};s.fog.halfExtent={std::stof(argv[++i]),std::stof(argv[++i]),std::stof(argv[++i])};}
        else if(arg=="--min-separation"&&i+1<argc)s.minimumSeparation=std::stof(argv[++i]);
        else if(arg=="--patterns")patterns=true;
        else if(arg=="--preset"&&i+1<argc)preset=argv[++i];
        else if(arg=="--scene"&&i+1<argc)sceneMode=argv[++i];
        else if(arg=="--time"&&i+1<argc)startTime=std::stod(argv[++i]);
        else if(arg=="--view"&&i+1<argc){const std::string view=argv[++i];s.cameraMode=view=="stage"?1:view=="sky"?2:0;}
        else if(arg=="--capture"&&i+1<argc)capture=argv[++i];
        else if(arg=="--music"&&i+1<argc)musicPath=argv[++i];
        else if(arg=="--backend"&&i+1<argc)backend=std::string(argv[++i])=="d3d12"?render::Backend::Direct3D12:render::Backend::Direct3D11;
        else s.path=arg;
    }
    s.presenter.setPointLighting(s.pointLighting);s.presenter.setConstantFlux(s.constantFlux);
    sequence::LightShow data=sequence::makeLightShowExample(fixtures,points);
    if(preset=="single") {
        s.cameraMode=3;
        data=sequence::makeLightShowExample(1,0);auto& track=data.fixtures.front();
        track.sweepPan=track.sweepTilt=0;auto& value=track.keys.front().value;
        value.position={0,7,0};value.pan=0;value.tilt=.35f;value.color={.25f,.65f,1};
    }
    if(patterns)for(size_t i=0;i<data.fixtures.size();++i)
        for(auto& key:data.fixtures[i].keys)key.value.gobo=static_cast<sequence::ShowGobo>(i%5);
    if(windowWidth>=1600){
        if(!stepsExplicit)steps=24;
        if(!volumeExplicit)volumeBudget=12;
        if(!pointExplicit)pointBudget=10000;
    }
    bool smokeValid=true;
    for(int axis=0;axis<3;++axis)smokeValid &= std::isfinite(s.fog.center[axis])&&std::abs(s.fog.center[axis])<=100000
        &&std::isfinite(s.fog.halfExtent[axis])&&s.fog.halfExtent[axis]>=.01f&&s.fog.halfExtent[axis]<=10000;
    if(steps<8||steps>64||windowWidth<1280||windowWidth>8192||windowHeight<480||windowHeight>8192
        ||surfaceBudget>256||volumeBudget>64||shadowBudget>8||pointBudget>65536
        ||!std::isfinite(s.minimumSeparation)||s.minimumSeparation<=0
        ||!smokeValid
        ||(sceneMode!="air"&&sceneMode!="floor"&&sceneMode!="occluders")){
        std::fprintf(stderr,"Invalid viewport, light budget, --steps, --min-separation or --scene\n");return 2;
    }
    s.width=static_cast<uint16_t>(windowWidth);s.height=static_cast<uint16_t>(windowHeight);
    lightBudget={static_cast<uint16_t>(surfaceBudget),static_cast<uint8_t>(volumeBudget),static_cast<uint8_t>(shadowBudget)};
    if(!s.path.empty()&&!sequence::loadLightShow(s.path,data,&s.error)){std::fprintf(stderr,"%s\n",s.error.c_str());return 1;}
    s.show=sequence::CompiledLightShow::compile(std::move(data),&s.error);if(!s.show)return 2;
    s.rebuildTimeline();
    s.player.bind(s.show);if(!s.player.seek(startTime)){std::fprintf(stderr,"Invalid --time\n");return 2;}s.player.play();
#if defined(AY_LIGHTSHOW_AUDIO)
    if(musicPath.empty()&&!s.path.empty()&&!sequence::bundledLightShowMusic(s.path,musicPath,&s.error)){
        std::fprintf(stderr,"%s\n",s.error.c_str());return 2;
    }
    if(!musicPath.empty()&&!s.music.load(musicPath,s.show->data().loop,s.player.time(),true,s.error)){
        std::fprintf(stderr,"%s\n",s.error.c_str());return 2;
    }
#else
    if(!musicPath.empty()){std::fprintf(stderr,"--music requires AY_ENABLE_AUDIO=ON\n");return 2;}
#endif
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW timelineClass{};timelineClass.lpfnWndProc=timelineProcedure;timelineClass.hInstance=GetModuleHandleW(nullptr);
    timelineClass.lpszClassName=L"AliyatLightShowTimeline";timelineClass.hCursor=LoadCursor(nullptr,IDC_HAND);RegisterClassW(&timelineClass);
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AliyatLightShowStudio";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
    RECT windowRect{0,0,LONG(s.width),LONG(s.height)};
    AdjustWindowRectEx(&windowRect,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,FALSE,0);
    const HWND window=CreateWindowExW(0,wc.lpszClassName,L"Aliyat Light Show Studio",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,
        windowRect.right-windowRect.left,windowRect.bottom-windowRect.top,nullptr,nullptr,wc.hInstance,&s);
    if(!window)return 3;
    int x=8;for(const auto& [label,id]:{std::pair{L"Play / Pause",Play},{L"Restart",Restart},{L"Open",Open},{L"Save As",Save},{L"Undo",Undo},{L"Redo",Redo},{L"Circle key",Circle},{L"Sphere key",Sphere},{L"Palette",Palette}}){control(window,L"BUTTON",label,id,x,6,100,26);x+=104;}
    s.time=control(window,TRACKBAR_CLASSW,L"",Time,950,4,280,28,TBS_NOTICKS);SendMessageW(s.time,TBM_SETRANGE,TRUE,MAKELPARAM(0,1000));
    s.target=control(window,L"COMBOBOX",L"",Target,8,42,170,350,CBS_DROPDOWNLIST|WS_VSCROLL);
    s.property=control(window,L"COMBOBOX",L"",Property,188,42,170,350,CBS_DROPDOWNLIST);
    for(const auto* name:{L"Intensity",L"Pan",L"Tilt",L"Field half-angle (deg)",L"Pattern rotation",L"Red",L"Green",L"Blue",L"Pattern",L"Strobe Hz",L"Volume strength",L"Core half-angle (deg)",L"Aperture radius (units)",L"Pattern focus",L"Iris opening",L"Pattern scale",L"Shadow softness (px)"})SendMessageW(s.property,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
    SendMessageW(s.property,CB_SETCURSEL,0,0);s.targets();
    s.value=control(window,TRACKBAR_CLASSW,L"",Value,370,39,230,30,TBS_NOTICKS);SendMessageW(s.value,TBM_SETRANGE,TRUE,MAKELPARAM(0,1000));SendMessageW(s.value,TBM_SETPOS,TRUE,500);
    s.valueReadout=control(window,L"STATIC",L"",0,604,46,76,22);s.updateReadout();
    control(window,L"BUTTON",L"Add / update key",AddKey,680,41,150,27);
    control(window,L"BUTTON",L"Both",ViewBoth,846,41,100,27);control(window,L"BUTTON",L"Stage",ViewStage,954,41,100,27);control(window,L"BUTTON",L"Sky",ViewSky,1062,41,100,27);
    control(window,L"BUTTON",L"Check routes",CheckRoutes,1168,41,112,27);
    control(window,L"BUTTON",L"Gobo image...",LoadGobo,8,77,130,26);
    control(window,L"BUTTON",L"Smoke box on/off",SmokeBox,146,77,145,26);
    control(window,L"BUTTON",L"Shadows on/off",ToggleShadow,299,77,135,26);
    control(window,L"BUTTON",L"Plan route",PlanRoute,718,77,130,26);
    control(window,L"BUTTON",L"Export show folder...",ExportBundle,856,77,180,26);
    control(window,L"BUTTON",L"Animated fog on/off",ToggleFogMotion,1044,77,188,26);
    s.keyLane=control(window,L"COMBOBOX",L"",KeyLane,8,112,150,150,CBS_DROPDOWNLIST);
    for(const auto* label:{L"Fixture keys",L"Formation keys"})SendMessageW(s.keyLane,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendMessageW(s.keyLane,CB_SETCURSEL,0,0);
    control(window,L"BUTTON",L"Previous",PreviousKey,166,112,80,26);control(window,L"BUTTON",L"Next",NextKey,250,112,65,26);
    control(window,L"STATIC",L"To (s)",0,323,117,48,22);
    s.keyDestination=control(window,L"EDIT",L"1",KeyDestination,372,112,80,26,WS_BORDER|ES_AUTOHSCROLL);
    control(window,L"BUTTON",L"Move",MoveKey,460,112,65,26);control(window,L"BUTTON",L"Copy",CopyKey,533,112,65,26);
    control(window,L"BUTTON",L"Delete",RemoveKey,606,112,65,26);
    s.keyInterpolation=control(window,L"COMBOBOX",L"",KeyInterpolation,679,112,90,150,CBS_DROPDOWNLIST);
    for(const auto* label:{L"Step",L"Linear",L"Smooth"})SendMessageW(s.keyInterpolation,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendMessageW(s.keyInterpolation,CB_SETCURSEL,1,0);
    control(window,L"BUTTON",L"Set mode",ApplyInterpolation,777,112,90,26);
    control(window,L"BUTTON",L"Point light on/off",TogglePointLights,875,112,160,26);
    control(window,L"BUTTON",L"Flux zoom on/off",ToggleFlux,1043,112,160,26);
    s.timeline=CreateWindowExW(0,timelineClass.lpszClassName,L"",WS_CHILD|WS_VISIBLE,8,146,
        int(s.width)-16,64,window,nullptr,wc.hInstance,&s);
#if defined(AY_LIGHTSHOW_AUDIO)
    control(window,L"BUTTON",L"Music...",LoadMusic,442,77,130,26);
    control(window,L"BUTTON",L"Clear music",ClearMusic,580,77,130,26);
#endif
    if(!s.hidden)ShowWindow(window,SW_SHOW);
    render::InitDesc init;init.windowHandle=window;init.width=s.width;init.height=s.height;init.vsync=false;init.msaa=0;init.backend=backend;
    if(!s.renderer.initialize(init)){DestroyWindow(window);return 4;}s.initialized=true;
    if(!s.prepareGobos(*s.show,s.path)){std::fprintf(stderr,"%s\n",s.error.c_str());s.renderer.shutdown();DestroyWindow(window);return 4;}
    s.renderer.configurePipeline(render::RenderPipelineDesc::makeDeferred());s.renderer.setAmbientStrength(.08f);
    s.renderer.setPostProcessBloomStrength(.2f);s.renderer.setPostProcessBloomThreshold(1);s.renderer.setPostProcessExposure(1);
    s.fog.enabled=true;s.fog.density=.015f;s.fog.steps=static_cast<uint8_t>(steps);s.fog.maxDistance=100;s.fog.anisotropy=.2f;
    s.fog.noiseStrength=staticFog?0.f:.35f;s.fog.noiseScale=.2f;s.renderer.setVolumetricSettings(s.fog);
    const auto cube=s.renderer.createTexturedUnitCube();const auto material=s.renderer.createMaterialFromPhoskia(render::kPbrPhoskiaSource,"light_show_stage_pbr");
    s.renderer.setMaterialColor(material,"baseColor",.3f,.32f,.36f,1);s.renderer.setMaterialFloat(material,"roughness",.65f);
    auto previous=std::chrono::steady_clock::now();
    std::vector<double> timings,gpuTimings,presenterTimings,renderTimings;uint32_t rendered=0;
    while(s.running&&(!frames||rendered<frames+8)) {
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        if(!s.running)break;
        const auto begin=std::chrono::steady_clock::now();const double dt=std::chrono::duration<double>(begin-previous).count();previous=begin;
#if defined(AY_LIGHTSHOW_AUDIO)
        if(s.music.loaded())s.music.tick(float(std::min(dt,.25)),s.player);
        else
#endif
            s.player.advance(frames?1.0/60:std::min(dt,.25));
        s.player.evaluate(s.frame);
        s.fog.noiseTime=float(s.player.time());s.renderer.setVolumetricSettings(s.fog);
        const uint16_t viewHeight=s.height-toolbarHeight;
        s.renderer.setViewportRect(0,toolbarHeight,s.width,viewHeight);
        const auto eye=s.cameraMode==3?math::FVector3(10,6,-13):s.cameraMode==1?math::FVector3(16,11,-23):math::FVector3(28,24,-38);
        const auto aim=s.cameraMode==3?math::FVector3(0,3,1):s.cameraMode==1?math::FVector3(0,3,0):s.cameraMode==2?math::FVector3(0,18,0):math::FVector3(0,10,0);
        s.renderer.setMainCameraLookAtPerspective(eye,aim,{0,1,0},55,float(s.width)/viewHeight,.1f,150);
        math::Float4x4 view,projection;s.renderer.mainCameraMatrices(view,projection);
        render::RenderScene scene;
        if(sceneMode!="air")scene.add(cube,material,math::translate(0.f,-.2f,0.f)*math::scale(24.f,.4f,24.f));
        if(sceneMode=="occluders")for(int i=0;i<5;++i)scene.add(cube,material,math::translate(float(i*3-6),1.5f,0)*math::scale(1.f,3.f,1.f));
        const auto presenterBegin=std::chrono::steady_clock::now();
        if(!s.presenter.append(s.renderer,scene,s.frame,view,projection,s.width,viewHeight,
            lightBudget,{.75f,true,pointBudget,!pointCpu},&s.error)){
            std::fprintf(stderr,"%s\n",s.error.c_str());s.running=false;
        }
        const auto presenterEnd=std::chrono::steady_clock::now();
        s.renderer.beginFrame({.005f,.006f,.012f,1});s.renderer.render(scene);
        if(!capture.empty()&&rendered==frames)s.renderer.captureScreenshot(capture);
        s.renderer.endFrame();
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        if(rendered>=30&&(!frames||rendered<frames)){
            timings.push_back(elapsed);
            presenterTimings.push_back(std::chrono::duration<double,std::milli>(presenterEnd-presenterBegin).count());
            renderTimings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-presenterEnd).count());
            const float gpuMs=s.renderer.getFrameStats().gpuFrameTimeMs;
            if(std::isfinite(gpuMs)&&gpuMs>0)gpuTimings.push_back(gpuMs);
        }
        if(rendered%15==0&&!s.hidden) {
            const auto stats=s.renderer.performanceLightStats();
            std::wstring title=L"Aliyat Light Show Studio | "+std::to_wstring(s.player.time())+L" s | lights "+std::to_wstring(stats.surfaceDraws)+L" / "+std::to_wstring(stats.sourceLights)+L" | beams "+std::to_wstring(stats.volumeDraws)+L" | points "+std::to_wstring(s.presenter.pointStats().visible);
#if defined(AY_LIGHTSHOW_AUDIO)
            if(s.music.loaded())title+=L" | music "+std::filesystem::path(s.music.path).filename().wstring();
#endif
            if(!s.error.empty())title+=L" | "+std::filesystem::path(s.error).wstring();SetWindowTextW(window,title.c_str());
            SendMessageW(s.time,TBM_SETPOS,TRUE,LPARAM(s.player.time()/s.show->data().duration*1000));
            if(s.timeline&&!s.hidden)InvalidateRect(s.timeline,nullptr,FALSE);
        }
        ++rendered;
    }
    if(!timings.empty()) {
        std::sort(timings.begin(),timings.end());double sum=0;for(double t:timings)sum+=t;
        std::sort(gpuTimings.begin(),gpuTimings.end());double gpuSum=0;for(double t:gpuTimings)gpuSum+=t;
        double presenterSum=0,renderSum=0;
        for(double t:presenterTimings)presenterSum+=t;
        for(double t:renderTimings)renderSum+=t;
        const double gpuMean=gpuTimings.empty()?0:gpuSum/gpuTimings.size();
        const double gpuP95=gpuTimings.empty()?0:gpuTimings[size_t((gpuTimings.size()-1)*.95)];
        const auto stats=s.renderer.performanceLightStats();
        const auto pointStats=s.presenter.pointStats();
        const auto aggregate=s.presenter.pointLightingStats();
        double musicTime=0;bool musicLoaded=false;
#if defined(AY_LIGHTSHOW_AUDIO)
        musicLoaded=s.music.loaded();
        if(musicLoaded)musicTime=double(s.music.engine.voicePositionFrames(s.music.voice))/s.music.engine.outputSampleRate();
#endif
        std::printf("{\"samples\":%zu,\"gpuSamples\":%zu,\"width\":%u,\"height\":%u,\"meanFrameMs\":%.4f,\"p95FrameMs\":%.4f,\"meanGpuMs\":%.4f,\"p95GpuMs\":%.4f,\"meanPresenterMs\":%.4f,\"meanRenderMs\":%.4f,\"surfaceDraws\":%u,\"volumeDraws\":%u,\"volumeBudgetSkipped\":%u,\"sourcePoints\":%u,\"visiblePoints\":%u,\"pointBudgetSkipped\":%u,\"instancedPoints\":%s,\"showTimeSeconds\":%.4f,\"musicTimeSeconds\":%.4f,\"musicLoaded\":%s,\"pointLightCells\":%u,\"pointLights\":%u,\"pointLightBudgetSkipped\":%u}\n",
            timings.size(),gpuTimings.size(),s.width,s.height-toolbarHeight,
            sum/timings.size(),timings[size_t((timings.size()-1)*.95)],gpuMean,gpuP95,
            presenterSum/presenterTimings.size(),renderSum/renderTimings.size(),
            stats.surfaceDraws,stats.volumeDraws,stats.volumeBudgetSkipped,
            pointStats.source,pointStats.visible,pointStats.budgetSkipped,
            pointStats.instanced?"true":"false",s.player.time(),musicTime,musicLoaded?"true":"false",aggregate.occupiedCells,aggregate.selectedLights,aggregate.budgetSkipped);
    }
    s.presenter.clearGoboTextures();
    for(auto& [name,texture]:s.goboTextures)s.renderer.destroyTexture(texture);
    s.renderer.shutdown();s.initialized=false;DestroyWindow(window);return s.error.empty()?0:5;
}
