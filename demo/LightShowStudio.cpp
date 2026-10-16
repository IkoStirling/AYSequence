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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <numbers>
#include <set>

using namespace ayt;
namespace {
constexpr int toolbarHeight=84;
enum Command { Play=100,Restart,Open,Save,Undo,Redo,AddKey,Circle,Sphere,Palette,Target,Property,Value,Time,ViewBoth,ViewStage,ViewSky };
struct Studio {
    render::Renderer renderer;
    sequence::LightShowPlayer player;
    sequence::LightShowPresenter presenter;
    sequence::LightShowFrame frame;
    std::shared_ptr<const sequence::CompiledLightShow> show;
    std::deque<std::shared_ptr<const sequence::CompiledLightShow>> undo,redo;
    std::vector<std::vector<size_t>> targetMembers;
    int cameraMode=0;
    HWND window=nullptr,target=nullptr,property=nullptr,value=nullptr,time=nullptr;
    bool running=true,initialized=false,hidden=false;
    uint16_t width=1280,height=800;
    std::string path,error;
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
    void install(std::shared_ptr<const sequence::CompiledLightShow> next,bool keepHistory) {
        if(!next)return;
        if(keepHistory){undo.push_back(show);redo.clear();while(undo.size()>8)undo.pop_front();}
        const auto t=player.time();const bool playing=player.playing();show=std::move(next);player.bind(show);
        player.seek(std::min(t,show->data().duration));if(playing)player.play();targets();
    }
    void edit(sequence::LightShow data) {
        auto next=sequence::CompiledLightShow::compile(std::move(data),&error);
        if(next)install(std::move(next),true);
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
                case 3:state.outerAngle=.02f+v;state.innerAngle=std::min(state.innerAngle,state.outerAngle*.75f);break;
                case 4:state.goboRotation=v*float(2*std::numbers::pi);break;
                case 5:state.color[0]=v;break;case 6:state.color[1]=v;break;case 7:state.color[2]=v;break;
                case 8:state.gobo=static_cast<sequence::ShowGobo>(std::min(4,int(v*5)));break;
                case 9:track.strobeFrequency=v*20;break;
                case 10:state.volumetric=v*20;break;
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
        if(save){if(sequence::saveLightShow(selected,show->data(),&error))path=selected;}
        else {sequence::LightShow data;if(sequence::loadLightShow(selected,data,&error)){install(sequence::CompiledLightShow::compile(std::move(data)),true);path=selected;}}
    }
    void command(int id) {
        if(id==ViewBoth)cameraMode=0;if(id==ViewStage)cameraMode=1;if(id==ViewSky)cameraMode=2;
        if(id==Play){if(player.playing())player.pause();else player.play();}
        if(id==Restart){player.stop();player.play();}
        if(id==Open)file(false);if(id==Save)file(true);if(id==AddKey)key();
        if(id==Circle)formation(false);if(id==Sphere)formation(true);
        if(id==Palette){auto data=show->data();for(auto& key:data.formations)for(auto& p:key.points)std::rotate(p.color.begin(),p.color.begin()+1,p.color.end());edit(std::move(data));}
        if(id==Undo&&!undo.empty()){redo.push_back(show);auto next=undo.back();undo.pop_back();install(std::move(next),false);}
        if(id==Redo&&!redo.empty()){undo.push_back(show);auto next=redo.back();redo.pop_back();install(std::move(next),false);}
    }
};
HWND control(HWND parent,const wchar_t* type,const wchar_t* title,int id,int x,int y,int w,int h,DWORD style=0) {
    return CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* s=reinterpret_cast<Studio*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){s=static_cast<Studio*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));s->window=window;}
    if(!s)return DefWindowProcW(window,message,w,l);
    if(message==WM_CLOSE){s->running=false;return 0;}
    if(message==WM_COMMAND){s->command(LOWORD(w));return 0;}
    if(message==WM_SIZE){s->width=static_cast<uint16_t>(std::max(1,int(LOWORD(l))));s->height=static_cast<uint16_t>(std::max(toolbarHeight+1,int(HIWORD(l))));if(s->initialized)s->renderer.resize(s->width,s->height);return 0;}
    if(message==WM_HSCROLL&&reinterpret_cast<HWND>(l)==s->time){s->player.pause();s->player.seek(double(SendMessageW(s->time,TBM_GETPOS,0,0))/1000*s->show->data().duration);return 0;}
    return DefWindowProcW(window,message,w,l);
}
}
int main(int argc,char** argv) {
    uint32_t fixtures=32,points=1000,frames=0;double startTime=0;std::string capture;render::Backend backend=render::Backend::Direct3D11;Studio s;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--hidden")s.hidden=true;
        else if(arg=="--frames"&&i+1<argc)frames=std::stoul(argv[++i]);
        else if(arg=="--points"&&i+1<argc)points=std::stoul(argv[++i]);
        else if(arg=="--fixtures"&&i+1<argc)fixtures=std::stoul(argv[++i]);
        else if(arg=="--time"&&i+1<argc)startTime=std::stod(argv[++i]);
        else if(arg=="--view"&&i+1<argc){const std::string view=argv[++i];s.cameraMode=view=="stage"?1:view=="sky"?2:0;}
        else if(arg=="--capture"&&i+1<argc)capture=argv[++i];
        else if(arg=="--backend"&&i+1<argc)backend=std::string(argv[++i])=="d3d12"?render::Backend::Direct3D12:render::Backend::Direct3D11;
        else s.path=arg;
    }
    sequence::LightShow data=sequence::makeLightShowExample(fixtures,points);
    if(!s.path.empty()&&!sequence::loadLightShow(s.path,data,&s.error)){std::fprintf(stderr,"%s\n",s.error.c_str());return 1;}
    s.show=sequence::CompiledLightShow::compile(std::move(data),&s.error);if(!s.show)return 2;
    s.player.bind(s.show);if(!s.player.seek(startTime)){std::fprintf(stderr,"Invalid --time\n");return 2;}s.player.play();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AliyatLightShowStudio";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
    const HWND window=CreateWindowExW(0,wc.lpszClassName,L"Aliyat Light Show Studio",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1300,860,nullptr,nullptr,wc.hInstance,&s);
    if(!window)return 3;
    int x=8;for(const auto& [label,id]:{std::pair{L"Play / Pause",Play},{L"Restart",Restart},{L"Open",Open},{L"Save As",Save},{L"Undo",Undo},{L"Redo",Redo},{L"Circle key",Circle},{L"Sphere key",Sphere},{L"Palette",Palette}}){control(window,L"BUTTON",label,id,x,6,100,26);x+=104;}
    s.time=control(window,TRACKBAR_CLASSW,L"",Time,950,4,280,28,TBS_NOTICKS);SendMessageW(s.time,TBM_SETRANGE,TRUE,MAKELPARAM(0,1000));
    s.target=control(window,L"COMBOBOX",L"",Target,8,42,170,350,CBS_DROPDOWNLIST|WS_VSCROLL);
    s.property=control(window,L"COMBOBOX",L"",Property,188,42,170,350,CBS_DROPDOWNLIST);
    for(const auto* name:{L"Intensity",L"Pan",L"Tilt",L"Beam angle",L"Pattern rotation",L"Red",L"Green",L"Blue",L"Pattern",L"Strobe Hz",L"Volume strength"})SendMessageW(s.property,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
    SendMessageW(s.property,CB_SETCURSEL,0,0);s.targets();
    s.value=control(window,TRACKBAR_CLASSW,L"",Value,370,39,300,30,TBS_NOTICKS);SendMessageW(s.value,TBM_SETRANGE,TRUE,MAKELPARAM(0,1000));SendMessageW(s.value,TBM_SETPOS,TRUE,500);
    control(window,L"BUTTON",L"Add / update key",AddKey,680,41,150,27);
    control(window,L"BUTTON",L"Both",ViewBoth,846,41,100,27);control(window,L"BUTTON",L"Stage",ViewStage,954,41,100,27);control(window,L"BUTTON",L"Sky",ViewSky,1062,41,100,27);
    if(!s.hidden)ShowWindow(window,SW_SHOW);
    render::InitDesc init;init.windowHandle=window;init.width=s.width;init.height=s.height;init.vsync=false;init.msaa=0;init.backend=backend;
    if(!s.renderer.initialize(init)){DestroyWindow(window);return 4;}s.initialized=true;
    s.renderer.configurePipeline(render::RenderPipelineDesc::makeDeferred());s.renderer.setAmbientStrength(.08f);
    s.renderer.setPostProcessBloomStrength(.3f);s.renderer.setPostProcessBloomThreshold(1);s.renderer.setPostProcessExposure(1.2f);
    render::VolumetricSettings fog;fog.enabled=true;fog.density=.025f;fog.steps=16;fog.maxDistance=100;fog.anisotropy=.2f;s.renderer.setVolumetricSettings(fog);
    const auto cube=s.renderer.createTexturedUnitCube();const auto material=s.renderer.createMaterialFromPhoskia(render::kPbrPhoskiaSource,"light_show_stage_pbr");
    s.renderer.setMaterialColor(material,"baseColor",.3f,.32f,.36f,1);s.renderer.setMaterialFloat(material,"roughness",.65f);
    auto previous=std::chrono::steady_clock::now();std::vector<double> timings;uint32_t rendered=0;
    while(s.running&&(!frames||rendered<frames+8)) {
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        if(!s.running)break;
        const auto begin=std::chrono::steady_clock::now();const double dt=std::chrono::duration<double>(begin-previous).count();previous=begin;
        s.player.advance(frames?1.0/60:std::min(dt,.25));s.player.evaluate(s.frame);
        const uint16_t viewHeight=s.height-toolbarHeight;
        s.renderer.setViewportRect(0,toolbarHeight,s.width,viewHeight);
        const auto eye=s.cameraMode==1?math::FVector3(16,11,-23):math::FVector3(28,24,-38);
        const auto aim=s.cameraMode==1?math::FVector3(0,3,0):s.cameraMode==2?math::FVector3(0,18,0):math::FVector3(0,10,0);
        s.renderer.setMainCameraLookAtPerspective(eye,aim,{0,1,0},55,float(s.width)/viewHeight,.1f,150);
        math::Float4x4 view,projection;s.renderer.mainCameraMatrices(view,projection);
        render::RenderScene scene;
        scene.add(cube,material,math::translate(0.f,-.2f,0.f)*math::scale(24.f,.4f,24.f));
        for(int i=0;i<5;++i)scene.add(cube,material,math::translate(float(i*3-6),1.5f,0)*math::scale(1.f,3.f,1.f));
        if(!s.presenter.append(s.renderer,scene,s.frame,view,projection,s.width,viewHeight,{64,32,4},{.75f,true,65536},&s.error)){
            std::fprintf(stderr,"%s\n",s.error.c_str());s.running=false;
        }
        s.renderer.beginFrame({.005f,.006f,.012f,1});s.renderer.render(scene);
        if(!capture.empty()&&rendered==frames)s.renderer.captureScreenshot(capture);
        s.renderer.endFrame();
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        if(rendered>=30&&(!frames||rendered<frames))timings.push_back(elapsed);
        if(rendered%15==0&&!s.hidden) {
            const auto stats=s.renderer.performanceLightStats();
            std::wstring title=L"Aliyat Light Show Studio | "+std::to_wstring(s.player.time())+L" s | lights "+std::to_wstring(stats.surfaceDraws)+L" / "+std::to_wstring(stats.sourceLights)+L" | beams "+std::to_wstring(stats.volumeDraws)+L" | points "+std::to_wstring(s.presenter.pointStats().visible);
            if(!s.error.empty())title+=L" | "+std::filesystem::path(s.error).wstring();SetWindowTextW(window,title.c_str());
            SendMessageW(s.time,TBM_SETPOS,TRUE,LPARAM(s.player.time()/s.show->data().duration*1000));
        }
        ++rendered;
    }
    if(!timings.empty()) {
        std::sort(timings.begin(),timings.end());double sum=0;for(double t:timings)sum+=t;
        const auto stats=s.renderer.performanceLightStats();
        std::printf("{\"samples\":%zu,\"width\":%u,\"height\":%u,\"meanFrameMs\":%.4f,\"p95FrameMs\":%.4f,\"surfaceDraws\":%u,\"volumeDraws\":%u,\"visiblePoints\":%u}\n",timings.size(),s.width,s.height-toolbarHeight,sum/timings.size(),timings[size_t((timings.size()-1)*.95)],stats.surfaceDraws,stats.volumeDraws,s.presenter.pointStats().visible);
    }
    s.renderer.shutdown();s.initialized=false;DestroyWindow(window);return s.error.empty()?0:5;
}
