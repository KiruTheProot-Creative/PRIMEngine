// fbx_import.inl : FBX files (text and binary), included into meshmodel.cpp (it uses the Builder there).
// Reads the meshes with their place in the node tree, the materials' pictures (inside the file or next to it),
// and the animations (stacks of curves on nodes), so doors can slide and swing.

namespace fbx {

struct Prop {
    char type = 0;                         // 'I' integer, 'D' number, 'S' text, 'A' list of numbers, 'R' bytes
    int64_t i = 0; double d = 0; std::string s; std::vector<double> a; std::vector<uint8_t> raw;
    double num() const { return type == 'I' ? (double)i : d; }
    int64_t id() const { return type == 'I' ? i : (int64_t)d; }
};
struct Node {
    std::string name; std::vector<Prop> props; std::vector<Node> kids;
    const Node* child(const std::string& n) const { for (const Node& k : kids) if (k.name == n) return &k; return nullptr; }
    // a list of numbers: binary files keep it in the first property, text files in a child "a"
    const std::vector<double>* list(const std::string& n) const {
        const Node* c = child(n); if (!c) return nullptr;
        if (!c->props.empty() && c->props[0].type == 'A') return &c->props[0].a;
        const Node* a = c->child("a"); if (a && !a->props.empty() && a->props[0].type == 'A') return &a->props[0].a;
        return nullptr;
    }
    std::string str(const std::string& n) const { const Node* c = child(n); return c && !c->props.empty() ? c->props[0].s : ""; }
};

// ---- text FBX
struct Text {
    const char* p; const char* e; std::string err;
    void ws(bool newlines) {
        for (;;) {
            while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || (newlines && *p == '\n'))) p++;
            if (p < e && *p == ';') { while (p < e && *p != '\n') p++; continue; }
            return;
        }
    }
    bool value(Prop& v) {
        ws(false);
        if (p >= e) return false;
        if (*p == '"') {
            const char* q = ++p; while (p < e && *p != '"') p++;
            v.type = 'S'; v.s.assign(q, p); if (p < e) p++; return true;
        }
        if (*p == '*') {   // "*24": a list follows in { a: ... }
            p++; const char* q = p; while (p < e && *p >= '0' && *p <= '9') p++;
            v.type = 'I'; v.i = std::atoll(std::string(q, p).c_str()); v.s = "*"; return true;
        }
        if (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9')) {
            const char* q = p; bool real = false;
            while (p < e && (std::strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) { if (*p == '.' || *p == 'e' || *p == 'E') real = true; p++; }
            std::string t(q, p);
            if (real) { v.type = 'D'; v.d = std::atof(t.c_str()); } else { v.type = 'I'; v.i = std::atoll(t.c_str()); v.d = (double)v.i; }
            return true;
        }
        if (std::isalpha((unsigned char)*p) || *p == '_') {   // a bare word (Shading: Y)
            const char* q = p; while (p < e && (std::isalnum((unsigned char)*p) || *p == '_' || *p == '|')) p++;
            v.type = 'S'; v.s.assign(q, p); return true;
        }
        return false;
    }
    bool node(Node& n, int depth) {
        if (depth > 64) { err = "too deep"; return false; }
        ws(true);
        const char* q = p; while (p < e && *p != ':' && *p != '{' && *p != '}' && *p != '\n') p++;
        if (p >= e || *p != ':') { err = "expected a name"; return false; }
        n.name.assign(q, p); while (!n.name.empty() && n.name.back() == ' ') n.name.pop_back(); p++;
        // values, separated by commas (a comma at the end of a line continues on the next)
        bool numberList = n.name == "a";
        for (;;) {
            ws(false); if (p >= e || *p == '\n' || *p == '{' || *p == '}') break;
            Prop v; if (!value(v)) { err = "bad value in " + n.name; return false; }
            if (numberList && (v.type == 'I' || v.type == 'D')) {
                if (n.props.empty()) { Prop l; l.type = 'A'; n.props.push_back(l); }
                n.props[0].a.push_back(v.num());
            } else n.props.push_back(v);
            ws(false);
            if (p < e && *p == ',') { p++; ws(true); continue; }
            break;
        }
        ws(false);
        if (p < e && *p == '{') {
            p++;
            for (;;) {
                ws(true); if (p >= e) { err = "missing }"; return false; }
                if (*p == '}') { p++; break; }
                n.kids.emplace_back(); if (!node(n.kids.back(), depth + 1)) return false;
            }
        }
        return true;
    }
};

// ---- binary FBX
struct Bin {
    const uint8_t* b; size_t size; size_t pos = 0; bool wide = false; std::string err;
    template <class T> bool rd(T& v) { if (pos + sizeof(T) > size) return false; std::memcpy(&v, b + pos, sizeof(T)); pos += sizeof(T); return true; }
    bool prop(Prop& v) {
        char t; if (!rd(t)) return false;
        switch (t) {
        case 'Y': { int16_t x; if (!rd(x)) return false; v.type = 'I'; v.i = x; v.d = x; return true; }
        case 'C': { uint8_t x; if (!rd(x)) return false; v.type = 'I'; v.i = x; v.d = x; return true; }
        case 'I': { int32_t x; if (!rd(x)) return false; v.type = 'I'; v.i = x; v.d = x; return true; }
        case 'L': { int64_t x; if (!rd(x)) return false; v.type = 'I'; v.i = x; v.d = (double)x; return true; }
        case 'F': { float x; if (!rd(x)) return false; v.type = 'D'; v.d = x; return true; }
        case 'D': { double x; if (!rd(x)) return false; v.type = 'D'; v.d = x; return true; }
        case 'S': case 'R': {
            uint32_t n; if (!rd(n) || pos + n > size) return false;
            if (t == 'S') { v.type = 'S'; v.s.assign((const char*)b + pos, n); } else { v.type = 'R'; v.raw.assign(b + pos, b + pos + n); }
            pos += n; return true;
        }
        case 'f': case 'd': case 'l': case 'i': case 'b': {
            uint32_t count, enc, clen; if (!rd(count) || !rd(enc) || !rd(clen) || pos + clen > size) return false;
            int es = t == 'd' || t == 'l' ? 8 : t == 'b' ? 1 : 4;
            std::vector<uint8_t> data;
            if (enc == 1) {   // zlib
                int outLen = 0; char* out = ::stbi_zlib_decode_malloc_guesssize((const char*)b + pos, (int)clen, (int)(count * es), &outLen);
                if (!out) { err = "a packed list could not be unpacked"; return false; }
                data.assign((uint8_t*)out, (uint8_t*)out + outLen); ::stbi_image_free(out);
            } else data.assign(b + pos, b + pos + clen);
            pos += clen;
            if (data.size() < (size_t)count * es) return false;
            v.type = 'A'; v.a.resize(count);
            for (uint32_t k = 0; k < count; k++) {
                const uint8_t* q = &data[(size_t)k * es];
                if (t == 'f') { float x; std::memcpy(&x, q, 4); v.a[k] = x; }
                else if (t == 'd') { double x; std::memcpy(&x, q, 8); v.a[k] = x; }
                else if (t == 'l') { int64_t x; std::memcpy(&x, q, 8); v.a[k] = (double)x; }
                else if (t == 'i') { int32_t x; std::memcpy(&x, q, 4); v.a[k] = x; }
                else v.a[k] = *q;
            }
            return true;
        }
        default: err = "unknown value type"; return false;
        }
    }
    // false = error; done = the empty record that ends a list
    bool node(Node& n, bool& done, int depth) {
        if (depth > 64) return false;
        uint64_t end, np, plen; uint8_t nl;
        if (wide) { if (!rd(end) || !rd(np) || !rd(plen)) return false; }
        else { uint32_t a, c, d; if (!rd(a) || !rd(c) || !rd(d)) return false; end = a; np = c; plen = d; }
        if (!rd(nl)) return false;
        if (end == 0) { done = true; return true; }
        if (end > size || pos + nl > size) return false;
        n.name.assign((const char*)b + pos, nl); pos += nl;
        for (uint64_t k = 0; k < np; k++) { n.props.emplace_back(); if (!prop(n.props.back())) return false; }
        while (pos < end) {
            Node c; bool d = false; if (!node(c, d, depth + 1)) return false;
            if (d) break;
            n.kids.push_back(std::move(c));
        }
        pos = (size_t)end; return true;
    }
};

bool parse(const std::string& file, Node& root, std::string& err) {
    root = Node{}; root.name = "root";
    static const char magic[] = "Kaydara FBX Binary  ";
    if (file.size() > 27 && !std::memcmp(file.data(), magic, 20)) {
        Bin bn{ (const uint8_t*)file.data(), file.size() };
        uint32_t ver; std::memcpy(&ver, file.data() + 23, 4); bn.wide = ver >= 7500; bn.pos = 27;
        while (bn.pos < file.size()) {
            Node n; bool done = false;
            if (!bn.node(n, done, 0)) { err = bn.err.empty() ? "the binary FBX file is damaged" : bn.err; return false; }
            if (done) break;
            root.kids.push_back(std::move(n));
        }
        return true;
    }
    Text tx{ file.data(), file.data() + file.size() };
    for (;;) {
        tx.ws(true); if (tx.p >= tx.e) break;
        root.kids.emplace_back(); if (!tx.node(root.kids.back(), 0)) { err = tx.err; return false; }
    }
    return true;
}

std::string clean(const std::string& n) {   // "Model::door" (text) or "door\0\1Model" (binary) -> "door"
    size_t z = n.find(std::string("\x00\x01", 2)); if (z != std::string::npos) return n.substr(0, z);
    size_t c = n.find("::"); return c == std::string::npos ? n : n.substr(c + 2);
}

// a node's Properties70 values: name -> numbers / text
struct P70 {
    std::map<std::string, std::vector<double>> nums; std::map<std::string, std::string> strs;
    explicit P70(const Node* n) {
        const Node* p = n ? n->child("Properties70") : nullptr; if (!p) return;
        for (const Node& k : p->kids) {
            if (k.name != "P" || k.props.empty()) continue;
            std::vector<double> v; std::string s;
            for (size_t i = 4; i < k.props.size(); i++) { if (k.props[i].type == 'S') s = k.props[i].s; else v.push_back(k.props[i].num()); }
            nums[k.props[0].s] = v; strs[k.props[0].s] = s;
        }
    }
    V3 vec(const std::string& n, V3 def) const { auto it = nums.find(n); return it != nums.end() && it->second.size() >= 3 ? V3{ it->second[0], it->second[1], it->second[2] } : def; }
    double num(const std::string& n, double def) const { auto it = nums.find(n); return it != nums.end() && !it->second.empty() ? it->second[0] : def; }
};

using M4 = std::array<double, 16>;   // column-major
M4 ident() { return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; }
M4 mul(const M4& a, const M4& b) { M4 c{}; for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) { double s = 0; for (int q = 0; q < 4; q++) s += a[q * 4 + r] * b[k * 4 + q]; c[k * 4 + r] = s; } return c; }
M4 trans(V3 t) { M4 m = ident(); m[12] = t.x; m[13] = t.y; m[14] = t.z; return m; }
M4 scal(V3 s) { M4 m = ident(); m[0] = s.x; m[5] = s.y; m[10] = s.z; return m; }
M4 rot1(int axis, double deg) {
    double a = deg * 3.14159265358979 / 180, c = std::cos(a), s = std::sin(a); M4 m = ident();
    if (axis == 0) { m[5] = c; m[6] = s; m[9] = -s; m[10] = c; }
    else if (axis == 1) { m[0] = c; m[2] = -s; m[8] = s; m[10] = c; }
    else { m[0] = c; m[1] = s; m[4] = -s; m[5] = c; }
    return m;
}
M4 euler(V3 r, int order) {   // FBX orders: 0 XYZ, 1 XZY, 2 YZX, 3 YXZ, 4 ZXY, 5 ZYX (the first turns first)
    static const int seq[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 }, { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 } };
    const double d[3] = { r.x, r.y, r.z }; const int* q = seq[std::clamp(order, 0, 5)];
    return mul(rot1(q[2], d[q[2]]), mul(rot1(q[1], d[q[1]]), rot1(q[0], d[q[0]])));
}
M4 transpose3(const M4& m) { M4 t = ident(); for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) t[c * 4 + r] = m[r * 4 + c]; return t; }
V3 point(const M4& m, V3 p) { return { m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13], m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] }; }
V3 dir(const M4& m, V3 p) { V3 v{ m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z, m[2] * p.x + m[6] * p.y + m[10] * p.z }; double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); return l > 0 ? v * (1 / l) : v; }

// a node's own move / turn / size, the way FBX puts them together
M4 localOf(const P70& p, V3 t, V3 r, V3 s) {
    V3 roff = p.vec("RotationOffset", {}), rp = p.vec("RotationPivot", {}), soff = p.vec("ScalingOffset", {}), sp = p.vec("ScalingPivot", {});
    M4 pre = euler(p.vec("PreRotation", {}), 0), post = transpose3(euler(p.vec("PostRotation", {}), 0));
    int order = (int)p.num("RotationOrder", 0);
    M4 m = mul(trans(t), mul(trans(roff), mul(trans(rp), mul(pre, mul(euler(r, order), mul(post, trans(rp * -1)))))));
    return mul(m, mul(trans(soff), mul(trans(sp), mul(scal(s), trans(sp * -1)))));
}

} // namespace fbx

static bool importFbx(const fs::path& path, MeshModel& m, std::string& err) {
    using namespace fbx;
    std::string file = readFile(path); if (file.empty()) { err = "The file could not be opened."; return false; }
    Node root; if (!parse(file, root, err)) { err = "This FBX file could not be read (" + err + ")."; return false; }
    const Node* objs = root.child("Objects"); const Node* cons = root.child("Connections");
    if (!objs) { err = "This FBX file has nothing in it."; return false; }
    // units and which way is up
    P70 gs(root.child("GlobalSettings"));
    double unit = gs.num("UnitScaleFactor", 1);   // centimetres per file unit
    int up = (int)gs.num("UpAxis", 1), upSign = (int)gs.num("UpAxisSign", 1);
    M4 axisFix = ident();
    if (up == 2) axisFix = rot1(0, upSign > 0 ? -90 : 90);   // Z up (3ds Max, some Blender files): turn so Y is up
    std::map<int64_t, const Node*> byId;
    for (const Node& o : objs->kids) if (!o.props.empty()) byId[o.props[0].id()] = &o;
    std::multimap<int64_t, int64_t> kidsOf; std::map<int64_t, int64_t> parentOf;   // node tree (OO between models)
    std::multimap<int64_t, std::pair<int64_t, std::string>> into;                     // child -> (parent, property)
    std::multimap<int64_t, std::pair<int64_t, std::string>> from;                     // parent -> (child, property)
    if (cons) for (const Node& c : cons->kids) {
        if (c.name != "C" || c.props.size() < 3) continue;
        int64_t a = c.props[1].id(), b = c.props[2].id(); std::string prop = c.props.size() > 3 ? c.props[3].s : "";
        into.insert({ a, { b, prop } }); from.insert({ b, { a, prop } });
        auto ia = byId.find(a), ib = byId.find(b);
        if (ia != byId.end() && ia->second->name == "Model" && (b == 0 || (ib != byId.end() && ib->second->name == "Model"))) { parentOf[a] = b; kidsOf.insert({ b, a }); }
    }
    auto childrenOfType = [&](int64_t id, const std::string& type) { std::vector<std::pair<const Node*, std::string>> v; auto r = from.equal_range(id);
        for (auto it = r.first; it != r.second; ++it) { auto o = byId.find(it->second.first); if (o != byId.end() && o->second->name == type) v.push_back({ o->second, it->second.second }); } return v; };
    // rest pose: every model's place
    std::map<int64_t, M4> world;
    std::function<M4(int64_t)> worldOf = [&](int64_t id) -> M4 {
        auto w = world.find(id); if (w != world.end()) return w->second;
        auto o = byId.find(id); if (o == byId.end()) return axisFix;
        P70 p(o->second);
        M4 local = localOf(p, p.vec("Lcl Translation", {}), p.vec("Lcl Rotation", {}), p.vec("Lcl Scaling", { 1, 1, 1 }));
        auto pa = parentOf.find(id); M4 par = pa == parentOf.end() || pa->second == 0 ? axisFix : worldOf(pa->second);
        world[id] = mul(par, local); return world[id];
    };
    // animated models become parts; the curves become clips
    std::map<int64_t, int> partOf;
    std::vector<AnimClip> clips;
    for (const Node& st : objs->kids) {
        if (st.name != "AnimationStack" || st.props.empty()) continue;
        AnimClip clip; clip.name = clean(st.props.size() > 1 ? st.props[1].s : "anim");
        P70 sp(&st); const double tick = 46186158000.0;
        clip.length = (float)(sp.num("LocalStop", 0) / tick);
        for (auto& layer : childrenOfType(st.props[0].id(), "AnimationLayer"))
            for (auto& cn : childrenOfType(layer.first->props[0].id(), "AnimationCurveNode")) {
                int64_t cnId = cn.first->props[0].id();
                auto tr = into.equal_range(cnId);
                for (auto it = tr.first; it != tr.second; ++it) {
                    auto mo = byId.find(it->second.first); if (mo == byId.end() || mo->second->name != "Model") continue;
                    const std::string& prop = it->second.second;
                    int which = prop == "Lcl Translation" ? 0 : prop == "Lcl Rotation" ? 1 : prop == "Lcl Scaling" ? 2 : -1; if (which < 0) continue;
                    int64_t mid = it->second.first;
                    if (!partOf.count(mid)) {
                        int idx = (int)m.parts.size(); partOf[mid] = idx;
                        MeshPart pt{}; pt.name = clean(mo->second->props.size() > 1 ? mo->second->props[1].s : "part");
                        P70 mp(mo->second); auto pa = parentOf.find(mid); M4 par = pa == parentOf.end() || pa->second == 0 ? axisFix : worldOf(pa->second);
                        std::copy(par.begin(), par.end(), pt.parent);
                        V3 t = mp.vec("Lcl Translation", {}), r = mp.vec("Lcl Rotation", {}), s = mp.vec("Lcl Scaling", { 1, 1, 1 }), pre = mp.vec("PreRotation", {}), piv = mp.vec("RotationPivot", {});
                        double* dst[5] = { pt.t, pt.r, pt.s, pt.pre, pt.pivot }; V3 src[5] = { t, r, s, pre, piv };
                        for (int k = 0; k < 5; k++) { dst[k][0] = src[k].x; dst[k][1] = src[k].y; dst[k][2] = src[k].z; }
                        m.parts.push_back(pt);
                    }
                    for (auto& cv : childrenOfType(cnId, "AnimationCurve")) {
                        const std::string& ax = cv.second; int axis = ax == "d|X" ? 0 : ax == "d|Y" ? 1 : ax == "d|Z" ? 2 : -1; if (axis < 0) continue;
                        const std::vector<double>* kt = cv.first->list("KeyTime"); const std::vector<double>* kv = cv.first->list("KeyValueFloat");
                        if (!kt || !kv || kt->size() != kv->size() || kt->empty()) continue;
                        AnimTrack t; t.part = partOf[mid]; t.prop = which; t.axis = axis;
                        for (size_t k = 0; k < kt->size(); k++) { t.time.push_back((float)((*kt)[k] / tick)); t.value.push_back((float)(*kv)[k]); }
                        clip.length = std::max(clip.length, t.time.back());
                        clip.tracks.push_back(t);
                    }
                }
            }
        if (!clip.tracks.empty()) clips.push_back(clip);
    }
    m.clips = clips;
    auto movingPart = [&](int64_t id) -> int {   // the nearest animated node above (or at) this one
        for (int guard = 0; id != 0 && guard < 256; guard++) { auto p = partOf.find(id); if (p != partOf.end()) return p->second; auto pa = parentOf.find(id); if (pa == parentOf.end()) break; id = pa->second; }
        return -1;
    };
    for (auto& kv : partOf) {   // a moving part inside another moving part: it moves along with it
        auto pa = parentOf.find(kv.first); m.parts[kv.second].parentPart = pa == parentOf.end() ? -1 : movingPart(pa->second);
        auto mo = byId.find(kv.first); if (mo != byId.end()) { P70 mp(mo->second); m.parts[kv.second].order = std::clamp((int)mp.num("RotationOrder", 0), 0, 5); }
    }
    // materials -> pictures
    Builder B; std::map<const Node*, int> matImg;
    auto materialImage = [&](const Node* mat) -> int {
        auto it = matImg.find(mat); if (it != matImg.end()) return it->second;
        Img im; bool got = false;
        for (auto& tx : childrenOfType(mat->props[0].id(), "Texture")) {
            if (tx.second.find("Diffuse") == std::string::npos && !tx.second.empty() && tx.second != "Color") continue;
            for (auto& vd : childrenOfType(tx.first->props[0].id(), "Video")) {
                const Node* c = vd.first->child("Content");
                if (c && !c->props.empty()) {
                    if (c->props[0].type == 'R') got = imageFrom(c->props[0].raw, im);
                    else if (c->props[0].type == 'S') { std::string all; for (const Prop& q : c->props) all += q.s; if (!all.empty()) got = imageFrom(base64Decode(all), im); }   // long pictures come in several pieces
                }
                if (got) break;
            }
            if (!got) for (const char* key : { "RelativeFilename", "FileName" }) {
                std::string fn = tx.first->str(key); if (fn.empty()) continue;
                std::replace(fn.begin(), fn.end(), '\\', '/');
                for (fs::path cand : { path.parent_path() / fs::u8path(fn), path.parent_path() / fs::u8path(fn).filename() }) {
                    std::string d = readFile(cand); if (!d.empty() && imageFrom(std::vector<uint8_t>(d.begin(), d.end()), im)) { got = true; break; }
                }
                if (got) break;
            }
            if (got) break;
        }
        if (!got) { P70 p(mat); V3 c = p.vec("DiffuseColor", p.vec("Diffuse", { 0.8, 0.8, 0.8 })); im = solid((float)c.x, (float)c.y, (float)c.z, 1); }
        int id = (int)B.imgs.size(); B.imgs.push_back(im); matImg[mat] = id; return id;
    };
    std::vector<uint16_t> vpart;
    int meshes = 0;
    for (const Node& o : objs->kids) {
        if (o.name != "Model" || o.props.size() < 3 || o.props[2].s != "Mesh") continue;
        int64_t mid = o.props[0].id();
        const Node* geo = nullptr; for (auto& g : childrenOfType(mid, "Geometry")) { geo = g.first; break; }
        if (!geo) continue;
        std::vector<const Node*> mats; for (auto& mt : childrenOfType(mid, "Material")) mats.push_back(mt.first);
        P70 mp(&o);
        M4 w = mul(worldOf(mid), mul(trans(mp.vec("GeometricTranslation", {})), mul(euler(mp.vec("GeometricRotation", {}), 0), scal(mp.vec("GeometricScaling", { 1, 1, 1 })))));
        const std::vector<double>* V = geo->list("Vertices"); const std::vector<double>* PVI = geo->list("PolygonVertexIndex");
        if (!V || !PVI) continue;
        // layers: normals, texture coordinates, materials (how they map: per corner, per point, per face, or one for all)
        struct Layer { const std::vector<double>* data = nullptr; const std::vector<double>* index = nullptr; std::string map, ref; int comps = 0; };
        auto layer = [&](const char* name, const char* dataName, const char* indexName, int comps) {
            Layer L; const Node* n = geo->child(name); if (!n) return L;
            L.data = n->list(dataName); L.index = indexName ? n->list(indexName) : nullptr; L.map = n->str("MappingInformationType"); L.ref = n->str("ReferenceInformationType"); L.comps = comps;
            return L;
        };
        Layer LN = layer("LayerElementNormal", "Normals", "NormalsIndex", 3), LU = layer("LayerElementUV", "UV", "UVIndex", 2), LM = layer("LayerElementMaterial", "Materials", nullptr, 1);
        auto fetch = [&](const Layer& L, size_t poly, size_t corner, size_t vert, double* out) {
            if (!L.data) return false;
            size_t k = L.map == "ByPolygonVertex" ? corner : (L.map == "ByVertex" || L.map == "ByVertice") ? vert : L.map == "ByPolygon" ? poly : 0;
            if (L.ref == "IndexToDirect" && L.index) { if (k >= L.index->size()) return false; k = (size_t)(*L.index)[k]; }
            if ((k + 1) * L.comps > L.data->size()) return false;
            for (int c = 0; c < L.comps; c++) out[c] = (*L.data)[k * L.comps + c];
            return true;
        };
        int part = movingPart(mid);
        size_t poly = 0; std::vector<MeshVert> cur;
        for (size_t c = 0; c < PVI->size(); c++) {
            int64_t raw = (int64_t)(*PVI)[c]; bool last = raw < 0; size_t vi = (size_t)(last ? -raw - 1 : raw);
            if ((vi + 1) * 3 > V->size()) { cur.clear(); continue; }
            V3 p = point(w, { (*V)[vi * 3], (*V)[vi * 3 + 1], (*V)[vi * 3 + 2] });
            MeshVert mv{ { (float)p.x, (float)p.y, (float)p.z }, { 0, 0 }, { 0, 0, 0 } };
            double nn[3]; if (fetch(LN, poly, c, vi, nn)) { V3 n = dir(w, { nn[0], nn[1], nn[2] }); mv.n[0] = (float)n.x; mv.n[1] = (float)n.y; mv.n[2] = (float)n.z; }
            double uv[2]; if (fetch(LU, poly, c, vi, uv)) { mv.uv[0] = (float)uv[0]; mv.uv[1] = (float)(1 - uv[1]); }   // FBX counts from the bottom of the picture
            cur.push_back(mv);
            if (last) {
                double mi = 0; int image = 0;
                if (!mats.empty()) { fetch(LM, poly, c, vi, &mi); image = materialImage(mats[std::clamp((int)mi, 0, (int)mats.size() - 1)]); }
                else { if (B.imgs.empty()) B.imgs.push_back(Img{}); image = 0; }
                size_t before = B.v.size();
                for (size_t q = 1; q + 1 < cur.size(); q++) B.tri(cur[0], cur[q], cur[q + 1], image);
                vpart.resize(B.v.size(), 0xFFFF); for (size_t q = before; q < B.v.size(); q++) vpart[q] = part < 0 ? 0xFFFF : (uint16_t)part;
                cur.clear(); poly++;
            }
        }
        meshes++;
    }
    if (!meshes || B.i.empty()) { err = "There are no meshes in this FBX file."; return false; }
    B.finish(m);
    bool anyPart = false; for (uint16_t p : vpart) anyPart |= p != 0xFFFF;
    if (anyPart) m.vpart = vpart; else { m.parts.clear(); m.clips.clear(); }
    // FBX counts in centimetres (times UnitScaleFactor): 100 cm = 1 metre = 1 block
    m.unitScale = unit / 100.0; m.update();
    return true;
}
