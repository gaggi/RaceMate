// Uses NanoSVG's bounded, in-memory XML tokenizer. No scripts, external entities,
// fonts or linked assets are resolved. Unsupported text is optional map content.
struct LabelStyle {
    double size=36;
    int anchor=0;
    bool visible=true;
};
struct LabelParser {
    struct Node {
        float matrix[6]{1,0,0,1,0,0};
        double x=0,y=0;
        LabelStyle style;
        bool text=false,css=false,skip=false;
        std::string tag;
    };
    std::vector<Node> stack;
    std::map<std::string,std::string> styles;
    std::vector<Label> labels;
    double width=0,height=0,originX=0,originY=0;
    int overflow=0;
    bool invalid=false;
};
std::string LabelTrim(std::string_view s) {
    const auto a=s.find_first_not_of(" \t\r\n");
    return a==s.npos?std::string{}:std::string(s.substr(a,s.find_last_not_of(" \t\r\n")-a+1));
}
double LabelNumber(const char* s,double fallback) {
    char* end=nullptr; const double n=std::strtod(s,&end);
    return end!=s && std::isfinite(n) && std::abs(n)<=1e6?n:fallback;
}
void ApplyLabelStyle(LabelStyle& style,std::string_view css) {
    while(!css.empty()) {
        const auto end=css.find(';'); const auto item=css.substr(0,end); const auto colon=item.find(':');
        if(colon!=item.npos) {
            const auto key=LabelTrim(item.substr(0,colon)),value=LabelTrim(item.substr(colon+1));
            if(key=="font-size") style.size=LabelNumber(value.c_str(),style.size);
            if(key=="text-anchor") style.anchor=value=="middle"?1:(value=="end"?2:0);
            if((key=="display" && value=="none") || (key=="visibility" && (value=="hidden" || value=="collapse"))) style.visible=false;
        }
        if(end==css.npos) break;
        css.remove_prefix(end+1);
    }
}
std::string LabelEntities(std::string_view s) {
    std::string result;
    for(size_t i=0;i<s.size() && result.size()<512;++i) {
        if(s[i]!='&') { result+=s[i]; continue; }
        const auto end=s.find(';',i+1); if(end==s.npos || end-i>16) { result+='&'; continue; }
        const auto entity=s.substr(i+1,end-i-1); unsigned code=0;
        if(entity=="amp") code='&'; else if(entity=="lt") code='<'; else if(entity=="gt") code='>';
        else if(entity=="quot") code='"'; else if(entity=="apos") code='\'';
        else if(!entity.empty() && entity[0]=='#') {
            const std::string digits(entity.substr(entity.size()>1 && entity[1]=='x'?2:1)); char* tail=nullptr;
            const auto value=std::strtoul(digits.c_str(),&tail,entity.size()>1 && entity[1]=='x'?16:10);
            if(tail!=digits.c_str() && !*tail && value<=0x10ffff) code=static_cast<unsigned>(value);
        }
        if(!code || (code>=0xd800 && code<=0xdfff)) { result+='?'; i=end; continue; }
        if(code<0x80) result+=static_cast<char>(code);
        else if(code<0x800) { result+=static_cast<char>(0xc0|(code>>6)); result+=static_cast<char>(0x80|(code&63)); }
        else if(code<0x10000) { result+=static_cast<char>(0xe0|(code>>12)); result+=static_cast<char>(0x80|((code>>6)&63)); result+=static_cast<char>(0x80|(code&63)); }
        else { result+=static_cast<char>(0xf0|(code>>18)); result+=static_cast<char>(0x80|((code>>12)&63)); result+=static_cast<char>(0x80|((code>>6)&63)); result+=static_cast<char>(0x80|(code&63)); }
        i=end;
    }
    return result;
}
void LabelStart(void* user,const char* tag,const char** attributes) {
    auto& p=*static_cast<LabelParser*>(user);
    if(p.overflow || p.stack.size()>=64) { ++p.overflow; p.invalid=true; return; }
    LabelParser::Node n=p.stack.empty()?LabelParser::Node{}:p.stack.back();
    n.tag=tag; n.css=std::strcmp(tag,"style")==0;
    n.text=std::strcmp(tag,"text")==0 || (n.text && std::strcmp(tag,"tspan")==0);
    if(std::strcmp(tag,"defs")==0 || std::strcmp(tag,"clipPath")==0 || std::strcmp(tag,"script")==0) n.skip=true;
    // Apply class declarations first, then presentation attributes and inline style.
    for(int i=0;attributes[i];i+=2) if(std::strcmp(attributes[i],"class")==0) {
        std::istringstream classes(attributes[i+1]); std::string name;
        while(classes>>name) if(const auto found=p.styles.find(name);found!=p.styles.end()) ApplyLabelStyle(n.style,found->second);
    }
    std::string inlineStyle;
    for(int i=0;attributes[i];i+=2) {
        const std::string_view key=attributes[i]; const char* value=attributes[i+1];
        if(key=="transform") { float matrix[6]; nsvg__parseTransform(matrix,value); nsvg__xformPremultiply(n.matrix,matrix); }
        if(key=="x") n.x=LabelNumber(value,n.x);
        if(key=="y") n.y=LabelNumber(value,n.y);
        if(key=="dx") n.x+=LabelNumber(value,0);
        if(key=="dy") n.y+=LabelNumber(value,0);
        if(key=="font-size" || key=="text-anchor" || key=="display" || key=="visibility") ApplyLabelStyle(n.style,std::string(key)+":"+value);
        if(key=="style") inlineStyle=value;
        if(std::strcmp(tag,"svg")==0 && key=="viewBox") {
            std::istringstream numbers(value); numbers>>p.originX>>p.originY>>p.width>>p.height;
        }
    }
    ApplyLabelStyle(n.style,inlineStyle); p.stack.push_back(std::move(n));
}
void LabelEnd(void* user,const char* tag) {
    auto& p=*static_cast<LabelParser*>(user);
    if(p.overflow) { --p.overflow; return; }
    if(p.stack.empty() || p.stack.back().tag!=tag) { p.invalid=true; return; }
    const auto n=p.stack.back(); p.stack.pop_back();
    if(n.tag=="tspan" && !p.stack.empty()) { p.stack.back().x=n.x; p.stack.back().y=n.y; }
}
void LabelContent(void* user,const char* content) {
    auto& p=*static_cast<LabelParser*>(user);
    if(p.overflow || p.stack.empty()) return;
    auto& n=p.stack.back();
    if(n.css) {
        std::string_view css(content);
        while(p.styles.size()<128) {
            const auto open=css.find('{'),close=css.find('}');
            if(open==css.npos || close==css.npos || close<open) break;
            const auto selector=LabelTrim(css.substr(0,open));
            if(selector.size()>1 && selector[0]=='.') p.styles[selector.substr(1)]=std::string(css.substr(open+1,close-open-1));
            css.remove_prefix(close+1);
        }
        return;
    }
    if(!n.text || n.skip || !n.style.visible || p.labels.size()>=256) return;
    const auto text=LabelEntities(LabelTrim(content)); if(text.empty()) return;
    float x=0,y=0; nsvg__xformPoint(&x,&y,static_cast<float>(n.x),static_cast<float>(n.y),n.matrix);
    const double size=n.style.size*std::hypot(n.matrix[2],n.matrix[3]);
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) || size<1 || size>500 || std::abs(x)>1e5 || std::abs(y)>1e5) return;
    const bool number=std::all_of(text.begin(),text.end(),[](char c){return c>='0' && c<='9';});
    p.labels.push_back({{x-p.originX,y-p.originY},text,size,std::atan2(n.matrix[1],n.matrix[0]),n.style.anchor,number});
    // Preserve inline tspan continuation; official layers normally position each text explicitly.
    n.x+=text.size()*n.style.size*.55;
}
std::vector<Label> ParseLabels(std::string svg,const Layer& viewport) {
    if(svg.empty() || svg.size()>MaxSvgBytes || svg.find("<svg")==svg.npos) return {};
    LabelParser parser;
    nsvg__parseXML(svg.data(),LabelStart,LabelEnd,LabelContent,&parser);
    if(parser.invalid || !parser.stack.empty() || std::abs(parser.width-viewport.width)>.1 || std::abs(parser.height-viewport.height)>.1) return {};
    return std::move(parser.labels);
}
