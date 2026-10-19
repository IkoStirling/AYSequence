// Windows Studio authoring panel; shares Core transactional key/grid operations.
enum FogPanelCommand {FogNew=3100,FogClear,FogGridSize,FogCreateGrid,FogProperty,FogNumber,FogApplyKey,FogLayer,FogBrushValue,FogRadius,FogStrength,FogVolume,FogFill,FogUndo,FogRedo,FogReadCurrent};
struct FogPanelState {
    Studio* owner=nullptr;HWND window=nullptr,property=nullptr,number=nullptr,layer=nullptr,layerLabel=nullptr,gridSize=nullptr,brushValue=nullptr,radius=nullptr,strength=nullptr,volume=nullptr,status=nullptr,timeLabel=nullptr;
    bool painting=false,changed=false,owned=false;std::optional<sequence::ShowFogGrid> draft;
    std::shared_ptr<const sequence::CompiledLightShow> strokeSource,seen;
    RECT canvas() const {RECT client{};GetClientRect(window,&client);const int size=std::max(1,std::min(int(client.right)-32,int(client.bottom)-220));return {16,160,16+size,160+size};}
    const sequence::ShowFogGrid& grid() const {return draft?*draft:owner->show->data().fog.grid;}
    void message(const std::string& text){SetWindowTextA(status,text.c_str());}
    static bool numeric(HWND edit,double& value){wchar_t text[80]{};GetWindowTextW(edit,text,80);wchar_t* end=nullptr;value=std::wcstod(text,&end);return end!=text&&*end==0&&std::isfinite(value);}
    sequence::ShowFogState current() const {
        sequence::LightShowFrame frame;if(owner->show->evaluate(owner->player.time(),frame,false)&&frame.fog.authored)return frame.fog.value;
        const auto& s=owner->fog;sequence::ShowFogState result;result.enabled=s.enabled;result.local=s.local;result.density=s.density;result.scattering=s.scattering;result.anisotropy=s.anisotropy;result.maxDistance=s.maxDistance;result.noiseStrength=s.noiseStrength;result.noiseScale=s.noiseScale;
        for(int i=0;i<3;++i){result.center[i]=s.center[i];result.halfExtent[i]=s.halfExtent[i];result.tint[i]=s.tint[i];}return result;
    }
    void loadNumber(){
        const auto v=current();const int selected=int(SendMessageW(property,CB_GETCURSEL,0,0));double value=0;
        if(selected==0)value=v.density;else if(selected==1)value=v.scattering;else if(selected==2)value=v.anisotropy;else if(selected==3)value=v.maxDistance;
        else if(selected>=4&&selected<=6)value=v.center[selected-4];else if(selected>=7&&selected<=9)value=v.halfExtent[selected-7];else if(selected>=10&&selected<=12)value=v.tint[selected-10];
        else if(selected==13)value=v.noiseStrength;else if(selected==14)value=v.noiseScale;else if(selected==15)value=v.enabled?1:0;else if(selected==16)value=v.local?1:0;
        wchar_t text[80]{};swprintf_s(text,L"%.6g",value);SetWindowTextW(number,text);
    }
    void refresh(bool values=true){
        if(!owner->show)return;seen=owner->show;const auto& g=grid();const int previous=int(SendMessageW(layer,TBM_GETPOS,0,0));
        SendMessageW(layer,TBM_SETRANGE,TRUE,MAKELPARAM(0,g.depth?g.depth-1:0));SendMessageW(layer,TBM_SETPOS,TRUE,std::min(previous,g.depth?int(g.depth)-1:0));
        wchar_t text[96]{};swprintf_s(text,L"Z %d / %u",int(SendMessageW(layer,TBM_GETPOS,0,0)),g.depth?unsigned(g.depth)-1:0);SetWindowTextW(layerLabel,text);
        swprintf_s(text,L"Time %.3f s",owner->player.time());SetWindowTextW(timeLabel,text);if(values)loadNumber();InvalidateRect(window,nullptr,FALSE);
    }
    bool commit(sequence::LightShow data){const auto before=owner->show;owner->error.clear();owner->edit(std::move(data));if(!owner->error.empty()){message(owner->error);return false;}refresh();return owner->show!=before;}
    static void ensureTrack(sequence::LightShow& data,const sequence::ShowFogState& state){if(data.fog.keys.empty())data.fog.keys.push_back({0,state,sequence::ShowInterpolation::Linear});}
    void command(int id){
        if(id==FogProperty||id==FogReadCurrent){loadNumber();return;}
        if(id==FogUndo||id==FogRedo){owner->command(id==FogUndo?Undo:Redo);refresh();return;}
        if(id==FogNew){auto data=owner->show->data();if(!data.fog.keys.empty()){message("Fog track already exists.");return;}ensureTrack(data,current());if(commit(std::move(data)))message("Fog track created. Add keys at the current show time.");return;}
        if(id==FogClear){auto data=owner->show->data();data.fog={};if(commit(std::move(data)))message("Fog track cleared; host medium controls are active.");return;}
        if(id==FogCreateGrid){
            auto data=owner->show->data();ensureTrack(data,current());for(auto& key:data.fog.keys)key.value.local=true;
            const auto selected=SendMessageW(gridSize,CB_GETCURSEL,0,0);const uint8_t size=selected==2?64:selected==1?32:16;data.fog.grid={size,size,size,std::vector<uint8_t>(size_t(size)*size*size,255)};
            if(commit(std::move(data))){SendMessageW(layer,TBM_SETPOS,TRUE,size/2);refresh(false);message("Local density grid created at full density. Paint0 to remove fog,255 to add it.");}return;
        }
        if(id==FogFill){
            double value;if(!numeric(brushValue,value)||value<0||value>255||std::floor(value)!=value){message("Density value must be an integer0..255.");return;}
            auto data=owner->show->data();if(data.fog.grid.values.empty()){message("Create a local grid first.");return;}std::fill(data.fog.grid.values.begin(),data.fog.grid.values.end(),uint8_t(value));
            if(commit(std::move(data)))message("All layers filled; one Undo restores the previous grid.");return;
        }
        if(id==FogApplyKey){
            double value;if(!numeric(number,value)){message("Enter a finite numeric value.");return;}
            auto data=owner->show->data();auto state=current();const int selected=int(SendMessageW(property,CB_GETCURSEL,0,0));
            if(selected==0)state.density=float(value);else if(selected==1)state.scattering=float(value);else if(selected==2)state.anisotropy=float(value);else if(selected==3)state.maxDistance=float(value);
            else if(selected>=4&&selected<=6)state.center[selected-4]=float(value);else if(selected>=7&&selected<=9)state.halfExtent[selected-7]=float(value);else if(selected>=10&&selected<=12)state.tint[selected-10]=float(value);
            else if(selected==13)state.noiseStrength=float(value);else if(selected==14)state.noiseScale=float(value);else if(selected==15||selected==16){if(value!=0&&value!=1){message("Enabled/Local expects0 or1.");return;}if(selected==15)state.enabled=value==1;else state.local=value==1;}
            ensureTrack(data,current());const double time=owner->player.time();auto key=sequence::ShowFogKey{time,state,sequence::ShowInterpolation::Linear};
            const auto selectedMode=SendMessageW(owner->keyInterpolation,CB_GETCURSEL,0,0);if(selectedMode>=0&&selectedMode<=2)key.interpolation=sequence::ShowInterpolation(selectedMode);
            auto it=std::find_if(data.fog.keys.begin(),data.fog.keys.end(),[&](const auto& k){return std::abs(k.time-time)<=1e-6;});if(it!=data.fog.keys.end())*it=key;else data.fog.keys.insert(std::lower_bound(data.fog.keys.begin(),data.fog.keys.end(),time,[](const auto& k,double t){return k.time<t;}),key);
            owner->seekPreview(time);if(commit(std::move(data)))message("Physical fog key saved at the current time.");return;
        }
    }
    void stamp(int px,int py){
        if(!draft||draft->values.empty())return;const auto rect=canvas();if(px<rect.left||px>=rect.right||py<rect.top||py>=rect.bottom)return;
        double value,radiusValue,strengthValue;if(!numeric(brushValue,value)||!numeric(radius,radiusValue)||!numeric(strength,strengthValue)||value<0||value>255||std::floor(value)!=value||radiusValue<1||radiusValue>16||std::floor(radiusValue)!=radiusValue){message("Brush: value0..255, radius1..16, strength0..1.");return;}
        sequence::FogDensityBrush brush;brush.center={uint16_t((px-rect.left)*draft->width/(rect.right-rect.left)),uint16_t(draft->height-1-(py-rect.top)*draft->height/(rect.bottom-rect.top)),uint16_t(SendMessageW(layer,TBM_GETPOS,0,0))};brush.radius=uint16_t(radiusValue);brush.value=uint8_t(value);brush.strength=float(strengthValue);brush.volume=SendMessageW(volume,BM_GETCHECK,0,0)==BST_CHECKED;
        sequence::ShowFogGrid edited;std::string error;if(!sequence::paintFogDensityGrid(*draft,brush,edited,&error)){message(error);return;}changed|=edited.values!=draft->values;draft=std::move(edited);InvalidateRect(window,nullptr,FALSE);
    }
};
LRESULT CALLBACK fogPanelProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* state=reinterpret_cast<FogPanelState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){state=static_cast<FogPanelState*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);state->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
    if(!state)return DefWindowProcW(window,message,w,l);
    if(message==WM_COMMAND){state->command(LOWORD(w));return 0;}
    if(message==WM_HSCROLL){state->refresh(false);return 0;}
    if(message==WM_TIMER){wchar_t text[80]{};swprintf_s(text,L"Time %.3f s",state->owner->player.time());SetWindowTextW(state->timeLabel,text);if(!state->painting&&state->owner->show!=state->seen)state->refresh(GetFocus()!=state->number);return 0;}
    if(message==WM_SIZE){InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_GETMINMAXINFO){auto* size=reinterpret_cast<MINMAXINFO*>(l);size->ptMinTrackSize={590,600};return 0;}
    if(message==WM_LBUTTONDOWN){
        const auto rect=state->canvas();POINT point{int(short(LOWORD(l))),int(short(HIWORD(l)))};if(!PtInRect(&rect,point))return 0;
        if(state->grid().values.empty()){state->message("Create a local density grid first.");return 0;}
        state->draft=state->owner->show->data().fog.grid;state->strokeSource=state->owner->show;state->painting=true;state->changed=false;SetCapture(window);state->stamp(point.x,point.y);return 0;
    }
    if(message==WM_MOUSEMOVE&&state->painting){state->stamp(int(short(LOWORD(l))),int(short(HIWORD(l))));return 0;}
    if(message==WM_LBUTTONUP&&state->painting){
        state->painting=false;ReleaseCapture();if(state->changed&&state->owner->show==state->strokeSource){auto data=state->owner->show->data();data.fog.grid=std::move(*state->draft);state->draft.reset();if(state->commit(std::move(data)))state->message("Stroke committed. One Undo restores it.");}
        else {state->draft.reset();if(state->owner->show!=state->strokeSource)state->message("Show changed during the stroke; stroke discarded.");}
        state->strokeSource.reset();InvalidateRect(window,nullptr,FALSE);return 0;
    }
    if(message==WM_CAPTURECHANGED&&state->painting){state->painting=false;state->draft.reset();state->strokeSource.reset();InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_PAINT){
        PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);FillRect(dc,&client,GetSysColorBrush(COLOR_BTNFACE));const auto rect=state->canvas();SetDCBrushColor(dc,RGB(20,24,30));FillRect(dc,&rect,HBRUSH(GetStockObject(DC_BRUSH)));
        const auto& grid=state->grid();if(!grid.values.empty()){
            const int z=std::clamp(int(SendMessageW(state->layer,TBM_GETPOS,0,0)),0,int(grid.depth)-1);
            std::vector<uint32_t> pixels(size_t(grid.width)*grid.height);for(int y=0;y<grid.height;++y)for(int x=0;x<grid.width;++x){const auto value=uint32_t(grid.values[x+grid.width*((grid.height-1-y)+grid.height*z)]);pixels[x+grid.width*y]=value|(value<<8)|(value<<16);}
            BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=grid.width;bitmap.bmiHeader.biHeight=-int(grid.height);bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,0,0,grid.width,grid.height,pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
            if(grid.width<=32){SelectObject(dc,GetStockObject(DC_PEN));SetDCPenColor(dc,RGB(45,50,60));for(int x=0;x<=grid.width;++x){const int at=rect.left+x*(rect.right-rect.left)/grid.width;MoveToEx(dc,at,rect.top,nullptr);LineTo(dc,at,rect.bottom);}for(int y=0;y<=grid.height;++y){const int at=rect.top+y*(rect.bottom-rect.top)/grid.height;MoveToEx(dc,rect.left,at,nullptr);LineTo(dc,rect.right,at);}}
        }
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(30,35,45));const auto* tip=L"X right, Y up. Drag then release: one undo step.";TextOutW(dc,16,rect.bottom+7,tip,int(std::wcslen(tip)));const auto* legend=L"Black=clear air, white=full density multiplier.";TextOutW(dc,16,rect.bottom+25,legend,int(std::wcslen(legend)));EndPaint(window,&paint);return 0;
    }
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_NCDESTROY){state->owner->fogEditor=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);if(state->owned)delete state;return DefWindowProcW(window,message,w,l);}
    return DefWindowProcW(window,message,w,l);
}
void openFogPanel(Studio& studio){
    if(studio.fogEditor&&IsWindow(studio.fogEditor)){SetForegroundWindow(studio.fogEditor);return;}
    WNDCLASSW wc{};wc.lpfnWndProc=fogPanelProcedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AliyatLightShowFogAuthoring";wc.hCursor=LoadCursor(nullptr,IDC_CROSS);RegisterClassW(&wc);
    auto* state=new FogPanelState;state->owner=&studio;const int height=std::min(740,GetSystemMetrics(SM_CYSCREEN)-60);studio.fogEditor=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Fog keys and density layers",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,610,std::max(600,height),studio.window,nullptr,wc.hInstance,state);
    if(!studio.fogEditor){delete state;studio.error="Cannot create fog authoring panel";return;}
    state->owned=true;
    const auto window=studio.fogEditor;
    control(window,L"BUTTON",L"New fog",FogNew,12,10,78,25);control(window,L"BUTTON",L"Clear track",FogClear,96,10,90,25);
    state->gridSize=control(window,L"COMBOBOX",L"",FogGridSize,194,10,65,100,CBS_DROPDOWNLIST);for(const auto* text:{L"16",L"32",L"64"})SendMessageW(state->gridSize,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(state->gridSize,CB_SETCURSEL,0,0);control(window,L"BUTTON",L"Create local grid",FogCreateGrid,267,10,135,25);control(window,L"BUTTON",L"Undo",FogUndo,410,10,70,25);control(window,L"BUTTON",L"Redo",FogRedo,488,10,70,25);
    state->property=control(window,L"COMBOBOX",L"",FogProperty,12,44,160,350,CBS_DROPDOWNLIST);for(const auto* text:{L"Extinction density",L"Scattering albedo",L"Anisotropy",L"Maximum distance",L"Center X",L"Center Y",L"Center Z",L"Half extent X",L"Half extent Y",L"Half extent Z",L"Tint red",L"Tint green",L"Tint blue",L"Noise strength",L"Noise scale",L"Enabled (0/1)",L"Local (0/1)"})SendMessageW(state->property,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(state->property,CB_SETCURSEL,0,0);
    state->number=control(window,L"EDIT",L"0.025",FogNumber,180,44,100,25,WS_BORDER|ES_AUTOHSCROLL);control(window,L"BUTTON",L"Add / update key",FogApplyKey,288,44,135,25);state->timeLabel=control(window,L"STATIC",L"",0,435,48,130,22);
    state->layer=control(window,TRACKBAR_CLASSW,L"",FogLayer,12,78,220,27,TBS_NOTICKS);state->layerLabel=control(window,L"STATIC",L"Z0",0,236,82,70,23);control(window,L"STATIC",L"Value0..255",0,310,82,78,22);state->brushValue=control(window,L"EDIT",L"0",FogBrushValue,392,78,65,25,WS_BORDER|ES_AUTOHSCROLL);control(window,L"BUTTON",L"Fill all",FogFill,468,78,90,25);
    control(window,L"STATIC",L"Radius",0,12,116,45,22);state->radius=control(window,L"EDIT",L"2",FogRadius,62,112,50,25,WS_BORDER|ES_AUTOHSCROLL);control(window,L"STATIC",L"Strength",0,124,116,60,22);state->strength=control(window,L"EDIT",L"1",FogStrength,190,112,65,25,WS_BORDER|ES_AUTOHSCROLL);state->volume=control(window,L"BUTTON",L"3D brush",FogVolume,270,112,100,25,BS_AUTOCHECKBOX);control(window,L"BUTTON",L"Read current",FogReadCurrent,382,112,110,25);state->status=control(window,L"STATIC",L"Create a local grid, then paint a Z layer.",0,12,139,555,16);
    state->refresh();SetTimer(window,1,200,nullptr);if(!studio.hidden)ShowWindow(window,SW_SHOW);
}
bool fogPanelSmoke(Studio& studio){
    openFogPanel(studio);if(!studio.fogEditor)return false;auto* panel=reinterpret_cast<FogPanelState*>(GetWindowLongPtrW(studio.fogEditor,GWLP_USERDATA));
    SendMessageW(panel->window,WM_COMMAND,FogNew,0);SendMessageW(panel->window,WM_COMMAND,FogCreateGrid,0);if(studio.show->data().fog.grid.values.size()!=4096){studio.error="Fog grid UI creation failed";return false;}
    const auto before=studio.show;const auto rect=panel->canvas();const int x=(rect.left+rect.right)/2,y=(rect.top+rect.bottom)/2;SendMessageW(panel->window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(panel->window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+12,y));SendMessageW(panel->window,WM_LBUTTONUP,0,MAKELPARAM(x+12,y));
    const auto painted=studio.show;if(painted==before||studio.undo.empty()||studio.undo.back().show!=before||std::all_of(painted->data().fog.grid.values.begin(),painted->data().fog.grid.values.end(),[](auto value){return value==255;})){studio.error="Fog stroke did not create one undo snapshot";return false;}
    studio.command(Undo);if(studio.show!=before){studio.error="Fog undo failed";return false;}studio.command(Redo);if(studio.show!=painted){studio.error="Fog redo failed";return false;}
    studio.seekPreview(1);SetWindowTextW(panel->number,L"0.08");SendMessageW(panel->window,WM_COMMAND,FogApplyKey,0);if(studio.show->data().fog.keys.size()!=2||std::abs(studio.show->data().fog.keys.back().value.density-.08f)>1e-6f){studio.error="Fog physical key UI failed";return false;}
    std::string bytes;sequence::LightShow decoded;if(!sequence::encodeLightShow(studio.show->data(),bytes,&studio.error)||!sequence::decodeLightShow(bytes,decoded,&studio.error)||decoded.fog.grid.values!=studio.show->data().fog.grid.values){studio.error="Fog editor persistence failed";return false;}
    DestroyWindow(panel->window);std::fprintf(stderr,"[Fog editor] paint, undo, redo, physical key and persistence passed\n");return true;
}
